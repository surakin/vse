// asset_mint: Phase 22 Day 316, the first cut (Day 333 completes the tool).
//
//   asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]
//   asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]
//   asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N]
//   asset_mint mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N]
//   asset_mint mint-texture SOURCE.tex.src OUTPUT.blob [--target=N]
//   asset_mint mint-material SOURCE.mat.src OUTPUT.blob [--target=N]
//   asset_mint mint-level SOURCE.level.src OUTPUT.blob [--root=DIR] [--target=N]
//   asset_mint unmint-level LEVEL.blob OUTPUT.level.src [--root=DIR]
//
// packs the blobs of a directory (found by their own headers; the path is the file's location) into one pak for target
// N (default 0, the development target). It refuses an incomplete closure and lists each missing (blob, dependency)
// pair. The tool's options are its own command line, not engine settings.
#include "asset/filesystem_backend.h"
#include "asset/pak.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "io/file.h"
#include "jobs/scheduler.h"
#include "io/file_system.h"
#include "io/path.h"
#include "animation/components.h"
#include "asset/level.h"
#include "clip_source.h"
#include "level_source.h"
#include "material_source.h"
#include "mesh_source.h"
#include "project.h"
#include "skeleton_source.h"
#include "texture_source.h"
#include "os/command_line.h"
#include "os/console.h"
#include "storage/builtin_components.h"

using namespace engine_placeholder;
using namespace engine_placeholder::asset;

namespace
{

int Usage()
{
    os::PrintError(
        "usage: asset_mint adopt|mint|pack|verify|list|check|remint PROJECT.project [--force] [--workers=N]\n"
        "                  [--quiet] [--verbose] [--skeletons] [--target=N] [--require-current]\n"
        "       asset_mint refs|delete|reid|unmint PROJECT.project ASSET [--with-dependents] [--update-references] [--out=FILE]\n"
        "       asset_mint rename|move|copy PROJECT.project ASSET NEWNAME|NEWFOLDER|NEWPATH\n"
        "       asset_mint remint DIRECTORY\n"
        "       asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]\n"
        "       asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N]\n"
        "       asset_mint mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N]\n"
        "       asset_mint mint-texture SOURCE.tex.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-material SOURCE.mat.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-level SOURCE.level.src OUTPUT.blob [--root=DIR] [--target=N]\n"
        "       asset_mint unmint-level LEVEL.blob OUTPUT.level.src [--root=DIR]\n");
    return 64;
}

bool ParseNumber(const char* text, u32* out)
{
    if (*text == '\0')
    {
        return false;
    }
    u64 value = 0;
    for (; *text != '\0'; ++text)
    {
        if (*text < '0' || *text > '9' || value > 0xffffffffull)
        {
            return false;
        }
        value = value * 10 + static_cast<u64>(*text - '0');
    }
    if (value > 0xffffffffull)
    {
        return false;
    }
    *out = static_cast<u32>(value);
    return true;
}

using Line = os::CommandLine<HeapAllocator>;

// Every option any command takes; each command checks that the line holds only its own (OnlyOptions).
constexpr os::CommandLineOption kCliOptions[] = {{"target", true},
                                                 {"skeleton", true},
                                                 {"root", true},
                                                 {"no-names", false},
                                                 {"force", false},
                                                 {"workers", true},
                                                 {"quiet", false},
                                                 {"verbose", false},
                                                 {"skeletons", false},
                                                 {"with-dependents", false},
                                                 {"update-references", false},
                                                 {"require-current", false},
                                                 {"out", true}};

// True when the line holds no option outside `allowed`.
bool OnlyOptions(const Line& line, const char* const* allowed, usize count)
{
    for (usize i = 0; i < line.FlagCount(); ++i)
    {
        bool found = false;
        for (usize k = 0; k < count; ++k)
        {
            found = found || line.FlagName(i) == StringView(allowed[k]);
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

// --target N, or 0; false for something that is not a number.
bool TargetOption(const Line& line, u32* target)
{
    StringView text;
    *target = 0;
    return !line.TryGetOption(StringView("target"), text) || ParseNumber(text.Data(), target);
}

int Pack(const Line& line)
{
    static const char* const allowed[] = {"target", "no-names"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 2) || !TargetOption(line, &target))
    {
        return Usage();
    }
    const bool names = !line.HasFlag("no-names");
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path directory;
    io::Path output;
    if (!io::Path::Parse(StringView(in_path, CStrLength(in_path)), io::NativePathStyle(), &directory) ||
        !io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }

    MintDiagnostics diagnostics;
    const Status packed = PackDirectory(directory, output, target, names, &diagnostics);
    for (usize i = 0; i < diagnostics.Lines().Size(); ++i)
    {
        os::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
    }
    if (packed.Failed())
    {
        return 1;
    }
    os::Print("packed %s for target %u\n", in_path, static_cast<unsigned>(target));
    return 0;
}

// Mints a skeleton source into a blob (Day 318). `remint` of old blobs arrives with the whole tool on Day 333.
int MintSkeletonCommand(const Line& line)
{
    static const char* const allowed[] = {"target"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 1) || !TargetOption(line, &target))
    {
        return Usage();
    }
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(in_path, CStrLength(in_path)), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    io::FilePtr in;
    if (io::File::Open(source_path, io::OpenMode::Read, io::kOpenNone, &in).Failed())
    {
        os::PrintError("asset_mint: the source file could not be opened\n");
        return 1;
    }
    Vector<u8> text;
    if (io::ReadAll(*in, &text, u64{1} << 24).Failed())
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    Vector<byte> blob;
    SkeletonSourceError error;
    const Status status = MintSkeleton(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), types,
                                       &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    os::Print("minted a skeleton of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

bool ReadWholeFilePath(const io::Path& path, u64 max_bytes, Vector<u8>* out)
{
    io::FilePtr in;
    if (io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &in).Failed())
    {
        return false;
    }
    return io::ReadAll(*in, out, max_bytes).Ok();
}

bool ReadWholeFile(const char* path_text, u64 max_bytes, Vector<u8>* out)
{
    io::Path path;
    if (!io::Path::Parse(StringView(path_text, CStrLength(path_text)), io::NativePathStyle(), &path))
    {
        return false;
    }
    io::FilePtr in;
    if (io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &in).Failed())
    {
        return false;
    }
    return io::ReadAll(*in, out, max_bytes).Ok();
}

// Mints a mesh source into a blob (Day 319). A skinned mesh needs the skeleton's blob (`--skeleton=`).
int MintMeshCommand(const Line& line)
{
    static const char* const allowed[] = {"target", "skeleton"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 2) || !TargetOption(line, &target))
    {
        return Usage();
    }
    const char* skeleton_path = line.OptionOr("skeleton", nullptr);
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path output;
    if (!io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(in_path, u64{1} << 30, &text))
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    Vector<u8> skeleton_bytes;
    SkeletonView skeleton;
    if (skeleton_path != nullptr)
    {
        if (!ReadWholeFile(skeleton_path, u64{1} << 24, &skeleton_bytes))
        {
            os::PrintError("asset_mint: the skeleton blob could not be read\n");
            return 1;
        }
        const Status opened = skeleton.Open(ByteSpan(skeleton_bytes.Data(), skeleton_bytes.Size()), types);
        if (opened.Failed())
        {
            os::PrintError("asset_mint: the skeleton blob: %s: %s\n", ErrorCodeName(opened.Code()), opened.Message());
            return 1;
        }
    }
    Vector<byte> blob;
    MeshSourceError error;
    const Status status = MintMesh(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), types,
                                   skeleton_path != nullptr ? &skeleton : nullptr, &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    os::Print("minted a mesh of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Mints a texture source into a blob (Day 323). The raw image files are found next to the source file.
int MintTextureCommand(const Line& line)
{
    static const char* const allowed[] = {"target"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 1) || !TargetOption(line, &target))
    {
        return Usage();
    }
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(in_path, CStrLength(in_path)), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(in_path, u64{1} << 24, &text))
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    TextureSource source;
    TextureSourceError error;
    if (ParseTextureSource(text_view, &source, &error).Failed())
    {
        os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    const io::Path directory = source_path.Parent();
    Vector<Vector<u8>> files;
    Vector<ByteSpan> spans;
    files.Reserve(source.sources.Size());
    for (usize i = 0; i < source.sources.Size(); ++i)
    {
        io::Path image_path;
        io::Path relative;
        if (!io::Path::Parse(StringView(source.sources[i].name.Data(), CStrLength(source.sources[i].name.Data())),
                             io::NativePathStyle(), &relative) ||
            !directory.TryJoin(relative, &image_path))
        {
            os::PrintError("asset_mint: %s:%u: the image path is not valid\n", in_path,
                           static_cast<unsigned>(source.sources[i].line));
            return 1;
        }
        Vector<u8> bytes;
        if (!ReadWholeFilePath(image_path, u64{1} << 31, &bytes))
        {
            os::PrintError("asset_mint: %s:%u: the image file could not be read\n", in_path,
                           static_cast<unsigned>(source.sources[i].line));
            return 1;
        }
        files.Push(static_cast<Vector<u8>&&>(bytes));
    }
    for (usize i = 0; i < files.Size(); ++i)
    {
        spans.Push(ByteSpan(files[i].Data(), files[i].Size()));
    }
    TypeRegistry types;
    Vector<byte> blob;
    // The block compression of a bc texture fans out over the workers; other formats need no threads.
    jobs::Scheduler::Config scheduler_config;
    UniquePtr<jobs::Scheduler> scheduler;
    if (IsBcFormat(source.format))
    {
        scheduler = MakeUnique<jobs::Scheduler>(scheduler_config);
    }
    if (MintTexture(text_view, Span<const ByteSpan>(spans.Data(), spans.Size()), types, &blob, &error, target,
                    scheduler.Get())
            .Failed())
    {
        os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    os::Print("minted a texture of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Resolves a material's texture paths against the source file's folder: the file is a minted texture blob and its id is
// the blob's own.
class FolderResolver final : public MaterialReferenceResolver
{
public:
    FolderResolver(const io::Path& directory, const TypeRegistry& types) : directory_(directory), types_(types) {}

    Status Resolve(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        io::Path relative;
        io::Path full;
        if (!io::Path::Parse(path, io::NativePathStyle(), &relative) || !directory_.TryJoin(relative, &full))
        {
            return Status::Make(ErrorCode::Invalid, "the texture path is not valid");
        }
        Vector<u8> bytes;
        if (!ReadWholeFilePath(full, u64{1} << 31, &bytes))
        {
            return Status::Make(ErrorCode::NotFound, "the texture file could not be read");
        }
        BlobReader reader;
        if (reader.Open(ByteSpan(bytes.Data(), bytes.Size()), types_).Failed())
        {
            return Status::Make(ErrorCode::Invalid, "the texture file is not a blob");
        }
        *id = reader.Id();
        files_.Push(static_cast<Vector<u8>&&>(bytes));  // the inner buffer does not move when the outer grows
        const Vector<u8>& kept = files_[files_.Size() - 1];
        *blob = ByteSpan(kept.Data(), kept.Size());
        return Status::Success();
    }

private:
    io::Path directory_;
    const TypeRegistry& types_;
    mutable Vector<Vector<u8>> files_;
};

// Mints a material source into a blob (Day 326).
int MintMaterialCommand(const Line& line)
{
    static const char* const allowed[] = {"target"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 1) || !TargetOption(line, &target))
    {
        return Usage();
    }
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(in_path, CStrLength(in_path)), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(in_path, u64{1} << 24, &text))
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    FolderResolver resolver(source_path.Parent(), types);
    Vector<byte> blob;
    MaterialSourceError error;
    if (MintMaterial(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), resolver, types, &blob,
                     &error, target)
            .Failed())
    {
        os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    os::Print("minted a material of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Mints a clip source into a blob (Day 320). The clip's joints are named, so the skeleton's blob is required.
int MintClipCommand(const Line& line)
{
    static const char* const allowed[] = {"target", "skeleton"};
    u32 target = 0;
    const char* skeleton_path = line.OptionOr("skeleton", nullptr);
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 2) || !TargetOption(line, &target) ||
        skeleton_path == nullptr)
    {
        return Usage();
    }
    const char* in_path = line.Arg(1).Data();
    const char* out_path = line.Arg(2).Data();
    io::Path output;
    if (!io::Path::Parse(StringView(out_path, CStrLength(out_path)), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    Vector<u8> skeleton_bytes;
    if (!ReadWholeFile(in_path, u64{1} << 28, &text) || !ReadWholeFile(skeleton_path, u64{1} << 24, &skeleton_bytes))
    {
        os::PrintError("asset_mint: the source or the skeleton blob could not be read\n");
        return 1;
    }
    TypeRegistry types;
    SkeletonView skeleton;
    const Status opened = skeleton.Open(ByteSpan(skeleton_bytes.Data(), skeleton_bytes.Size()), types);
    if (opened.Failed())
    {
        os::PrintError("asset_mint: the skeleton blob: %s: %s\n", ErrorCodeName(opened.Code()), opened.Message());
        return 1;
    }
    const ClipLimits
        limits;  // the defaults equal the `assets.anim_*` settings' defaults (Day 338 wires the tool to them)
    Vector<byte> blob;
    ClipSourceError error;
    const Status status = MintClip(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), skeleton, types,
                                   limits, &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    ClipView minted;
    if (minted.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok())
    {
        os::Print("minted a clip of %u bytes (measured error: rotation %.4f degrees, translation %.2e of the range)\n",
                  static_cast<unsigned>(blob.Size()), static_cast<double>(minted.RotationError()),
                  static_cast<double>(minted.TranslationError()));
        return 0;
    }
    os::Print("minted a clip of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Resolves a level's references against a project folder: the blobs under it, found by their own headers, and the paths
// the folder's layout gives them (a `FilesystemBackend`'s table).
class ProjectResolver final : public LevelReferenceResolver
{
public:
    explicit ProjectResolver(const TypeRegistry& types) : types_(types) {}

    Status Open(const io::Path& root)
    {
        ENGINE_TRY(FilesystemBackend::Create(root, types_, &backend_));
        return backend_->ReadTable(&table_);
    }

    Status ResolvePath(StringView path, ResourceId* id, ByteSpan* blob) const override
    {
        if (!table_.Resolve(path, id))
        {
            return Status::Make(ErrorCode::NotFound, "the project has no such path");
        }
        return ResolveId(*id, blob);
    }

    Status ResolveId(ResourceId id, ByteSpan* blob) const override
    {
        for (usize i = 0; i < ids_.Size(); ++i)
        {
            if (ids_[i] == id)
            {
                *blob = ByteSpan(blobs_[i].Data(), blobs_[i].Size());
                return Status::Success();
            }
        }
        UniquePtr<ResourceReader> reader;
        ENGINE_TRY(backend_->Open(id, &reader));
        Vector<byte> bytes;
        bytes.Resize(static_cast<usize>(reader->Size()));
        ENGINE_TRY(reader->ReadAt(0, bytes.Data(), bytes.Size()));
        ids_.Push(id);
        blobs_.Push(static_cast<Vector<byte>&&>(bytes));
        *blob = ByteSpan(blobs_[blobs_.Size() - 1].Data(), blobs_[blobs_.Size() - 1].Size());
        return Status::Success();
    }

    Status PathOf(ResourceId id, String<HeapAllocator>* path) const override
    {
        Vector<StringView> paths;
        if (table_.PathsOf(id, &paths) == 0)
        {
            return Status::Make(ErrorCode::NotFound, "the project has no path for the id");
        }
        *path = String<HeapAllocator>(HeapAllocator::Default(), paths[0]);
        return Status::Success();
    }

private:
    const TypeRegistry& types_;
    UniquePtr<FilesystemBackend> backend_;
    ResourceTable table_;
    mutable Vector<ResourceId> ids_;
    mutable Vector<Vector<byte>> blobs_;  // the inner buffers do not move when the outer grows
};

// The components a level may hold: the engine's own and the animation ones; a game adds its own where it builds its
// tools.
bool RegisterLevelRegistry(storage::ComponentRegistry* registry)
{
    return storage::RegisterBuiltinComponents(registry).Ok() && RegisterLevelComponents(registry).Ok() &&
           animation::RegisterAnimationComponents(registry).Ok();
}

bool WriteOutput(const io::Path& output, const void* data, usize size)
{
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return false;
    }
    const io::Status written = file->Write(data, size, 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        os::PrintError("asset_mint: writing the output failed\n");
        return false;
    }
    return true;
}

// Reads `--root DIR` and `--target N`. The root defaults to the folder of the first path.
bool LevelOptions(const Line& line, io::Path* root, u32* target)
{
    static const char* const allowed[] = {"root", "target"};
    io::Path first;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 2) ||
        !io::Path::Parse(line.Arg(1), io::NativePathStyle(), &first))
    {
        return false;
    }
    *root = first.Parent();
    if (root->IsEmpty() && !io::Path::Parse(StringView("."), io::NativePathStyle(), root))  // a bare file name
    {
        return false;
    }
    if (target != nullptr && !TargetOption(line, target))
    {
        return false;
    }
    StringView root_text;
    return !line.TryGetOption(StringView("root"), root_text) || io::Path::Parse(root_text, io::NativePathStyle(), root);
}

// Mints a level source into a blob (Day 327).
int MintLevelCommand(const Line& line)
{
    io::Path root;
    u32 target = 0;
    io::Path output;
    const char* in_path = line.ArgCount() >= 2 ? line.Arg(1).Data() : "";  // used after LevelOptions checked the count
    if (line.ArgCount() != 3 || !LevelOptions(line, &root, &target) ||
        !io::Path::Parse(line.Arg(2), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(in_path, u64{1} << 26, &text))
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    storage::ComponentRegistry registry;
    if (!RegisterLevelRegistry(&registry))
    {
        os::PrintError("asset_mint: the components could not be registered\n");
        return 1;
    }
    ProjectResolver resolver(types);
    const Status opened = resolver.Open(root);
    if (opened.Failed())
    {
        os::PrintError("asset_mint: the project folder could not be read: %s\n", opened.Message());
        return 1;
    }
    Vector<byte> blob;
    LevelSourceError error;
    if (MintLevel(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), resolver, types, registry, &blob,
                  &error, target)
            .Failed())
    {
        if (error.line == 0)
        {
            os::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        }
        return 1;
    }
    if (!WriteOutput(output, blob.Data(), blob.Size()))
    {
        return 1;
    }
    os::Print("minted a level of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Prints a level blob back as its canonical text (Day 327).
int UnmintLevelCommand(const Line& line)
{
    io::Path root;
    io::Path output;
    const char* in_path = line.ArgCount() >= 2 ? line.Arg(1).Data() : "";  // used after LevelOptions checked the count
    if (line.ArgCount() != 3 || !LevelOptions(line, &root, nullptr) ||
        !io::Path::Parse(line.Arg(2), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> blob;
    if (!ReadWholeFile(in_path, u64{1} << 31, &blob))
    {
        os::PrintError("asset_mint: the level file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    storage::ComponentRegistry registry;
    if (!RegisterLevelRegistry(&registry))
    {
        os::PrintError("asset_mint: the components could not be registered\n");
        return 1;
    }
    ProjectResolver resolver(types);
    const Status opened = resolver.Open(root);
    if (opened.Failed())
    {
        os::PrintError("asset_mint: the project folder could not be read: %s\n", opened.Message());
        return 1;
    }
    String<HeapAllocator> text(HeapAllocator::Default());
    LevelSourceError error;
    if (FormatLevelBlob(ByteSpan(blob.Data(), blob.Size()), types, registry, resolver, &text, &error).Failed())
    {
        os::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        return 1;
    }
    if (!WriteOutput(output, text.View().Data(), text.Length()))
    {
        return 1;
    }
    os::Print("wrote the text of a level of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// The project commands (Day 333): `asset_mint COMMAND PROJECT.project [options]`.
// How many arguments after the project a command takes.
bool ExtraArguments(StringView command, usize* count)
{
    static const struct
    {
        const char* name;
        usize extra;
    } kCommands[] = {{"adopt", 0}, {"mint", 0},   {"pack", 0}, {"verify", 0}, {"list", 0}, {"check", 0}, {"remint", 0},
                     {"refs", 1},  {"rename", 2}, {"move", 2}, {"delete", 1}, {"copy", 2}, {"reid", 1},  {"unmint", 1}};
    for (const auto& c : kCommands)
    {
        if (command == StringView(c.name))
        {
            *count = c.extra;
            return true;
        }
    }
    return false;
}

// True when an option is given to a command that has no use for it.
bool OptionMisused(const Line& line, StringView command)
{
    static const struct
    {
        const char* option;
        const char* commands;  // the commands that take it, each between spaces
    } kUses[] = {{"force", " mint remint "},      {"workers", " mint remint "},
                 {"skeletons", " list "},         {"with-dependents", " delete "},
                 {"update-references", " reid "}, {"require-current", " verify "},
                 {"target", " pack verify "},     {"out", " unmint "}};
    String<HeapAllocator> padded(HeapAllocator::Default(), StringView(" "));
    padded.Append(command);
    padded.Append(StringView(" "));
    for (const auto& u : kUses)
    {
        if (line.HasFlag(u.option) && !StringView(u.commands).Contains(padded.View()))
        {
            return true;
        }
    }
    return false;
}

int ProjectCommand(const Line& line)
{
    const StringView command = line.Arg(0);
    static const char* const allowed[] = {
        "force",           "workers",           "quiet",           "verbose", "skeletons",
        "with-dependents", "update-references", "require-current", "target",  "out"};
    MintOptions options;
    StringView workers;
    StringView target_text;
    usize extra = 0;
    u32 target = 0;
    if (!ExtraArguments(command, &extra) || line.ArgCount() != 2 + extra || !OnlyOptions(line, allowed, 10) ||
        OptionMisused(line, command) ||
        (line.TryGetOption(StringView("workers"), workers) && !ParseNumber(workers.Data(), &options.workers)) ||
        (line.TryGetOption(StringView("target"), target_text) && !ParseNumber(target_text.Data(), &target)))
    {
        return Usage();
    }
    options.force = line.HasFlag("force");
    const bool quiet = line.HasFlag("quiet");
    const bool verbose = line.HasFlag("verbose");
    const bool skeletons = line.HasFlag("skeletons");
    io::Path project_file;
    if (!io::Path::Parse(line.Arg(1), io::NativePathStyle(), &project_file))
    {
        return Usage();
    }
    MintDiagnostics diagnostics;
    Project project;
    Status status;
    if (command == StringView("adopt"))
    {
        // Adopting is what a missing id asks for: load without complaining about those, adopt, then load for real.
        MintDiagnostics before;
        status = project.Load(project_file, &before);
        u32 adopted = 0;
        status = project.Adopt(&before, &adopted);
        if (status.Failed())
        {
            for (usize i = 0; i < before.Lines().Size(); ++i)
            {
                os::PrintError("asset_mint: %s\n", before.Lines()[i].CStr());
            }
            return 1;
        }
        if (!quiet)
        {
            os::Print("adopted %u sources\n", static_cast<unsigned>(adopted));
        }
        if (verbose)
        {
            for (usize i = 0; i < before.Lines().Size(); ++i)
            {
                if (before.Lines()[i].View().StartsWith(StringView("note:")))
                {
                    os::PrintError("asset_mint: %s\n", before.Lines()[i].CStr());
                }
            }
        }
        status = project.Load(project_file, &diagnostics);
        for (usize i = 0; i < diagnostics.Lines().Size(); ++i)
        {
            os::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
        }
        return status.Ok() ? 0 : 1;
    }
    status = project.Load(project_file, &diagnostics);
    MintStats stats;
    String<HeapAllocator> listing(HeapAllocator::Default());
    if (project.IsLoaded())
    {
        if (command == StringView("mint"))
        {
            status = project.Mint(options, &diagnostics, &stats);
            if (!quiet)
            {
                os::Print("%u sources: %u minted, %u unchanged, %u blob files written, %u failed\n",
                          static_cast<unsigned>(stats.sources), static_cast<unsigned>(stats.minted),
                          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.rewritten),
                          static_cast<unsigned>(stats.failed));
            }
        }
        else if (command == StringView("remint"))
        {
            // The sources are minted again (a blob of an older version is replaced by a fresh mint), then the output is
            // checked: nothing in it may be of an older version.
            options.force = true;
            status = project.Mint(options, &diagnostics, &stats);
            if (status.Ok())
            {
                VerifyOptions verify_options;
                verify_options.require_current = true;
                status = project.Verify(&diagnostics, verify_options);
            }
            if (status.Ok() && !quiet)
            {
                os::Print("%u sources minted again; the output is the current version\n",
                          static_cast<unsigned>(stats.sources));
            }
        }
        else if (command == StringView("pack"))
        {
            PackOptions pack_options;
            pack_options.has_target = line.HasFlag("target");
            pack_options.target = target;
            status = project.Pack(&diagnostics, pack_options);
            if (status.Ok() && !quiet)
            {
                os::Print("packed the project for target %u\n",
                          static_cast<unsigned>(pack_options.has_target ? target : project.Config().target));
            }
        }
        else if (command == StringView("verify"))
        {
            VerifyOptions verify_options;
            verify_options.require_current = line.HasFlag("require-current");
            verify_options.has_target = line.HasFlag("target");
            verify_options.target = target;
            status = project.Verify(&diagnostics, verify_options);
            if (status.Ok() && !quiet)
            {
                os::Print("the output verifies\n");
            }
        }
        else if (command == StringView("refs"))
        {
            status = project.Refs(line.Arg(2), &diagnostics, &listing);
            if (status.Ok() && !quiet)
            {
                os::Print("%s", listing.CStr());
            }
        }
        else if (command == StringView("rename"))
        {
            status = project.Rename(line.Arg(2), line.Arg(3), &diagnostics);
        }
        else if (command == StringView("move"))
        {
            status = project.Move(line.Arg(2), line.Arg(3), &diagnostics);
        }
        else if (command == StringView("delete"))
        {
            status = project.Delete(line.Arg(2), line.HasFlag("with-dependents"), &diagnostics);
        }
        else if (command == StringView("copy"))
        {
            status = project.Copy(line.Arg(2), line.Arg(3), &diagnostics);
        }
        else if (command == StringView("reid"))
        {
            status = project.Reid(line.Arg(2), line.HasFlag("update-references"), &diagnostics);
        }
        else if (command == StringView("unmint"))
        {
            status = project.Unmint(line.Arg(2), &diagnostics, &listing);
            StringView out_path;
            if (status.Ok() && line.TryGetOption(StringView("out"), out_path))
            {
                io::Path path;
                if (!io::Path::Parse(out_path, io::NativePathStyle(), &path) ||
                    io::WriteFileAtomic(path, listing.CStr(), listing.Length()).Failed())
                {
                    diagnostics.Error(out_path, 0, StringView("the output file cannot be written"));
                    status = Status::Make(ErrorCode::Io, "the output file cannot be written");
                }
            }
            else if (status.Ok() && !quiet)
            {
                os::Print("%s", listing.CStr());
            }
        }
        else if (command == StringView("list"))
        {
            status = project.List(skeletons, &diagnostics, &listing);
            if (status.Ok() && !quiet)
            {
                os::Print("%s", listing.CStr());
            }
        }
        else if (command == StringView("check"))
        {
            status = project.Check(&diagnostics);
            if (status.Ok() && !quiet)
            {
                os::Print("the project of %u sources is consistent\n", static_cast<unsigned>(project.SourceCount()));
            }
        }
    }
    for (usize i = 0; i < diagnostics.Lines().Size(); ++i)
    {
        const String<HeapAllocator>& text = diagnostics.Lines()[i];
        const bool is_error = text.View().StartsWith(StringView("error:"));
        const bool is_note = text.View().StartsWith(StringView("note:"));
        if (is_error || verbose ||
            (is_note && !quiet && extra != 0 && command != StringView("refs") && command != StringView("unmint")))
        {
            os::PrintError("asset_mint: %s\n", text.CStr());
        }
    }
    return status.Ok() && diagnostics.ErrorCount() == 0 ? 0 : 1;
}

// `asset_mint remint DIRECTORY`: the blobs of a folder, rewritten to the current version through the upgrade chains.
int RemintCommand(const Line& line)
{
    io::Path directory;
    if (!OnlyOptions(line, nullptr, 0) || !io::Path::Parse(line.Arg(1), io::NativePathStyle(), &directory))
    {
        return Usage();
    }
    TypeRegistry types;
    UpgradeChains chains;  // no built-in type has an older version yet: see upgrade.h
    MintDiagnostics diagnostics;
    RemintStats stats;
    const Status status = RemintDirectory(directory, types, chains, &diagnostics, &stats);
    for (usize i = 0; i < diagnostics.Lines().Size(); ++i)
    {
        os::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
    }
    os::Print("%u blobs: %u current, %u upgraded, %u failed\n", static_cast<unsigned>(stats.blobs),
              static_cast<unsigned>(stats.current), static_cast<unsigned>(stats.upgraded),
              static_cast<unsigned>(stats.failed));
    return status.Ok() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    const Line line(HeapAllocator::Default(), argc, argv, Span<const os::CommandLineOption>(kCliOptions, 13));
    if (!line.Ok())
    {
        os::PrintError("asset_mint: %s\n", line.Error().Data());
        return Usage();
    }
    if (line.ArgCount() < 1)
    {
        return Usage();
    }
    const StringView command = line.Arg(0);
    const bool has_project = line.ArgCount() >= 2 && line.Arg(1).EndsWith(StringView(".project"));
    const bool project_command = command == StringView("adopt") || command == StringView("mint") ||
                                 command == StringView("verify") || command == StringView("list") ||
                                 command == StringView("check") || command == StringView("refs") ||
                                 command == StringView("rename") || command == StringView("move") ||
                                 command == StringView("delete") || command == StringView("copy") ||
                                 command == StringView("reid") || command == StringView("unmint") ||
                                 ((command == StringView("pack") || command == StringView("remint")) && has_project);
    if (project_command)
    {
        return ProjectCommand(line);
    }
    if (command == StringView("remint") && line.ArgCount() == 2)
    {
        return RemintCommand(line);
    }
    if (command == StringView("pack"))
    {
        return Pack(line);
    }
    if (command == StringView("mint-mesh"))
    {
        return MintMeshCommand(line);
    }
    if (command == StringView("mint-texture"))
    {
        return MintTextureCommand(line);
    }
    if (command == StringView("mint-material"))
    {
        return MintMaterialCommand(line);
    }
    if (command == StringView("mint-level"))
    {
        return MintLevelCommand(line);
    }
    if (command == StringView("unmint-level"))
    {
        return UnmintLevelCommand(line);
    }
    if (command == StringView("mint-clip"))
    {
        return MintClipCommand(line);
    }
    if (command == StringView("mint-skeleton"))
    {
        return MintSkeletonCommand(line);
    }
    return Usage();
}
