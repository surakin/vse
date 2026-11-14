#include "project.h"

#include "algorithm.h"
#include "cstr/varargs.h"
#include "animation/components.h"
#include "asset/blob.h"
#include "asset/filesystem_backend.h"
#include "asset/level.h"
#include "asset/pak.h"
#include "asset/resource_table.h"
#include "asset/skeleton.h"
#include "clip_source.h"
#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "level_source.h"
#include "material_source.h"
#include "mesh_source.h"
#include "skeleton_source.h"
#include "source_text.h"
#include "storage/builtin_components.h"
#include "texture_source.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u64 kMaxSourceBytes = u64{1} << 28;

struct KindInfo
{
    SourceKind kind;
    const char* extension;  // the whole extension, `.src` included
    const char* name;
    ResourceType type;
};

const KindInfo kKinds[] = {
    {SourceKind::Skeleton, ".skel.src", "skeleton", types::kSkeleton},
    {SourceKind::Mesh, ".mesh.src", "mesh", types::kMesh},
    {SourceKind::Clip, ".anim.src", "clip", types::kAnimation},
    {SourceKind::Texture, ".tex.src", "texture", types::kTexture},
    {SourceKind::Material, ".mat.src", "material", types::kMaterial},
    {SourceKind::Level, ".level.src", "level", types::kLevel},
};

const KindInfo& InfoOf(SourceKind kind)
{
    return kKinds[static_cast<usize>(kind)];
}

String<HeapAllocator> Str(StringView text)
{
    return String<HeapAllocator>(HeapAllocator::Default(), text);
}

// One text line of formatted output.
String<HeapAllocator> Fmt(const char* format, ...)
{
    char buffer[768];
    VarArgs args;
    ENGINE_VA_START(args, format);
    VFormatTo(buffer, sizeof(buffer), format, args);
    ENGINE_VA_END(args);
    return Str(StringView(buffer, CStrLength(buffer)));
}

bool ReadFileText(const io::Path& path, u64 max_bytes, Vector<u8>* out)
{
    return io::ReadFile(path, out, max_bytes).Ok();
}

bool HexValue(char c, u32* out)
{
    if (c >= '0' && c <= '9')
    {
        *out = static_cast<u32>(c - '0');
    }
    else if (c >= 'a' && c <= 'f')
    {
        *out = static_cast<u32>(c - 'a' + 10);
    }
    else if (c >= 'A' && c <= 'F')
    {
        *out = static_cast<u32>(c - 'A' + 10);
    }
    else
    {
        return false;
    }
    return true;
}

// Sixteen hexadecimal digits: a resource id as written in a source.
bool IsIdText(StringView text)
{
    if (text.Length() != 16)
    {
        return false;
    }
    for (usize i = 0; i < 16; ++i)
    {
        u32 v = 0;
        if (!HexValue(text[i], &v))
        {
            return false;
        }
    }
    return true;
}

u64 HashText(StringView text)
{
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < text.Length(); ++i)
    {
        h = (h ^ static_cast<u8>(text[i])) * 1099511628211ull;
    }
    return h;
}

void Put32(ContentHasher* hasher, u32 value)
{
    byte b[4];
    for (u32 i = 0; i < 4; ++i)
    {
        b[i] = static_cast<byte>(value >> (8 * i));
    }
    hasher->Update(ByteSpan(b, 4));
}

void Put64(ContentHasher* hasher, u64 value)
{
    byte b[8];
    for (u32 i = 0; i < 8; ++i)
    {
        b[i] = static_cast<byte>(value >> (8 * i));
    }
    hasher->Update(ByteSpan(b, 8));
}

String<HeapAllocator> HashText16(const ContentHash& hash)
{
    char text[33];
    FormatContentHash(hash, text);
    return Str(StringView(text, 32));
}

bool ParseHash32(StringView text, ContentHash* out)
{
    if (text.Length() != 32)
    {
        return false;
    }
    for (usize i = 0; i < 16; ++i)
    {
        u32 hi = 0;
        u32 lo = 0;
        if (!HexValue(text[2 * i], &hi) || !HexValue(text[2 * i + 1], &lo))
        {
            return false;
        }
        out->bytes[i] = static_cast<u8>(hi * 16 + lo);
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Diagnostics

void MintDiagnostics::Error(StringView file, u32 line, StringView message)
{
    if (line != 0)
    {
        lines_.Push(Fmt("error: %.*s:%u: %.*s", static_cast<int>(file.Length()), file.Data(),
                        static_cast<unsigned>(line), static_cast<int>(message.Length()), message.Data()));
    }
    else
    {
        lines_.Push(Fmt("error: %.*s: %.*s", static_cast<int>(file.Length()), file.Data(),
                        static_cast<int>(message.Length()), message.Data()));
    }
    errors_ += 1;
}

void MintDiagnostics::Note(StringView text)
{
    lines_.Push(Fmt("note: %.*s", static_cast<int>(text.Length()), text.Data()));
}

bool MintDiagnostics::Mentions(StringView text) const
{
    for (usize i = 0; i < lines_.Size(); ++i)
    {
        if (lines_[i].View().Contains(text))
        {
            return true;
        }
    }
    return false;
}

const char* SourceKindName(SourceKind kind)
{
    return InfoOf(kind).name;
}

// ---------------------------------------------------------------------------------------------------------------------
// The project file

namespace
{

bool SafeRelative(StringView text)
{
    if (text.Length() == 0 || text[0] == '/' || text[0] == '\\' || (text.Length() > 1 && text[1] == ':'))
    {
        return false;
    }
    usize start = 0;
    for (usize i = 0; i <= text.Length(); ++i)
    {
        if (i < text.Length() && text[i] == '\\')
        {
            return false;  // the project file's paths use `/` on every system
        }
        if (i == text.Length() || text[i] == '/')
        {
            if (text.Substring(start, i - start) == StringView(".."))
            {
                return false;
            }
            start = i + 1;
        }
    }
    return true;
}

void Fail(ProjectError* error, u32 line, const char* message)
{
    error->line = line;
    FormatTo(error->message.Data(), error->message.Size(), "%s", message);
}

}  // namespace

Status ParseProjectConfig(StringView text, ProjectConfig* out, ProjectError* error)
{
    *error = ProjectError();
    bool have_source = false;
    bool have_output = false;
    bool have_pak = false;
    bool have_target = false;
    u32 line_number = 0;
    usize pos = 0;
    while (pos <= text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        const StringView line = text.Substring(pos, end - pos);
        pos = end + 1;
        line_number += 1;
        const srctext::LineTokens t = srctext::Tokenize(line);
        if (t.overflow)
        {
            Fail(error, line_number, "too many fields on the line");
            return Status::Make(ErrorCode::Invalid, "the project file is not valid");
        }
        if (t.count == 0)
        {
            continue;
        }
        const StringView key = t.tokens[0];
        if (key == StringView("source_root") || key == StringView("output_root") || key == StringView("pak"))
        {
            bool* seen = key == StringView("source_root")
                             ? &have_source
                             : (key == StringView("output_root") ? &have_output : &have_pak);
            if (t.count != 2 || *seen || !SafeRelative(t.tokens[1]))
            {
                Fail(error, line_number,
                     t.count != 2
                         ? "expected one relative path"
                         : (*seen ? "the record is repeated" : "the path must be relative and stay below the project"));
                return Status::Make(ErrorCode::Invalid, "the project file is not valid");
            }
            *seen = true;
            String<HeapAllocator>& dest = key == StringView("source_root")
                                              ? out->source_root
                                              : (key == StringView("output_root") ? out->output_root : out->pak);
            dest = Str(t.tokens[1]);
        }
        else if (key == StringView("target"))
        {
            if (t.count != 2 || have_target || !srctext::ParseUnsigned(t.tokens[1], &out->target))
            {
                Fail(error, line_number, "expected one number, once");
                return Status::Make(ErrorCode::Invalid, "the project file is not valid");
            }
            have_target = true;
        }
        else if (key == StringView("exclude"))
        {
            if (t.count != 2 || !SafeRelative(t.tokens[1]))
            {
                Fail(error, line_number, "expected one relative path prefix");
                return Status::Make(ErrorCode::Invalid, "the project file is not valid");
            }
            StringView prefix = t.tokens[1];
            while (prefix.Length() > 1 && prefix[prefix.Length() - 1] == '/')
            {
                prefix = prefix.Substring(0, prefix.Length() - 1);  // `drafts/` and `drafts` are the same prefix
            }
            out->excludes.Push(Str(prefix));
        }
        else if (key == StringView("rule"))
        {
            bool known = false;
            for (const KindInfo& k : kKinds)
            {
                known = known || (t.count >= 2 && t.tokens[1] == StringView(k.extension));
            }
            if (t.count != 3 || !known || t.tokens[2] != StringView("skip"))
            {
                Fail(error, line_number, "expected `rule EXTENSION skip` with a known source extension");
                return Status::Make(ErrorCode::Invalid, "the project file is not valid");
            }
            out->skipped_extensions.Push(Str(t.tokens[1]));
        }
        else
        {
            Fail(error, line_number, "unknown record");
            return Status::Make(ErrorCode::Invalid, "the project file is not valid");
        }
    }
    if (!have_source || !have_output)
    {
        Fail(error, 0, "`source_root` and `output_root` are required");
        return Status::Make(ErrorCode::Invalid, "the project file is not valid");
    }
    if (!have_pak)
    {
        out->pak = Str(StringView("assets.pak"));
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// The project

struct Project::Source
{
    Source() : rel(HeapAllocator::Default()), vpath(HeapAllocator::Default()), blob_rel(HeapAllocator::Default()) {}
    SourceKind kind = SourceKind::Skeleton;
    String<HeapAllocator> rel;       // the path below the source root, e.g. `meshes/rock.mesh.src`
    String<HeapAllocator> vpath;     // the resource path, e.g. `/meshes/rock`
    String<HeapAllocator> blob_rel;  // the blob's path below the output root
    io::Path file;
    Vector<u8> text;
    ResourceId id;
    bool has_id = false;
    u32 id_line = 0;
    Vector<u32> deps;  // indices of the sources it references, sorted, without repeats
    u32 wave = 0;
    // The state of a mint.
    Vector<byte> blob;
    ContentHash blob_hash;
    ContentHash input_hash;
    bool ok = false;
    bool minted = false;
    bool rewritten = false;
    Vector<String<HeapAllocator>> messages;  // collected on a worker, reported in order afterwards
    Vector<u32> message_lines;
};

struct Project::Impl
{
    Impl() : by_id(HeapAllocator::Default()), by_path(HeapAllocator::Default()) {}
    TypeRegistry types;
    Vector<UniquePtr<Source>> sources;  // sorted by `rel`
    HashMap<u64, u32, HeapAllocator> by_id;
    HashMap<u64, u32, HeapAllocator> by_path;  // keyed by a hash of the resource path; compared on a hit
    usize load_errors = 0;
    bool loaded = false;
    storage::ComponentRegistry components;
    bool components_ready = false;

    i64 FindId(ResourceId id) const
    {
        const u32* found = by_id.Find(id.Value());
        return found == nullptr ? -1 : static_cast<i64>(*found);
    }
    i64 FindPath(StringView vpath) const
    {
        const u32* found = by_path.Find(HashText(vpath));
        if (found == nullptr || sources[*found]->vpath.View() != vpath)
        {
            return -1;
        }
        return static_cast<i64>(*found);
    }
};

Project::Project() : impl_(MakeUnique<Impl>()) {}
Project::~Project() = default;

bool Project::IsLoaded() const
{
    return impl_->loaded;
}

usize Project::SourceCount() const
{
    return impl_->sources.Size();
}

namespace
{

struct WalkState
{
    const io::Path* root;
    const ProjectConfig* config;
    Vector<String<HeapAllocator>> found;  // relative paths of the sources
};

bool IsExcluded(const ProjectConfig& config, StringView rel)
{
    for (usize i = 0; i < config.excludes.Size(); ++i)
    {
        const StringView prefix = config.excludes[i].View();
        if (rel.StartsWith(prefix) && (rel.Length() == prefix.Length() || rel[prefix.Length()] == '/'))
        {
            return true;
        }
    }
    return false;
}

const KindInfo* KindOfFile(StringView rel)
{
    for (const KindInfo& k : kKinds)
    {
        if (rel.EndsWith(StringView(k.extension)) && rel.Length() > CStrLength(k.extension))
        {
            return &k;
        }
    }
    return nullptr;
}

io::WalkAction VisitSource(void* user, const io::WalkEntry& entry)
{
    WalkState* state = static_cast<WalkState*>(user);
    if (entry.type != io::FileType::Regular)
    {
        return io::WalkAction::Continue;
    }
    const StringView full = entry.path.View();
    const StringView root = state->root->View();
    if (!full.StartsWith(root) || full.Length() <= root.Length() + 1)
    {
        return io::WalkAction::Continue;
    }
    const StringView rel = full.Substring(root.Length() + 1);
    if (KindOfFile(rel) == nullptr || IsExcluded(*state->config, rel))
    {
        return io::WalkAction::Continue;
    }
    for (usize i = 0; i < state->config->skipped_extensions.Size(); ++i)
    {
        if (rel.EndsWith(state->config->skipped_extensions[i].View()))
        {
            return io::WalkAction::Continue;
        }
    }
    state->found.Push(Str(rel));
    return io::WalkAction::Continue;
}

// The id of a source: the first record must be `id HEX16` (comments and blank lines may come before it).
void ScanId(const Vector<u8>& text, ResourceId* id, bool* has_id, u32* line_number)
{
    *has_id = false;
    const StringView all(reinterpret_cast<const char*>(text.Data()), text.Size());
    usize pos = 0;
    u32 n = 0;
    while (pos <= all.Length())
    {
        usize end = pos;
        while (end < all.Length() && all[end] != '\n')
        {
            ++end;
        }
        n += 1;
        const srctext::LineTokens t = srctext::Tokenize(all.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 0)
        {
            continue;
        }
        if (t.tokens[0] == StringView("id") && t.count == 2 && IsIdText(t.tokens[1]) &&
            ParseResourceId(t.tokens[1], id).Ok())
        {
            *has_id = true;
            *line_number = n;
        }
        return;
    }
}

// The directory part of a resource path: `/a/b/c` gives `/a/b`.
StringView DirectoryOf(StringView vpath)
{
    usize last = 0;
    for (usize i = 0; i < vpath.Length(); ++i)
    {
        if (vpath[i] == '/')
        {
            last = i;
        }
    }
    return vpath.Substring(0, last);
}

// The resource path a token names, seen from the source at `from` (an absolute path, or one relative to its folder).
bool TokenPath(StringView token, StringView from_vpath, String<HeapAllocator>* out)
{
    if (token.Length() == 0)
    {
        return false;
    }
    String<HeapAllocator> joined = Str(token[0] == '/' ? StringView() : DirectoryOf(from_vpath));
    if (token[0] != '/')
    {
        joined.Append('/');
    }
    joined.Append(token);
    return NormalizePath(joined.View(), out).Ok();
}

}  // namespace

Status Project::Load(const io::Path& project_file, MintDiagnostics* diagnostics)
{
    impl_ = MakeUnique<Impl>();
    Impl& impl = *impl_;
    project_file_ = project_file;
    const StringView project_name = project_file.View();
    Vector<u8> text;
    if (!ReadFileText(project_file, u64{1} << 20, &text))
    {
        diagnostics->Error(project_name, 0, StringView("the project file cannot be read"));
        return Status::Make(ErrorCode::NotFound, "the project file cannot be read");
    }
    config_ = ProjectConfig();
    ProjectError parse_error;
    if (ParseProjectConfig(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), &config_, &parse_error)
            .Failed())
    {
        diagnostics->Error(project_name, parse_error.line, StringView(parse_error.message.Data()));
        return Status::Make(ErrorCode::Invalid, "the project file is not valid");
    }
    const io::Path base = project_file.Parent();
    source_root_ = base / config_.source_root.View();
    output_root_ = base / config_.output_root.View();

    WalkState walk;
    walk.root = &source_root_;
    walk.config = &config_;
    io::WalkOptions options;
    options.sorted = true;
    options.max_depth = 64;
    if (io::WalkDirectory(source_root_, options, &VisitSource, &walk).Failed())
    {
        diagnostics->Error(source_root_.View(), 0, StringView("the source folder cannot be read"));
        return Status::Make(ErrorCode::NotFound, "the source folder cannot be read");
    }
    for (usize i = 0; i < walk.found.Size(); ++i)
    {
        UniquePtr<Source> source = MakeUnique<Source>();
        source->rel = Str(walk.found[i].View());
        const KindInfo* info = KindOfFile(source->rel.View());
        source->kind = info->kind;
        const StringView rel = source->rel.View();
        bool spaced = false;
        for (usize k = 0; k < rel.Length(); ++k)
        {
            spaced = spaced || rel[k] == ' ' || rel[k] == '\t' || rel[k] == '\n';
        }
        if (spaced)
        {
            diagnostics->Error(rel, 0, StringView("a source path may not contain blanks"));
            impl.load_errors += 1;
            continue;
        }
        // `meshes/rock.mesh.src` -> `/meshes/rock`; the blob is `meshes/rock.mesh.blob`.
        const StringView stem = rel.Substring(0, rel.Length() - CStrLength(info->extension));
        String<HeapAllocator> raw = Str(StringView("/"));
        raw.Append(stem);
        if (NormalizePath(raw.View(), &source->vpath).Failed())
        {
            diagnostics->Error(rel, 0, StringView("the resource path made from the file name is not valid"));
            impl.load_errors += 1;
            continue;
        }
        source->blob_rel = Str(rel.Substring(0, rel.Length() - 3));
        source->blob_rel.Append(StringView("blob"));
        source->file = source_root_ / rel;
        if (!ReadFileText(source->file, kMaxSourceBytes, &source->text))
        {
            diagnostics->Error(rel, 0, StringView("the source cannot be read"));
            impl.load_errors += 1;
            continue;
        }
        ScanId(source->text, &source->id, &source->has_id, &source->id_line);
        if (!source->has_id)
        {
            diagnostics->Error(rel, 0,
                               StringView("the source has no `id` line: run `asset_mint adopt` to give it one"));
            impl.load_errors += 1;
        }
        impl.sources.Push(static_cast<UniquePtr<Source>&&>(source));
    }

    // Ids and paths must be unique: two sources with one id (a plain copy) are named together.
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        Source& s = *impl.sources[i];
        const u32 index = static_cast<u32>(i);
        if (s.has_id)
        {
            const u32* other = impl.by_id.Find(s.id.Value());
            if (other != nullptr)
            {
                char id_text[17];
                FormatResourceId(s.id, id_text);
                diagnostics->Error(s.rel.View(), s.id_line,
                                   Fmt("the id %s is also used by %s: run `asset_mint reid` on one of them", id_text,
                                       impl.sources[*other]->rel.CStr())
                                       .View());
                impl.load_errors += 1;
            }
            else
            {
                impl.by_id.Insert(s.id.Value(), index);
            }
        }
        const u64 key = HashText(s.vpath.View());
        const u32* same = impl.by_path.Find(key);
        if (same != nullptr && impl.sources[*same]->vpath.View() == s.vpath.View())
        {
            diagnostics->Error(
                s.rel.View(), 0,
                Fmt("the resource path %s is also made by %s", s.vpath.CStr(), impl.sources[*same]->rel.CStr()).View());
            impl.load_errors += 1;
        }
        else
        {
            impl.by_path.Insert(key, index);
        }
    }

    // The references: every word of a source that names another source's path or id. Texture and skeleton sources
    // name no resources (a texture names image files).
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        Source& s = *impl.sources[i];
        if (s.kind == SourceKind::Skeleton || s.kind == SourceKind::Texture)
        {
            continue;
        }
        const StringView all(reinterpret_cast<const char*>(s.text.Data()), s.text.Size());
        usize pos = 0;
        u32 line_number = 0;
        while (pos <= all.Length())
        {
            usize end = pos;
            while (end < all.Length() && all[end] != '\n')
            {
                ++end;
            }
            line_number += 1;
            const srctext::LineTokens t = srctext::Tokenize(all.Substring(pos, end - pos));
            pos = end + 1;
            for (usize k = 0; k < t.count; ++k)
            {
                StringView token = t.tokens[k];
                if (token.Length() >= 2 && token[0] == '"' && token[token.Length() - 1] == '"')
                {
                    token = token.Substring(1, token.Length() - 2);
                }
                // What counts as a reference: `@HEX16` anywhere, a bare id after `skeleton`, a path that starts with
                // `/`, and the path of a material's `texture SLOT PATH` line (which may be relative to the material's
                // folder). Other words are not looked at, so a name that happens to be a sibling's file name is not a
                // reference.
                const bool keyword_target = k == 1 && (t.tokens[0] == StringView("skeleton") ||
                                                       t.tokens[0] == StringView("include") ||
                                                       t.tokens[0] == StringView("animation"));
                const bool texture_path = s.kind == SourceKind::Material && k == 2 &&
                                          t.tokens[0] == StringView("texture");
                i64 found = -1;
                bool is_reference = false;
                if (token.Length() == 17 && token[0] == '@' && IsIdText(token.Substring(1)))
                {
                    is_reference = true;
                    ResourceId id;
                    if (ParseResourceId(token.Substring(1), &id).Ok())
                    {
                        found = impl.FindId(id);
                    }
                }
                else if (IsIdText(token) && k == 1 && t.tokens[0] == StringView("skeleton"))
                {
                    is_reference = true;
                    ResourceId id;
                    if (ParseResourceId(token, &id).Ok())
                    {
                        found = impl.FindId(id);
                    }
                }
                else if (token.Length() != 0 && (token[0] == '/' || texture_path))
                {
                    is_reference = keyword_target || texture_path;
                    String<HeapAllocator> vpath(HeapAllocator::Default());
                    if (TokenPath(token, s.vpath.View(), &vpath))
                    {
                        found = impl.FindPath(vpath.View());
                    }
                }
                if (found < 0 && is_reference && (keyword_target || texture_path))
                {
                    diagnostics->Error(s.rel.View(), line_number,
                                       Fmt("the reference %.*s names no source of the project",
                                           static_cast<int>(token.Length()), token.Data())
                                           .View());
                    impl.load_errors += 1;
                }
                if (found >= 0 && static_cast<usize>(found) != i)
                {
                    bool known = false;
                    for (usize d = 0; d < s.deps.Size(); ++d)
                    {
                        known = known || s.deps[d] == static_cast<u32>(found);
                    }
                    if (!known)
                    {
                        s.deps.Push(static_cast<u32>(found));
                    }
                }
            }
        }
        Sort(Span<u32>(s.deps.Data(), s.deps.Size()));
    }

    // What a kind may name: meshes and clips a skeleton, materials textures.
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        const Source& s = *impl.sources[i];
        for (usize d = 0; d < s.deps.Size(); ++d)
        {
            const Source& dep = *impl.sources[s.deps[d]];
            const bool bad = ((s.kind == SourceKind::Mesh || s.kind == SourceKind::Clip) &&
                              dep.kind != SourceKind::Skeleton) ||
                             (s.kind == SourceKind::Material && dep.kind != SourceKind::Texture);
            if (bad)
            {
                diagnostics->Error(
                    s.rel.View(), 0,
                    Fmt("a %s may not reference the %s %s", InfoOf(s.kind).name, InfoOf(dep.kind).name, dep.rel.CStr())
                        .View());
                impl.load_errors += 1;
            }
        }
    }

    // The order: a wave is one more than the highest wave it depends on; a cycle is named.
    Vector<u8> state;  // 0 new, 1 on the stack, 2 done
    state.Resize(impl.sources.Size());
    for (usize i = 0; i < state.Size(); ++i)
    {
        state[i] = 0;
    }
    Vector<u32> stack;
    Vector<u32> next;
    for (usize root = 0; root < impl.sources.Size(); ++root)
    {
        if (state[root] != 0)
        {
            continue;
        }
        stack.Push(static_cast<u32>(root));
        next.Push(0);
        state[root] = 1;
        while (stack.Size() != 0)
        {
            const u32 at = stack[stack.Size() - 1];
            Source& s = *impl.sources[at];
            u32& cursor = next[next.Size() - 1];
            if (cursor < s.deps.Size())
            {
                const u32 dep = s.deps[cursor++];
                if (state[dep] == 1)
                {
                    String<HeapAllocator> names = Str(StringView());
                    bool in_cycle = false;
                    for (usize k = 0; k < stack.Size(); ++k)
                    {
                        in_cycle = in_cycle || stack[k] == dep;
                        if (in_cycle)
                        {
                            names.Append(impl.sources[stack[k]]->rel.View());
                            names.Append(StringView(" -> "));
                        }
                    }
                    names.Append(impl.sources[dep]->rel.View());
                    diagnostics->Error(s.rel.View(), 0, Fmt("the references form a cycle: %s", names.CStr()).View());
                    impl.load_errors += 1;
                }
                else if (state[dep] == 0)
                {
                    state[dep] = 1;
                    stack.Push(dep);
                    next.Push(0);
                }
            }
            else
            {
                u32 wave = 0;
                for (usize d = 0; d < s.deps.Size(); ++d)
                {
                    const u32 w = impl.sources[s.deps[d]]->wave + 1;
                    wave = w > wave ? w : wave;
                }
                s.wave = wave;
                state[at] = 2;
                stack.Pop();
                next.Pop();
            }
        }
    }
    impl.loaded = true;
    return impl.load_errors == 0 ? Status::Success() : Status::Make(ErrorCode::Invalid, "the project has errors");
}

Status Project::Check(MintDiagnostics* diagnostics)
{
    if (!impl_->loaded)
    {
        diagnostics->Error(project_file_.View(), 0, StringView("the project is not loaded"));
        return Status::Make(ErrorCode::Invalid, "the project is not loaded");
    }
    // `Load` reported everything it found; what it found is what the check says.
    return impl_->load_errors == 0 ? Status::Success() : Status::Make(ErrorCode::Invalid, "the project has errors");
}

Status Project::Adopt(MintDiagnostics* diagnostics, u32* adopted)
{
    Impl& impl = *impl_;
    *adopted = 0;
    // Adopt works on what `Load` found even when ids are missing (that is what it is for); a duplicate id or a bad
    // reference is not its business.
    if (!impl.loaded)
    {
        diagnostics->Error(project_file_.View(), 0, StringView("the project is not loaded"));
        return Status::Make(ErrorCode::Invalid, "the project is not loaded");
    }
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        Source& s = *impl.sources[i];
        if (s.has_id)
        {
            continue;
        }
        ResourceId id = GenerateResourceId();
        while (impl.FindId(id) >= 0)
        {
            id = GenerateResourceId();
        }
        char id_text[17];
        FormatResourceId(id, id_text);
        Vector<byte> out;
        const char* head = "id ";
        for (usize k = 0; head[k] != '\0'; ++k)
        {
            out.Push(static_cast<byte>(head[k]));
        }
        for (usize k = 0; k < 16; ++k)
        {
            out.Push(static_cast<byte>(id_text[k]));
        }
        out.Push(static_cast<byte>('\n'));
        for (usize k = 0; k < s.text.Size(); ++k)
        {
            out.Push(static_cast<byte>(s.text[k]));
        }
        if (io::WriteFileAtomic(s.file, out.Data(), out.Size()).Failed())
        {
            diagnostics->Error(s.rel.View(), 0, StringView("the source cannot be written"));
            return Status::Make(ErrorCode::Io, "the source cannot be written");
        }
        s.id = id;
        s.has_id = true;
        impl.by_id.Insert(id.Value(), static_cast<u32>(i));
        *adopted += 1;
        diagnostics->Note(Fmt("%s: id %s", s.rel.CStr(), id_text).View());
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// Minting

namespace
{

constexpr const char* kManifestName = "assets.manifest";
constexpr const char* kTableName = "resources.table";

struct ManifestEntry
{
    ManifestEntry() : rel(HeapAllocator::Default()) {}
    ResourceId id;
    String<HeapAllocator> rel;
    ContentHash input;
    ContentHash blob;
    Vector<u64> deps;
};

const ManifestEntry* FindEntry(const Vector<ManifestEntry>& entries, const HashMap<u64, u32, HeapAllocator>& index,
                               ResourceId id)
{
    const u32* at = index.Find(id.Value());
    return at == nullptr ? nullptr : &entries[*at];
}

// A missing, foreign or other-version manifest is an empty one: everything is minted again.
void LoadManifest(const io::Path& path, Vector<ManifestEntry>* out)
{
    out->Clear();
    Vector<u8> bytes;
    if (!ReadFileText(path, u64{1} << 28, &bytes))
    {
        return;
    }
    const StringView all(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    usize pos = 0;
    bool header_ok = false;
    bool version_ok = false;
    while (pos < all.Length())
    {
        usize end = pos;
        while (end < all.Length() && all[end] != '\n')
        {
            ++end;
        }
        const srctext::LineTokens t = srctext::Tokenize(all.Substring(pos, end - pos));
        pos = end + 1;
        if (t.count == 2 && t.tokens[0] == StringView("asset_mint_manifest") && t.tokens[1] == StringView("1"))
        {
            header_ok = true;
        }
        else if (t.count == 2 && t.tokens[0] == StringView("tool"))
        {
            u32 v = 0;
            version_ok = srctext::ParseUnsigned(t.tokens[1], &v) && v == kMintToolVersion;
        }
        else if (t.count == 6 && t.tokens[0] == StringView("S"))
        {
            ManifestEntry e;
            if (!ParseResourceId(t.tokens[1], &e.id).Ok() || !ParseHash32(t.tokens[3], &e.input) ||
                !ParseHash32(t.tokens[4], &e.blob))
            {
                out->Clear();
                return;
            }
            e.rel = Str(t.tokens[2]);
            const StringView deps = t.tokens[5];
            usize at = 0;
            while (deps != StringView("-") && at < deps.Length())
            {
                usize comma = at;
                while (comma < deps.Length() && deps[comma] != ',')
                {
                    ++comma;
                }
                ResourceId dep;
                if (!ParseResourceId(deps.Substring(at, comma - at), &dep).Ok())
                {
                    out->Clear();
                    return;
                }
                e.deps.Push(dep.Value());
                at = comma + 1;
            }
            out->Push(static_cast<ManifestEntry&&>(e));
        }
    }
    if (!header_ok || !version_ok)
    {
        out->Clear();
    }
}

struct ProjectMint
{
    Project::Impl* impl = nullptr;
    const io::Path* output_root = nullptr;
    const Vector<ManifestEntry>* manifest = nullptr;
    const HashMap<u64, u32, HeapAllocator>* manifest_index = nullptr;  // an id's place in `manifest`
    const Vector<Vector<u32>>* closures = nullptr;  // per source: the sources it references, directly or not
    u32 target = 0;
    bool force = false;
    jobs::Scheduler* scheduler = nullptr;
};

}  // namespace

namespace
{

// Resolves what a material or a level names, for one source: only what it references, directly or through what it
// references (those are all in earlier waves, so the answer does not depend on the schedule).
class SourceResolver final : public MaterialReferenceResolver, public LevelReferenceResolver
{
public:
    SourceResolver(const Project::Impl& impl, const Project::Source& from, const Vector<u32>& closure)
        : impl_(impl), from_(from), closure_(closure)
    {
    }

    Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const override { return ByPath(path, id, blob); }
    Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        return ByPath(path, id, blob);
    }
    Status ResolveId(ResourceId id, ByteSpan* blob) const override
    {
        const i64 index = impl_.FindId(id);
        return Take(index, blob);
    }
    Status PathOf(ResourceId id, String<HeapAllocator>* path) const override
    {
        const i64 index = impl_.FindId(id);
        if (index < 0)
        {
            return Status::Make(ErrorCode::NotFound, "the project has no path for the id");
        }
        *path = Str(impl_.sources[static_cast<usize>(index)]->vpath.View());
        return Status::Success();
    }

private:
    Status ByPath(StringView path, ResourceId* id, ByteSpan* blob) const
    {
        String<HeapAllocator> vpath(HeapAllocator::Default());
        if (!TokenPath(path, from_.vpath.View(), &vpath))
        {
            return Status::Make(ErrorCode::Invalid, "the path is not valid");
        }
        const i64 index = impl_.FindPath(vpath.View());
        if (index < 0)
        {
            return Status::Make(ErrorCode::NotFound, "the project has no such path");
        }
        *id = impl_.sources[static_cast<usize>(index)]->id;
        return Take(index, blob);
    }
    Status Take(i64 index, ByteSpan* blob) const
    {
        if (index < 0)
        {
            return Status::Make(ErrorCode::NotFound, "the project has no such resource");
        }
        bool allowed = false;
        for (usize i = 0; i < closure_.Size(); ++i)
        {
            allowed = allowed || closure_[i] == static_cast<u32>(index);
        }
        const Project::Source& s = *impl_.sources[static_cast<usize>(index)];
        if (!allowed || !s.ok)
        {
            return Status::Make(ErrorCode::NotFound, "the resource is not one the source references");
        }
        *blob = ByteSpan(s.blob.Data(), s.blob.Size());
        return Status::Success();
    }

    const Project::Impl& impl_;
    const Project::Source& from_;
    const Vector<u32>& closure_;
};

void AddClosure(const Project::Impl& impl, u32 index, Vector<u32>* closure)
{
    const Project::Source& s = *impl.sources[index];
    for (usize d = 0; d < s.deps.Size(); ++d)
    {
        bool known = false;
        for (usize k = 0; k < closure->Size(); ++k)
        {
            known = known || (*closure)[k] == s.deps[d];
        }
        if (!known)
        {
            closure->Push(s.deps[d]);
            AddClosure(impl, s.deps[d], closure);
        }
    }
}

void Complain(Project::Source& s, u32 line, StringView text)
{
    s.ok = false;
    s.messages.Push(Str(text));
    s.message_lines.Push(line);
}

template <typename Error> void ComplainWith(Project::Source& s, const Error& error)
{
    Complain(s, error.line, StringView(error.message.Data()));
}

bool RegisterLevelRegistry(storage::ComponentRegistry* registry)
{
    return storage::RegisterBuiltinComponents(registry).Ok() && RegisterLevelComponents(registry).Ok() &&
           animation::RegisterAnimationComponents(registry).Ok();
}

// Mints one source (on a worker, or on the main thread for a texture). Everything it writes is its own.
void MintOne(const ProjectMint& c, usize index)
{
    Project::Impl& impl = *c.impl;
    Project::Source& s = *impl.sources[index];
    s.ok = false;
    s.minted = false;
    s.rewritten = false;
    s.messages.Clear();
    s.message_lines.Clear();
    for (usize d = 0; d < s.deps.Size(); ++d)
    {
        if (!impl.sources[s.deps[d]]->ok)
        {
            Complain(s, 0,
                     Fmt("not minted: %s, which it references, failed", impl.sources[s.deps[d]]->rel.CStr()).View());
            return;
        }
    }
    const Vector<u32>& closure = (*c.closures)[index];
    const KindInfo& info = InfoOf(s.kind);
    TypeInfo type_info;
    impl.types.Find(info.type, &type_info);

    // A texture's inputs include its raw image files, found next to the source.
    Vector<Vector<byte>> images;
    TextureSource texture;
    const StringView text(reinterpret_cast<const char*>(s.text.Data()), s.text.Size());
    if (s.kind == SourceKind::Texture)
    {
        TextureSourceError error;
        if (ParseTextureSource(text, &texture, &error).Failed())
        {
            ComplainWith(s, error);
            return;
        }
        const io::Path directory = s.file.Parent();
        for (usize i = 0; i < texture.sources.Size(); ++i)
        {
            io::Path relative;
            io::Path image_path;
            const StringView name(texture.sources[i].name.Data(), CStrLength(texture.sources[i].name.Data()));
            Vector<u8> bytes;
            if (!io::Path::Parse(name, io::NativePathStyle(), &relative) || !directory.TryJoin(relative, &image_path) ||
                !ReadFileText(image_path, u64{1} << 31, &bytes))
            {
                Complain(s, texture.sources[i].line, StringView("the image file cannot be read"));
                return;
            }
            images.Push(static_cast<Vector<byte>&&>(bytes));
        }
    }

    // The hash of everything the blob is made from.
    ContentHasher hasher;
    hasher.Update(ByteSpan(reinterpret_cast<const byte*>("asset_mint"), 10));
    Put32(&hasher, kMintToolVersion);
    Put32(&hasher, c.target);
    Put32(&hasher, static_cast<u32>(s.kind));
    Put32(&hasher, type_info.current_version);
    Put64(&hasher, s.text.Size());
    hasher.Update(ByteSpan(reinterpret_cast<const byte*>(s.text.Data()), s.text.Size()));
    for (usize i = 0; i < images.Size(); ++i)
    {
        Put64(&hasher, images[i].Size());
        hasher.Update(ByteSpan(images[i].Data(), images[i].Size()));
    }
    for (usize i = 0; i < closure.Size(); ++i)
    {
        const Project::Source& dep = *impl.sources[closure[i]];
        Put64(&hasher, dep.id.Value());
        hasher.Update(ByteSpan(dep.blob_hash.bytes, 16));
    }
    s.input_hash = hasher.Finish();

    const io::Path blob_path = *c.output_root / s.blob_rel.View();
    // Unchanged: the manifest has these inputs and the file on disk is the blob it recorded.
    const ManifestEntry* entry = FindEntry(*c.manifest, *c.manifest_index, s.id);
    if (!c.force && entry != nullptr && entry->rel.View() == s.rel.View() && entry->input == s.input_hash)
    {
        Vector<u8> on_disk;
        if (ReadFileText(blob_path, u64{1} << 31, &on_disk) &&
            ComputeBlobHash(ByteSpan(on_disk.Data(), on_disk.Size())) == entry->blob)
        {
            s.blob = static_cast<Vector<byte>&&>(on_disk);
            s.blob_hash = entry->blob;
            s.ok = true;
            return;
        }
    }

    s.minted = true;
    SourceResolver resolver(impl, s, closure);
    Vector<byte> blob;
    switch (s.kind)
    {
    case SourceKind::Skeleton:
    {
        SkeletonSourceError error;
        if (MintSkeleton(text, impl.types, &blob, &error, c.target).Failed())
        {
            ComplainWith(s, error);
            return;
        }
        break;
    }
    case SourceKind::Mesh:
    case SourceKind::Clip:
    {
        // The skeleton is the one skeleton source the mesh or clip references.
        const Project::Source* skeleton_source = nullptr;
        for (usize d = 0; d < s.deps.Size(); ++d)
        {
            if (impl.sources[s.deps[d]]->kind == SourceKind::Skeleton)
            {
                skeleton_source = impl.sources[s.deps[d]].Get();
            }
        }
        SkeletonView skeleton;
        if (skeleton_source != nullptr &&
            skeleton.Open(ByteSpan(skeleton_source->blob.Data(), skeleton_source->blob.Size()), impl.types).Failed())
        {
            Complain(s, 0, StringView("the skeleton it references is not a skeleton blob"));
            return;
        }
        if (s.kind == SourceKind::Mesh)
        {
            MeshSourceError error;
            if (MintMesh(text, impl.types, skeleton_source != nullptr ? &skeleton : nullptr, &blob, &error, c.target)
                    .Failed())
            {
                ComplainWith(s, error);
                return;
            }
        }
        else
        {
            if (skeleton_source == nullptr)
            {
                Complain(s, 0, StringView("a clip needs the skeleton source it names in this project"));
                return;
            }
            ClipSourceError error;
            const ClipLimits limits;
            if (MintClip(text, skeleton, impl.types, limits, &blob, &error, c.target).Failed())
            {
                ComplainWith(s, error);
                return;
            }
        }
        break;
    }
    case SourceKind::Texture:
    {
        Vector<ByteSpan> spans;
        for (usize i = 0; i < images.Size(); ++i)
        {
            spans.Push(ByteSpan(images[i].Data(), images[i].Size()));
        }
        TextureSourceError error;
        if (MintTexture(text, Span<const ByteSpan>(spans.Data(), spans.Size()), impl.types, &blob, &error, c.target,
                        IsBcFormat(texture.format) ? c.scheduler : nullptr)
                .Failed())
        {
            ComplainWith(s, error);
            return;
        }
        break;
    }
    case SourceKind::Material:
    {
        MaterialSourceError error;
        if (MintMaterial(text, resolver, impl.types, &blob, &error, c.target).Failed())
        {
            ComplainWith(s, error);
            return;
        }
        break;
    }
    case SourceKind::Level:
    {
        LevelSourceError error;
        if (MintLevel(text, resolver, impl.types, impl.components, &blob, &error, c.target).Failed())
        {
            ComplainWith(s, error);
            return;
        }
        break;
    }
    }

    BlobHeaderInfo header;
    if (PeekBlobHeader(ByteSpan(blob.Data(), blob.Size()), &header).Failed() || header.id != s.id ||
        header.type != info.type)
    {
        Complain(s, 0, StringView("the minter made a blob of another id or type than the source"));
        return;
    }
    s.blob_hash = ComputeBlobHash(ByteSpan(blob.Data(), blob.Size()));
    Vector<u8> on_disk;
    const bool same = ReadFileText(blob_path, u64{1} << 31, &on_disk) && on_disk.Size() == blob.Size() &&
                      MemCompare(on_disk.Data(), blob.Data(), blob.Size()) == 0;
    if (!same)
    {
        if (io::WriteFileAtomic(blob_path, blob.Data(), blob.Size()).Failed())
        {
            Complain(s, 0, StringView("the blob cannot be written"));
            return;
        }
        s.rewritten = true;
    }
    s.blob = static_cast<Vector<byte>&&>(blob);
    s.ok = true;
}

bool WriteIfDifferent(const io::Path& path, const void* data, usize size)
{
    Vector<u8> existing;
    if (ReadFileText(path, u64{1} << 31, &existing) && existing.Size() == size &&
        (size == 0 || MemCompare(existing.Data(), data, size) == 0))
    {
        return true;
    }
    return io::WriteFileAtomic(path, data, size).Ok();
}

}  // namespace

void RemoveStaleBlobs(const io::Path& root, const Vector<UniquePtr<Project::Source>>& sources);

Status Project::Mint(const MintOptions& options, MintDiagnostics* diagnostics, MintStats* stats)
{
    Impl& impl = *impl_;
    *stats = MintStats();
    if (!impl.loaded)
    {
        diagnostics->Error(project_file_.View(), 0, StringView("the project is not loaded"));
        return Status::Make(ErrorCode::Invalid, "the project is not loaded");
    }
    // A project with source errors (a missing id, a clash, a cycle) mints nothing: the blobs would carry ids that are
    // not settled.
    if (impl.load_errors != 0)
    {
        return Status::Make(ErrorCode::Invalid, "the project has errors; nothing was minted");
    }
    if (!impl.components_ready)
    {
        if (!RegisterLevelRegistry(&impl.components))
        {
            diagnostics->Error(project_file_.View(), 0, StringView("the components could not be registered"));
            return Status::Make(ErrorCode::Other, "the components could not be registered");
        }
        impl.components_ready = true;
    }
    if (io::CreateDirectories(output_root_).Failed())
    {
        diagnostics->Error(output_root_.View(), 0, StringView("the output folder cannot be made"));
        return Status::Make(ErrorCode::Io, "the output folder cannot be made");
    }
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        const io::Path parent = (output_root_ / impl.sources[i]->blob_rel.View()).Parent();
        if (io::CreateDirectories(parent).Failed())
        {
            diagnostics->Error(parent.View(), 0, StringView("the output folder cannot be made"));
            return Status::Make(ErrorCode::Io, "the output folder cannot be made");
        }
    }

    Vector<ManifestEntry> manifest;
    LoadManifest(output_root_ / StringView(kManifestName), &manifest);

    // A source removed by hand: whatever the manifest says depended on it cannot be minted again; say so by name.
    Vector<bool> blocked;
    blocked.Resize(impl.sources.Size());
    for (usize i = 0; i < blocked.Size(); ++i)
    {
        blocked[i] = false;
    }
    for (usize m = 0; m < manifest.Size(); ++m)
    {
        if (impl.FindId(manifest[m].id) >= 0)
        {
            continue;
        }
        for (usize k = 0; k < manifest.Size(); ++k)
        {
            bool uses = false;
            for (usize d = 0; d < manifest[k].deps.Size(); ++d)
            {
                uses = uses || manifest[k].deps[d] == manifest[m].id.Value();
            }
            const i64 dependent = impl.FindId(manifest[k].id);
            if (uses && dependent >= 0)
            {
                diagnostics->Error(manifest[k].rel.View(), 0,
                                   Fmt("it references %s, whose source was removed; remove the reference or restore "
                                       "the source",
                                       manifest[m].rel.CStr())
                                       .View());
                blocked[static_cast<usize>(dependent)] = true;
            }
        }
    }

    HashMap<u64, u32, HeapAllocator> manifest_index(HeapAllocator::Default());
    for (usize m = 0; m < manifest.Size(); ++m)
    {
        manifest_index.Insert(manifest[m].id.Value(), static_cast<u32>(m));
    }
    // What each source references, directly or through others, and how many sources still need each one's bytes.
    Vector<Vector<u32>> closures;
    Vector<u32> users;
    users.Resize(impl.sources.Size());
    for (usize i = 0; i < users.Size(); ++i)
    {
        users[i] = 0;
    }
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        Vector<u32> closure;
        AddClosure(impl, static_cast<u32>(i), &closure);
        Sort(Span<u32>(closure.Data(), closure.Size()));
        for (usize k = 0; k < closure.Size(); ++k)
        {
            users[closure[k]] += 1;
        }
        closures.Push(static_cast<Vector<u32>&&>(closure));
    }

    jobs::Scheduler::Config scheduler_config;
    scheduler_config.participants = options.workers;
    scheduler_config.pin = false;
    jobs::Scheduler scheduler(scheduler_config);

    ProjectMint context;
    context.impl = &impl;
    context.output_root = &output_root_;
    context.manifest = &manifest;
    context.manifest_index = &manifest_index;
    context.closures = &closures;
    context.target = config_.target;
    context.force = options.force;
    context.scheduler = &scheduler;

    u32 max_wave = 0;
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        max_wave = impl.sources[i]->wave > max_wave ? impl.sources[i]->wave : max_wave;
        impl.sources[i]->ok = false;
        impl.sources[i]->blob.Clear();
    }
    for (u32 wave = 0; wave <= max_wave; ++wave)
    {
        Vector<u32> parallel;
        for (usize i = 0; i < impl.sources.Size(); ++i)
        {
            Source& s = *impl.sources[i];
            if (s.wave != wave)
            {
                continue;
            }
            if (blocked[i])
            {
                Complain(s, 0, StringView("not minted: a source it references was removed"));
            }
            else if (s.kind == SourceKind::Texture)
            {
                MintOne(context, i);  // a block-compressed texture fans out over the workers itself
            }
            else
            {
                parallel.Push(static_cast<u32>(i));
            }
        }
        if (parallel.Size() != 0)
        {
            auto run = [&](usize begin, usize end)
            {
                for (usize k = begin; k < end; ++k)
                {
                    MintOne(context, parallel[k]);
                }
            };
            jobs::ParallelForBlocking(scheduler, parallel.Size(), 1, run);
        }
        // What this wave's sources needed is needed by one source less; a blob nobody else needs is let go (only its
        // hash is kept), so memory holds the blobs of the open references, not the whole project.
        for (usize i = 0; i < impl.sources.Size(); ++i)
        {
            if (impl.sources[i]->wave != wave)
            {
                continue;
            }
            for (usize k = 0; k < closures[i].Size(); ++k)
            {
                const u32 c = closures[i][k];
                users[c] -= 1;
                if (users[c] == 0)
                {
                    impl.sources[c]->blob = Vector<byte>();
                }
            }
            if (users[i] == 0)
            {
                impl.sources[i]->blob = Vector<byte>();
            }
        }
    }

    // Report in source order, whatever order the workers finished in.
    stats->sources = static_cast<u32>(impl.sources.Size());
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        const Source& s = *impl.sources[i];
        for (usize m = 0; m < s.messages.Size(); ++m)
        {
            diagnostics->Error(s.rel.View(), s.message_lines[m], s.messages[m].View());
        }
        if (!s.ok)
        {
            stats->failed += 1;
            continue;
        }
        stats->minted += s.minted ? 1 : 0;
        stats->unchanged += s.minted ? 0 : 1;
        stats->rewritten += s.rewritten ? 1 : 0;
    }

    // The table of what was minted, and the manifest.
    ResourceTable table;
    String<HeapAllocator> manifest_text = Str(StringView("asset_mint_manifest 1\n"));
    manifest_text.Append(Fmt("tool %u\n", static_cast<unsigned>(kMintToolVersion)).View());
    Vector<u32> by_id;
    for (usize i = 0; i < impl.sources.Size(); ++i)
    {
        if (impl.sources[i]->ok)
        {
            by_id.Push(static_cast<u32>(i));
        }
    }
    Sort(Span<u32>(by_id.Data(), by_id.Size()),
         [&](const u32& a, const u32& b) { return impl.sources[a]->id < impl.sources[b]->id; });
    for (usize k = 0; k < by_id.Size(); ++k)
    {
        const Source& s = *impl.sources[by_id[k]];
        if (table.Add(s.vpath.View(), s.id, InfoOf(s.kind).type).Failed())
        {
            diagnostics->Error(s.rel.View(), 0, StringView("its path cannot be entered in the table"));
            stats->failed += 1;
        }
        char id_text[17];
        FormatResourceId(s.id, id_text);
        manifest_text.Append(Fmt("S %s %s %s %s ", id_text, s.rel.CStr(), HashText16(s.input_hash).CStr(),
                                 HashText16(s.blob_hash).CStr())
                                 .View());
        if (s.deps.Size() == 0)
        {
            manifest_text.Append('-');
        }
        // The ids the blob references, in id order.
        Vector<u64> dep_ids;
        for (usize d = 0; d < s.deps.Size(); ++d)
        {
            dep_ids.Push(impl.sources[s.deps[d]]->id.Value());
        }
        Sort(Span<u64>(dep_ids.Data(), dep_ids.Size()));
        for (usize d = 0; d < dep_ids.Size(); ++d)
        {
            char dep_text[17];
            FormatResourceId(ResourceId::FromValue(dep_ids[d]), dep_text);
            manifest_text.Append(d == 0 ? StringView(dep_text, 16) : StringView(dep_text, 16));
            if (d + 1 < dep_ids.Size())
            {
                manifest_text.Append(',');
            }
        }
        manifest_text.Append('\n');
    }
    Vector<byte> names;
    if (table.WriteNamesBlob(impl.types, &names).Failed() ||
        !WriteIfDifferent(output_root_ / StringView(kTableName), names.Data(), names.Size()))
    {
        diagnostics->Error(StringView(kTableName), 0, StringView("the table cannot be written"));
        stats->failed += 1;
    }
    if (!WriteIfDifferent(output_root_ / StringView(kManifestName), manifest_text.CStr(), manifest_text.Length()))
    {
        diagnostics->Error(StringView(kManifestName), 0, StringView("the manifest cannot be written"));
        stats->failed += 1;
    }
    // A blob no source maps to any more (its source was removed, renamed or moved) is removed, once everything minted.
    if (stats->failed == 0)
    {
        RemoveStaleBlobs(output_root_, impl.sources);
    }
    return stats->failed == 0 ? Status::Success() : Status::Make(ErrorCode::Invalid, "some sources failed");
}

// ---------------------------------------------------------------------------------------------------------------------
// Pack, verify, list

namespace
{

struct OutputBlob
{
    OutputBlob() : rel(HeapAllocator::Default()) {}
    String<HeapAllocator> rel;
    Vector<byte> bytes;
    BlobHeaderInfo header;
    bool ok = false;
};

struct BlobWalk
{
    const io::Path* root;
    Vector<String<HeapAllocator>>* found;
};

io::WalkAction VisitBlob(void* user, const io::WalkEntry& entry)
{
    BlobWalk* walk = static_cast<BlobWalk*>(user);
    if (entry.type != io::FileType::Regular || !entry.name.EndsWith(StringView(".blob")))
    {
        return io::WalkAction::Continue;
    }
    const StringView full = entry.path.View();
    const StringView root = walk->root->View();
    if (full.StartsWith(root) && full.Length() > root.Length() + 1)
    {
        walk->found->Push(Str(full.Substring(root.Length() + 1)));
    }
    return io::WalkAction::Continue;
}

Status CollectBlobs(const io::Path& root, const TypeRegistry& types, bool check_hash, MintDiagnostics* diagnostics,
                    Vector<UniquePtr<OutputBlob>>* out)
{
    Vector<String<HeapAllocator>> files;
    BlobWalk walk{&root, &files};
    io::WalkOptions options;
    options.sorted = true;
    options.max_depth = 64;
    if (io::WalkDirectory(root, options, &VisitBlob, &walk).Failed())
    {
        diagnostics->Error(root.View(), 0, StringView("the folder cannot be read"));
        return Status::Make(ErrorCode::NotFound, "the folder cannot be read");
    }
    bool all_ok = true;
    for (usize i = 0; i < files.Size(); ++i)
    {
        UniquePtr<OutputBlob> blob = MakeUnique<OutputBlob>();
        blob->rel = Str(files[i].View());
        Vector<u8> bytes;
        if (!ReadFileText(root / files[i].View(), u64{1} << 31, &bytes))
        {
            diagnostics->Error(files[i].View(), 0, StringView("the file cannot be read"));
            all_ok = false;
            continue;
        }
        blob->bytes.Resize(bytes.Size());
        MemCopy(blob->bytes.Data(), bytes.Data(), bytes.Size());
        BlobReader reader;
        const Status opened = reader.Open(ByteSpan(blob->bytes.Data(), blob->bytes.Size()), types);
        if (opened.Failed())
        {
            diagnostics->Error(files[i].View(), 0,
                               Fmt("%s: %s", ErrorCodeName(opened.Code()), opened.Message()).View());
            all_ok = false;
            continue;
        }
        if (check_hash && reader.Verify().Failed())
        {
            diagnostics->Error(files[i].View(), 0, StringView("the content hash does not match the bytes"));
            all_ok = false;
            continue;
        }
        (void)PeekBlobHeader(ByteSpan(blob->bytes.Data(), blob->bytes.Size()), &blob->header);
        blob->ok = true;
        out->Push(static_cast<UniquePtr<OutputBlob>&&>(blob));
    }
    return all_ok ? Status::Success() : Status::Make(ErrorCode::Invalid, "some blobs are not valid");
}

const OutputBlob* FindBlob(const Vector<UniquePtr<OutputBlob>>& blobs, ResourceId id)
{
    for (usize i = 0; i < blobs.Size(); ++i)
    {
        if (blobs[i]->header.id == id)
        {
            return blobs[i].Get();
        }
    }
    return nullptr;
}

}  // namespace

void RemoveStaleBlobs(const io::Path& root, const Vector<UniquePtr<Project::Source>>& sources)
{
    Vector<String<HeapAllocator>> files;
    BlobWalk walk{&root, &files};
    io::WalkOptions options;
    options.sorted = true;
    options.max_depth = 64;
    if (io::WalkDirectory(root, options, &VisitBlob, &walk).Failed())
    {
        return;
    }
    for (usize i = 0; i < files.Size(); ++i)
    {
        bool wanted = false;
        for (usize k = 0; k < sources.Size() && !wanted; ++k)
        {
            wanted = sources[k]->blob_rel.View() == files[i].View();
        }
        if (!wanted)
        {
            (void)io::Remove(root / files[i].View());  // a failure leaves the file, which Verify names
        }
    }
}

Status PackDirectory(const io::Path& directory, const io::Path& output, u32 target, bool names,
                     MintDiagnostics* diagnostics)
{
    TypeRegistry types;
    UniquePtr<FilesystemBackend> source;
    Status status = FilesystemBackend::Create(directory, types, &source, target);
    ResourceTable table;
    if (status.Ok())
    {
        status = source->ReadTable(&table);
    }
    if (status.Failed())
    {
        diagnostics->Error(directory.View(), 0, Fmt("%s: %s", ErrorCodeName(status.Code()), status.Message()).View());
        return status;
    }
    PakWriterOptions options;
    options.target = target;
    options.include_names = names;
    PakWriter writer(types, options);
    Vector<Vector<byte>> blobs;  // the writer borrows them until Finish
    bool all_ok = true;
    for (usize i = 0; i < table.Size(); ++i)
    {
        const TableEntryView entry = table.At(i);
        String<HeapAllocator> path = Str(entry.path);
        UniquePtr<ResourceReader> reader;
        status = source->Open(entry.id, &reader);
        Vector<byte> bytes;
        if (status.Ok())
        {
            bytes.Resize(static_cast<usize>(reader->Size()));
            status = reader->ReadAt(0, bytes.Data(), bytes.Size());
        }
        if (status.Ok())
        {
            // Only a current-version blob whose hash holds goes into a pak.
            BlobReader checked;
            status = checked.Open(ByteSpan(bytes.Data(), bytes.Size()), types);
            if (status.Ok())
            {
                status = checked.Verify();
            }
        }
        if (status.Ok())
        {
            blobs.Push(static_cast<Vector<byte>&&>(bytes));
            status = writer.Add(entry.id, ByteSpan(blobs[blobs.Size() - 1].Data(), blobs[blobs.Size() - 1].Size()),
                                path.View());
        }
        if (status.Failed())
        {
            diagnostics->Error(path.View(), 0, Fmt("%s: %s", ErrorCodeName(status.Code()), status.Message()).View());
            all_ok = false;
        }
    }
    if (!all_ok)
    {
        return Status::Make(ErrorCode::Invalid, "some blobs cannot be packed");
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        diagnostics->Error(output.View(), 0, StringView("the output file cannot be created"));
        return Status::Make(ErrorCode::Io, "the output file cannot be created");
    }
    status = writer.Finish(*file);
    if (status.Ok() && file->Close().Failed())
    {
        status = Status::Make(ErrorCode::Io, "closing the pak failed");
    }
    if (status.Failed())
    {
        file->Close();
        io::Remove(output);  // never leave a partial pak behind
        diagnostics->Error(output.View(), 0, Fmt("%s: %s", ErrorCodeName(status.Code()), status.Message()).View());
        for (usize i = 0; i < writer.Problems().Size(); ++i)
        {
            const PakWriter::Problem& p = writer.Problems()[i];
            char blob[17];
            char dependency[17];
            FormatResourceId(p.blob, blob);
            FormatResourceId(p.dependency, dependency);
            diagnostics->Error(StringView(blob, 16), 0,
                               Fmt("%s: dependency %s", ErrorCodeName(p.code), dependency).View());
        }
        return status;
    }
    return Status::Success();
}

Status Project::Pack(MintDiagnostics* diagnostics)
{
    return PackDirectory(output_root_, output_root_ / config_.pak.View(), config_.target, true, diagnostics);
}

Status Project::Verify(MintDiagnostics* diagnostics)
{
    TypeRegistry types;
    Vector<UniquePtr<OutputBlob>> blobs;
    bool ok = CollectBlobs(output_root_, types, true, diagnostics, &blobs).Ok();
    // The closure: every dependency is a blob of the output, of the type the reference declared.
    for (usize i = 0; i < blobs.Size(); ++i)
    {
        BlobReader reader;
        if (reader.Open(ByteSpan(blobs[i]->bytes.Data(), blobs[i]->bytes.Size()), types).Failed())
        {
            continue;
        }
        for (usize d = 0; d < reader.DependencyCount(); ++d)
        {
            const Dependency dep = reader.DependencyAt(d);
            const OutputBlob* target = FindBlob(blobs, dep.id);
            char id_text[17];
            FormatResourceId(dep.id, id_text);
            if (target == nullptr)
            {
                diagnostics->Error(blobs[i]->rel.View(), 0,
                                   Fmt("it references %s, which is not in the output", id_text).View());
                ok = false;
            }
            else if (target->header.type != dep.type)
            {
                diagnostics->Error(blobs[i]->rel.View(), 0,
                                   Fmt("it references %s as another type than the blob is", id_text).View());
                ok = false;
            }
        }
    }
    for (usize i = 0; i < blobs.Size(); ++i)
    {
        for (usize k = i + 1; k < blobs.Size(); ++k)
        {
            if (blobs[i]->header.id == blobs[k]->header.id)
            {
                diagnostics->Error(blobs[k]->rel.View(), 0,
                                   Fmt("it has the same id as %s", blobs[i]->rel.CStr()).View());
                ok = false;
            }
        }
    }
    // The table: every entry names a blob of the output, of the same type.
    Vector<u8> table_bytes;
    if (ReadFileText(output_root_ / StringView(kTableName), u64{1} << 30, &table_bytes))
    {
        BlobReader reader;
        ResourceTable table;
        Status status = reader.Open(ByteSpan(table_bytes.Data(), table_bytes.Size()), types);
        if (status.Ok())
        {
            status = reader.Verify();
        }
        if (status.Ok())
        {
            status = table.ReadNamesBlob(reader);
        }
        if (status.Failed())
        {
            diagnostics->Error(StringView(kTableName), 0,
                               Fmt("%s: %s", ErrorCodeName(status.Code()), status.Message()).View());
            ok = false;
        }
        for (usize i = 0; status.Ok() && i < blobs.Size(); ++i)
        {
            Vector<StringView> paths;
            if (table.PathsOf(blobs[i]->header.id, &paths) == 0)
            {
                diagnostics->Error(blobs[i]->rel.View(), 0, StringView("it has no entry in resources.table"));
                ok = false;
            }
        }
        for (usize i = 0; status.Ok() && i < table.Size(); ++i)
        {
            const TableEntryView entry = table.At(i);
            const OutputBlob* blob = FindBlob(blobs, entry.id);
            if (blob == nullptr || blob->header.type != entry.type)
            {
                diagnostics->Error(StringView(kTableName), 0,
                                   Fmt("the entry %.*s names a blob that is not in the output or is of another type",
                                       static_cast<int>(entry.path.Length()), entry.path.Data())
                                       .View());
                ok = false;
            }
        }
    }
    // The pak, when there is one.
    const io::Path pak_path = output_root_ / config_.pak.View();
    if (io::Exists(pak_path))
    {
        io::FilePtr file;
        PakReader pak;
        Status status = io::File::Open(pak_path, io::OpenMode::Read, io::kOpenNone, &file).Failed()
                            ? Status::Make(ErrorCode::Io, "the pak cannot be opened")
                            : PakReader::Open(static_cast<io::FilePtr&&>(file), types, config_.target, &pak);
        if (status.Ok())
        {
            status = pak.Verify(types);
        }
        if (status.Failed())
        {
            diagnostics->Error(config_.pak.View(), 0,
                               Fmt("%s: %s", ErrorCodeName(status.Code()), status.Message()).View());
            ok = false;
        }
    }
    return ok ? Status::Success() : Status::Make(ErrorCode::Invalid, "the output has problems");
}

Status Project::List(bool skeletons, MintDiagnostics* diagnostics, String<HeapAllocator>* out)
{
    TypeRegistry types;
    Vector<UniquePtr<OutputBlob>> blobs;
    if (CollectBlobs(output_root_, types, false, diagnostics, &blobs).Failed())
    {
        return Status::Make(ErrorCode::Invalid, "some blobs are not valid");
    }
    ResourceTable table;
    Vector<u8> table_bytes;
    if (ReadFileText(output_root_ / StringView(kTableName), u64{1} << 30, &table_bytes))
    {
        BlobReader reader;
        if (reader.Open(ByteSpan(table_bytes.Data(), table_bytes.Size()), types).Ok())
        {
            const Status read = table.ReadNamesBlob(reader);
            if (read.Failed())
            {
                diagnostics->Error(StringView(kTableName), 0,
                                   Fmt("%s: %s", ErrorCodeName(read.Code()), read.Message()).View());
            }
        }
    }
    Sort(Span<UniquePtr<OutputBlob>>(blobs.Data(), blobs.Size()),
         [](const UniquePtr<OutputBlob>& a, const UniquePtr<OutputBlob>& b) { return a->header.id < b->header.id; });
    *out = Str(StringView());
    for (usize i = 0; i < blobs.Size(); ++i)
    {
        const OutputBlob& blob = *blobs[i];
        char id_text[17];
        FormatResourceId(blob.header.id, id_text);
        TypeInfo info;
        types.Find(blob.header.type, &info);
        if (!skeletons)
        {
            Vector<StringView> paths;
            const usize found = table.PathsOf(blob.header.id, &paths);
            out->Append(Fmt("%s  %-9s %10llu  %.*s\n", id_text, info.name,
                            static_cast<unsigned long long>(blob.bytes.Size()),
                            static_cast<int>(found == 0 ? blob.rel.Length() : paths[0].Length()),
                            found == 0 ? blob.rel.CStr() : paths[0].Data())
                            .View());
            continue;
        }
        if (blob.header.type != types::kSkeleton)
        {
            continue;
        }
        SkeletonView skeleton;
        if (skeleton.Open(ByteSpan(blob.bytes.Data(), blob.bytes.Size()), types).Failed())
        {
            continue;
        }
        out->Append(Fmt("%s  %u joints  hash %s\n", id_text, static_cast<unsigned>(skeleton.JointCount()),
                        HashText16(skeleton.Hash()).CStr())
                        .View());
        for (usize k = 0; k < blobs.Size(); ++k)
        {
            BlobReader user;
            if (user.Open(ByteSpan(blobs[k]->bytes.Data(), blobs[k]->bytes.Size()), types).Failed())
            {
                continue;
            }
            for (usize d = 0; d < user.DependencyCount(); ++d)
            {
                if (user.DependencyAt(d).id == blob.header.id)
                {
                    TypeInfo user_info;
                    types.Find(blobs[k]->header.type, &user_info);
                    out->Append(Fmt("    used by %s %s\n", user_info.name, blobs[k]->rel.CStr()).View());
                    break;
                }
            }
        }
    }
    return Status::Success();
}

}  // namespace engine_placeholder::asset
