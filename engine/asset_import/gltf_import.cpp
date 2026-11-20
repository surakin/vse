#include "asset_import/gltf_import.h"
#include "containers/hash.h"
#include "asset_import/gltf_level.h"

#include "asset_import/gltf_collision.h"
#include "asset_import/gltf_container.h"
#include "asset_import/gltf_clip.h"
#include "asset_import/gltf_material.h"
#include "asset_import/gltf_mesh.h"
#include "asset_import/gltf_names.h"
#include "asset_import/gltf_skin.h"
#include "algorithm.h"
#include "lang/limits.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace vse::asset
{

namespace
{

constexpr usize kMaxManifestEntries = 1u << 20;

u64 SplitMix(u64* state)
{
    *state += 0x9e3779b97f4a7c15ull;
    u64 z = *state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

bool ParseKind(StringView name, ImportKind* out)
{
    static const ImportKind kinds[] = {ImportKind::Image,    ImportKind::Texture,     ImportKind::Material,
                                       ImportKind::Skeleton, ImportKind::Mesh,        ImportKind::Clip,
                                       ImportKind::Level,    ImportKind::MeshCollider};
    for (const ImportKind kind : kinds)
    {
        if (name == StringView(ImportKindName(kind)))
        {
            *out = kind;
            return true;
        }
    }
    return false;
}

// Percent-escapes what would break a manifest line: blanks, control characters, `%` and non-ASCII bytes.
void EscapeName(StringView name, String<HeapAllocator>* out)
{
    static const char hex[] = "0123456789ABCDEF";
    for (usize i = 0; i < name.Length(); ++i)
    {
        const u8 c = static_cast<u8>(name[i]);
        if (c <= 0x20 || c >= 0x7f || c == '%')
        {
            out->Append('%');
            out->Append(hex[c >> 4]);
            out->Append(hex[c & 15]);
        }
        else
        {
            out->Append(static_cast<char>(c));
        }
    }
}

bool UnescapeName(StringView text, String<HeapAllocator>* out)
{
    for (usize i = 0; i < text.Length(); ++i)
    {
        if (text[i] != '%')
        {
            out->Append(text[i]);
            continue;
        }
        if (i + 2 >= text.Length())
        {
            return false;
        }
        u32 value = 0;
        for (usize k = 1; k <= 2; ++k)
        {
            const char c = text[i + k];
            u32 digit = 0;
            if (c >= '0' && c <= '9')
            {
                digit = static_cast<u32>(c - '0');
            }
            else if (c >= 'A' && c <= 'F')
            {
                digit = static_cast<u32>(c - 'A' + 10);
            }
            else if (c >= 'a' && c <= 'f')
            {
                digit = static_cast<u32>(c - 'a' + 10);
            }
            else
            {
                return false;
            }
            value = value * 16 + digit;
        }
        out->Append(static_cast<char>(value));
        i += 2;
    }
    return true;
}

}  // namespace

const char* ImportKindName(ImportKind kind)
{
    switch (kind)
    {
    case ImportKind::Image: return "image";
    case ImportKind::Texture: return "texture";
    case ImportKind::Material: return "material";
    case ImportKind::Skeleton: return "skeleton";
    case ImportKind::Mesh: return "mesh";
    case ImportKind::Clip: return "clip";
    case ImportKind::Level: return "level";
    case ImportKind::MeshCollider: return "mesh_collider";
    }
    return "unknown";
}

Status NoGltfSource::Read(StringView, u64, Vector<byte>* out)
{
    out->Clear();
    return Status::Make(ErrorCode::NotFound, "this import has no source for external files");
}

ResourceId RandomIdSource::Next()
{
    return GenerateResourceId();
}

ResourceId SeededIdSource::Next()
{
    u64 value = SplitMix(&state_);
    while (value == 0)
    {
        value = SplitMix(&state_);
    }
    return ResourceId::FromValue(value);
}

// ---------------------------------------------------------------------------------------------------------------------
// The manifest

namespace
{

u64 HashName(StringView name)
{
    return Fnv1a64Builder(kFnv1a64LegacyBasis).AddText(name).Value();
}

struct NameKey
{
    u64 hash;
    u32 position;  // an entry's index in the manifest, or an object's index in the argument
};

bool NameKeyLess(const NameKey& a, const NameKey& b)
{
    return a.hash != b.hash ? a.hash < b.hash : a.position < b.position;
}

// The first key of `keys` (sorted) whose hash is `hash`.
usize FirstOf(const Vector<NameKey>& keys, u64 hash)
{
    usize low = 0;
    usize high = keys.Size();
    while (low < high)
    {
        const usize mid = low + (high - low) / 2;
        if (keys[mid].hash < hash)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

}  // namespace

void ImportManifest::Match(ImportKind kind, Span<const ManifestObject> objects, Vector<ResourceId>* ids) const
{
    ids->Clear();
    for (usize i = 0; i < objects.Size(); ++i)
    {
        ids->Push(ResourceId());
    }
    Vector<NameKey> entry_names;   // the entries of this kind that have a name, by name
    Vector<NameKey> object_names;  // the objects that have a name, by name
    Vector<bool> taken;            // per entry: given to an object
    for (usize e = 0; e < entries_.Size(); ++e)
    {
        taken.Push(false);
        if (entries_[e].kind == kind && entries_[e].name.Length() != 0)
        {
            entry_names.Push(NameKey{HashName(entries_[e].name.View()), static_cast<u32>(e)});
        }
    }
    for (usize o = 0; o < objects.Size(); ++o)
    {
        if (objects[o].name.Length() != 0)
        {
            object_names.Push(NameKey{HashName(objects[o].name), static_cast<u32>(o)});
        }
    }
    Sort(Span<NameKey>(entry_names.Data(), entry_names.Size()), &NameKeyLess);
    Sort(Span<NameKey>(object_names.Data(), object_names.Size()), &NameKeyLess);
    // How many entries (or objects) of the set have exactly this name.
    auto count_of = [](const Vector<NameKey>& keys, StringView name, auto name_at, usize* first_position)
    {
        usize count = 0;
        for (usize k = FirstOf(keys, HashName(name)); k < keys.Size() && keys[k].hash == HashName(name); ++k)
        {
            if (name_at(keys[k].position) == name)
            {
                if (count == 0)
                {
                    *first_position = keys[k].position;
                }
                count += 1;
            }
        }
        return count;
    };
    auto entry_name = [this](u32 position) { return entries_[position].name.View(); };
    auto object_name = [&objects](u32 position) { return objects[position].name; };
    // By name: a name that exactly one entry and exactly one object have.
    for (usize o = 0; o < objects.Size(); ++o)
    {
        if (objects[o].name.Length() == 0)
        {
            continue;
        }
        usize entry_position = 0;
        usize object_position = 0;
        if (count_of(entry_names, objects[o].name, entry_name, &entry_position) == 1 &&
            count_of(object_names, objects[o].name, object_name, &object_position) == 1 && !taken[entry_position])
        {
            taken[entry_position] = true;
            (*ids)[o] = entries_[entry_position].id;
        }
    }
    // By index, for what is left: the free entry at the object's index, when its name is the object's or no object's
    // (the object was renamed), so that an entry that belongs to another object by name is never taken.
    Vector<NameKey> by_index;
    for (usize e = 0; e < entries_.Size(); ++e)
    {
        if (entries_[e].kind == kind && !taken[e])
        {
            by_index.Push(NameKey{entries_[e].index, static_cast<u32>(e)});
        }
    }
    Sort(Span<NameKey>(by_index.Data(), by_index.Size()), &NameKeyLess);
    for (usize o = 0; o < objects.Size(); ++o)
    {
        if (!(*ids)[o].IsNone())
        {
            continue;
        }
        for (usize k = FirstOf(by_index, objects[o].index); k < by_index.Size() && by_index[k].hash == objects[o].index;
             ++k)
        {
            const usize e = by_index[k].position;
            usize unused = 0;
            const bool name_is_an_objects = entries_[e].name.Length() != 0 &&
                                            count_of(object_names, entries_[e].name.View(), object_name, &unused) != 0;
            if (!taken[e] && (entries_[e].name.View() == objects[o].name || !name_is_an_objects))
            {
                taken[e] = true;
                (*ids)[o] = entries_[e].id;
                break;
            }
        }
    }
}

void ImportManifest::Add(ImportKind kind, u32 index, StringView name, ResourceId id)
{
    ImportManifestEntry entry;
    entry.kind = kind;
    entry.index = index;
    entry.name = String<HeapAllocator>(HeapAllocator::Default(), name);
    entry.id = id;
    entries_.Push(static_cast<ImportManifestEntry&&>(entry));
}

void ImportManifest::Format(String<HeapAllocator>* out) const
{
    *out = String<HeapAllocator>(HeapAllocator::Default(), StringView("gltf-import 1\n"));
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        const ImportManifestEntry& e = entries_[i];
        char id[17];
        FormatResourceId(e.id, id);
        char line[64];
        FormatTo(line, sizeof(line), "%s %u %s ", ImportKindName(e.kind), static_cast<unsigned>(e.index), id);
        out->Append(StringView(line, CStrLength(line)));
        EscapeName(e.name.View(), out);
        out->Append('\n');
    }
}

Status ImportManifest::Parse(StringView text, GltfError* error)
{
    entries_.Clear();
    usize pos = 0;
    u32 line_number = 0;
    bool header = false;
    Vector<NameKey> lines;  // (id, line number) of every entry
    while (pos < text.Length())
    {
        usize end = pos;
        while (end < text.Length() && text[end] != '\n')
        {
            ++end;
        }
        const StringView line = text.Substring(pos, end - pos);
        pos = end + 1;
        line_number += 1;
        char where[32];
        FormatTo(where, sizeof(where), "line %u", static_cast<unsigned>(line_number));
        if (line.Length() == 0)
        {
            continue;
        }
        if (!header)
        {
            if (line != StringView("gltf-import 1"))
            {
                error->Set(StringView(where), StringView("the manifest does not start with `gltf-import 1`"));
                return Status::Make(ErrorCode::Invalid, "bad manifest");
            }
            header = true;
            continue;
        }
        // kind index id name
        StringView fields[4];
        usize start = 0;
        usize count = 0;
        for (usize i = 0; i <= line.Length() && count < 4; ++i)
        {
            if (i == line.Length() || (line[i] == ' ' && count < 3))
            {
                fields[count++] = line.Substring(start, i - start);
                start = i + 1;
            }
        }
        ImportKind kind = ImportKind::Mesh;
        ResourceId id;
        u64 index = 0;
        // Up to ten digits: the keys of the maps and converted textures start at 0x40000000 (ten digits), and an index
        // is a u32.
        bool number = fields[1].Length() != 0 && fields[1].Length() <= 10;
        for (usize i = 0; number && i < fields[1].Length(); ++i)
        {
            number = fields[1][i] >= '0' && fields[1][i] <= '9';
            index = index * 10 + static_cast<u64>(fields[1][i] - '0');
        }
        number = number && index <= Limits<u32>::Max();
        String<HeapAllocator> name(HeapAllocator::Default());
        if (count < 3 || !ParseKind(fields[0], &kind) || !number || ParseResourceId(fields[2], &id).Failed() ||
            (count == 4 && !UnescapeName(fields[3], &name)))
        {
            error->Set(StringView(where), StringView("is not `kind index id name`"));
            return Status::Make(ErrorCode::Invalid, "bad manifest line");
        }
        if (entries_.Size() >= kMaxManifestEntries)
        {
            error->Set(StringView(where), StringView("the manifest has too many entries"));
            return Status::Make(ErrorCode::TooLarge, "the manifest is too long");
        }
        Add(kind, static_cast<u32>(index), name.View(), id);
        lines.Push(NameKey{id.Value(), line_number});
    }
    // An id may belong to one entry: sorted by id, a neighbour with the same id is a repeat (named by its line).
    Sort(Span<NameKey>(lines.Data(), lines.Size()), &NameKeyLess);
    for (usize i = 1; i < lines.Size(); ++i)
    {
        if (lines[i].hash == lines[i - 1].hash)
        {
            char where[32];
            FormatTo(where, sizeof(where), "line %u", static_cast<unsigned>(lines[i].position));
            error->Set(StringView(where), StringView("repeats an id"));
            return Status::Make(ErrorCode::Invalid, "a manifest id is repeated");
        }
    }
    if (!header)
    {
        error->Set(StringView(), StringView("the manifest is empty"));
        return Status::Make(ErrorCode::Invalid, "bad manifest");
    }
    return Status::Success();
}

// ---------------------------------------------------------------------------------------------------------------------
// The import

namespace
{

// One step of the import is done: counts it and stops when the import was cancelled.
Status Step(const ImportOptions& options, GltfError* error)
{
    if (options.steps_done != nullptr)
    {
        options.steps_done->FetchAdd(1, MemoryOrder::Release);
    }
    if (options.cancel != nullptr && options.cancel->Load(MemoryOrder::Acquire))
    {
        error->Set(StringView(), StringView("the import was cancelled"));
        return Status::Make(ErrorCode::Cancelled, "the import was cancelled");
    }
    return Status::Success();
}

}  // namespace

Status ImportGltf(ByteSpan file, GltfSource& source, const ImportOptions& options, ImportResult* result,
                  GltfError* error)
{
    *result = ImportResult();
    GltfDocument document;
    ENGINE_TRY(ParseGltf(file, source, options.limits, &document, error));
    ENGINE_TRY(Step(options, error));
    ImportReport& report = result->report;
    report.meshes = static_cast<u32>(document.root.Get("meshes").Size());
    report.materials = static_cast<u32>(document.root.Get("materials").Size());
    report.textures = static_cast<u32>(document.root.Get("textures").Size());
    report.images = static_cast<u32>(document.root.Get("images").Size());
    report.skins = static_cast<u32>(document.root.Get("skins").Size());
    report.animations = static_cast<u32>(document.root.Get("animations").Size());
    report.nodes = static_cast<u32>(document.root.Get("nodes").Size());
    report.accessors = static_cast<u32>(document.accessors.Size());
    const TypeRegistry types;
    GltfNodeGraph graph;
    ENGINE_TRY(ReadNodeGraph(document, &graph, error));
    // Collision (glTF colliders Day 3): read only when the import makes a level, the one thing that uses it.
    Vector<GltfNodeCollision> collisions;
    Vector<bool> collision_only;  // per glTF mesh: only collision-only nodes use it, so it gets no render mesh
    if (options.emit_level)
    {
        ENGINE_TRY(ReadCollisionMarks(document, graph, &report, &collisions, error));
        CollisionOnlyMeshes(graph, Span<const GltfNodeCollision>(collisions.Data(), collisions.Size()), report.meshes,
                            &collision_only);
    }
    // Textures and materials first (everything refers to them, nothing to later kinds), then skeletons (meshes and
    // clips refer to them), then the meshes skinned to them, then the clips.
    ENGINE_TRY(ImportMaterials(document, graph, source, options, types, result, error));
    ENGINE_TRY(Step(options, error));
    Vector<ImportedSkin> skins;
    ENGINE_TRY(ImportSkins(document, graph, options, types, result, &skins, error));
    ENGINE_TRY(Step(options, error));
    Vector<MeshSkeletonBinding> bindings;
    bindings.Resize(report.meshes);
    for (usize n = 0; n < graph.nodes.Size(); ++n)
    {
        const GltfNode& node = graph.nodes[n];
        if (node.mesh == kNoGltfIndex || node.skin == kNoGltfIndex)
        {
            continue;
        }
        MeshSkeletonBinding& binding = bindings[node.mesh];
        const ImportedSkin& skin = skins[node.skin];
        if (binding.present && binding.skeleton != skin.id)
        {
            error->Set(document.root.Get("meshes").At(node.mesh).Path(), StringView("is used with two skins"));
            return Status::Make(ErrorCode::Invalid, "a mesh has two skins");
        }
        binding.present = true;
        binding.skeleton = skin.id;
        binding.skeleton_hash = skin.hash;
        binding.joint_count = skin.joint_count;
        binding.joint_remap = Span<const u32>(skin.joint_of_slot.Data(), skin.joint_of_slot.Size());
    }
    ENGINE_TRY(ImportMeshes(document, options, types, Span<const MeshSkeletonBinding>(bindings.Data(), bindings.Size()),
                            result, error, Span<const bool>(collision_only.Data(), collision_only.Size())));
    ENGINE_TRY(Step(options, error));
    ENGINE_TRY(ImportClips(document, graph, options, types, Span<const ImportedSkin>(skins.Data(), skins.Size()),
                           result, error));
    if (options.emit_level)
    {
        ENGINE_TRY(ImportColliders(document, options, types, &collisions, result, error));
        ENGINE_TRY(Step(options, error));
        ENGINE_TRY(ImportLevel(document, graph, options, types, Span<const ImportedSkin>(skins.Data(), skins.Size()),
                               Span<const GltfNodeCollision>(collisions.Data(), collisions.Size()), result, error));
    }
    return Step(options, error);
}

Status ImportIntoBackend(HostMemoryBackend& backend, const ImportResult& result)
{
    Vector<ResourceId> added;
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        const ImportedResource& resource = result.resources[i];
        const StringView path = resource.path.View();
        MemoryAddOptions add;
        if (path.Length() != 0)
        {
            add.paths = Span<const StringView>(&path, 1);
        }
        const Status status = backend.Add(resource.id, ByteSpan(resource.blob.Data(), resource.blob.Size()), add);
        if (status.Failed())
        {
            bool undone = true;
            for (usize k = added.Size(); k > 0; --k)
            {
                undone = backend.Remove(added[k - 1], true).Ok() && undone;  // a failed import adds nothing
            }
            return undone ? status
                          : Status::Make(ErrorCode::Other,
                                         "an import failed and could not be taken back out of the backend");
        }
        added.Push(resource.id);
    }
    return Status::Success();
}

}  // namespace vse::asset
