// asset_mint: Phase 22 Day 316, the first cut (Day 332 completes the tool).
//
//   asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]
//   asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]
//   asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N]
//   asset_mint mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N]
//   asset_mint mint-texture SOURCE.tex.src OUTPUT.blob [--target=N]
//   asset_mint mint-material SOURCE.mat.src OUTPUT.blob [--target=N]
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
#include "clip_source.h"
#include "material_source.h"
#include "mesh_source.h"
#include "skeleton_source.h"
#include "texture_source.h"
#include "os/console.h"

using namespace engine_placeholder;
using namespace engine_placeholder::asset;

namespace
{

int Usage()
{
    os::PrintError("usage: asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]\n"
                   "       asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]\n"
                   "       asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N]\n"
                   "       asset_mint mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N]\n"
                   "       asset_mint mint-texture SOURCE.tex.src OUTPUT.blob [--target=N]\n"
                   "       asset_mint mint-material SOURCE.mat.src OUTPUT.blob [--target=N]\n");
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

bool StartsWith(const char* text, const char* prefix)
{
    return StringView(text, CStrLength(text)).StartsWith(StringView(prefix, CStrLength(prefix)));
}

int Pack(int argc, char** argv)
{
    if (argc < 4)
    {
        return Usage();
    }
    u32 target = 0;
    bool names = true;
    for (int i = 4; i < argc; ++i)
    {
        if (StartsWith(argv[i], "--target="))
        {
            if (!ParseNumber(argv[i] + 9, &target))
            {
                return Usage();
            }
        }
        else if (StringView(argv[i], CStrLength(argv[i])) == StringView("--no-names"))
        {
            names = false;
        }
        else
        {
            return Usage();
        }
    }
    io::Path directory;
    io::Path output;
    if (!io::Path::Parse(StringView(argv[2], CStrLength(argv[2])), io::NativePathStyle(), &directory) ||
        !io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
    {
        return Usage();
    }

    TypeRegistry types;
    UniquePtr<FilesystemBackend> source;
    Status status = FilesystemBackend::Create(directory, types, &source, target);
    if (status.Failed())
    {
        os::PrintError("asset_mint: %s: %s\n", ErrorCodeName(status.Code()), status.Message());
        return 1;
    }
    ResourceTable table;
    status = source->ReadTable(&table);
    if (status.Failed())
    {
        os::PrintError("asset_mint: %s: %s\n", ErrorCodeName(status.Code()), status.Message());
        return 1;
    }

    PakWriterOptions options;
    options.target = target;
    options.include_names = names;
    PakWriter writer(types, options);
    Vector<Vector<byte>> blobs;  // the writer borrows them until Finish
    for (usize i = 0; i < table.Size(); ++i)
    {
        const TableEntryView entry = table.At(i);
        UniquePtr<ResourceReader> reader;
        status = source->Open(entry.id, &reader);
        if (status.Failed())
        {
            os::PrintError("asset_mint: %.*s: %s: %s\n", static_cast<int>(entry.path.Length()), entry.path.Data(),
                           ErrorCodeName(status.Code()), status.Message());
            return 1;
        }
        blobs.Push(Vector<byte>());
        Vector<byte>& bytes = blobs[blobs.Size() - 1];
        bytes.Resize(static_cast<usize>(reader->Size()));
        status = reader->ReadAt(0, bytes.Data(), bytes.Size());
        if (status.Ok())
        {
            status = writer.Add(entry.id, ByteSpan(bytes.Data(), bytes.Size()), entry.path);
        }
        if (status.Failed())
        {
            os::PrintError("asset_mint: %.*s: %s: %s\n", static_cast<int>(entry.path.Length()), entry.path.Data(),
                           ErrorCodeName(status.Code()), status.Message());
            return 1;
        }
    }

    io::FilePtr file;
    const io::Status opened = io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file);
    if (opened.Failed())
    {
        os::PrintError("asset_mint: the output file could not be created\n");
        return 1;
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
        os::PrintError("asset_mint: %s: %s\n", ErrorCodeName(status.Code()), status.Message());
        for (usize i = 0; i < writer.Problems().Size(); ++i)
        {
            const PakWriter::Problem& p = writer.Problems()[i];
            char blob[17];
            char dependency[17];
            FormatResourceId(p.blob, blob);
            FormatResourceId(p.dependency, dependency);
            os::PrintError("  blob %s: %s: dependency %s\n", blob, ErrorCodeName(p.code), dependency);
        }
        return 1;
    }
    os::Print("packed %u resources for target %u\n", static_cast<unsigned>(table.Size()),
              static_cast<unsigned>(target));
    return 0;
}

// Mints a skeleton source into a blob (Day 318). `remint` of old blobs arrives with the whole tool on Day 332.
int MintSkeletonCommand(int argc, char** argv)
{
    if (argc < 4 || argc > 5)
    {
        return Usage();
    }
    u32 target = 0;
    if (argc == 5 && (!StartsWith(argv[4], "--target=") || !ParseNumber(argv[4] + 9, &target)))
    {
        return Usage();
    }
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(argv[2], CStrLength(argv[2])), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
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
            os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", argv[2], error.message.Data());
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
int MintMeshCommand(int argc, char** argv)
{
    if (argc < 4)
    {
        return Usage();
    }
    u32 target = 0;
    const char* skeleton_path = nullptr;
    for (int i = 4; i < argc; ++i)
    {
        if (StartsWith(argv[i], "--target="))
        {
            if (!ParseNumber(argv[i] + 9, &target))
            {
                return Usage();
            }
        }
        else if (StartsWith(argv[i], "--skeleton="))
        {
            skeleton_path = argv[i] + 11;
        }
        else
        {
            return Usage();
        }
    }
    io::Path output;
    if (!io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(argv[2], u64{1} << 30, &text))
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
            os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", argv[2], error.message.Data());
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
int MintTextureCommand(int argc, char** argv)
{
    if (argc < 4)
    {
        return Usage();
    }
    u32 target = 0;
    for (int i = 4; i < argc; ++i)
    {
        if (!StartsWith(argv[i], "--target=") || !ParseNumber(argv[i] + 9, &target))
        {
            return Usage();
        }
    }
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(argv[2], CStrLength(argv[2])), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(argv[2], u64{1} << 24, &text))
    {
        os::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    TextureSource source;
    TextureSourceError error;
    if (ParseTextureSource(text_view, &source, &error).Failed())
    {
        os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
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
            os::PrintError("asset_mint: %s:%u: the image path is not valid\n", argv[2],
                           static_cast<unsigned>(source.sources[i].line));
            return 1;
        }
        Vector<u8> bytes;
        if (!ReadWholeFilePath(image_path, u64{1} << 31, &bytes))
        {
            os::PrintError("asset_mint: %s:%u: the image file could not be read\n", argv[2],
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
        os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
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
int MintMaterialCommand(int argc, char** argv)
{
    if (argc < 4)
    {
        return Usage();
    }
    u32 target = 0;
    for (int i = 4; i < argc; ++i)
    {
        if (!StartsWith(argv[i], "--target=") || !ParseNumber(argv[i] + 9, &target))
        {
            return Usage();
        }
    }
    io::Path source_path;
    io::Path output;
    if (!io::Path::Parse(StringView(argv[2], CStrLength(argv[2])), io::NativePathStyle(), &source_path) ||
        !io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadWholeFile(argv[2], u64{1} << 24, &text))
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
        os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
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
int MintClipCommand(int argc, char** argv)
{
    if (argc < 5)
    {
        return Usage();
    }
    u32 target = 0;
    const char* skeleton_path = nullptr;
    for (int i = 4; i < argc; ++i)
    {
        if (StartsWith(argv[i], "--target="))
        {
            if (!ParseNumber(argv[i] + 9, &target))
            {
                return Usage();
            }
        }
        else if (StartsWith(argv[i], "--skeleton="))
        {
            skeleton_path = argv[i] + 11;
        }
        else
        {
            return Usage();
        }
    }
    if (skeleton_path == nullptr)
    {
        return Usage();
    }
    io::Path output;
    if (!io::Path::Parse(StringView(argv[3], CStrLength(argv[3])), io::NativePathStyle(), &output))
    {
        return Usage();
    }
    Vector<u8> text;
    Vector<u8> skeleton_bytes;
    if (!ReadWholeFile(argv[2], u64{1} << 28, &text) || !ReadWholeFile(skeleton_path, u64{1} << 24, &skeleton_bytes))
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
        limits;  // the defaults equal the `assets.anim_*` settings' defaults (Day 336 wires the tool to them)
    Vector<byte> blob;
    ClipSourceError error;
    const Status status = MintClip(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), skeleton, types,
                                   limits, &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            os::PrintError("asset_mint: %s:%u: %s\n", argv[2], static_cast<unsigned>(error.line), error.message.Data());
        }
        else
        {
            os::PrintError("asset_mint: %s: %s\n", argv[2], error.message.Data());
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

}  // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("pack"))
    {
        return Pack(argc, argv);
    }
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("mint-mesh"))
    {
        return MintMeshCommand(argc, argv);
    }
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("mint-texture"))
    {
        return MintTextureCommand(argc, argv);
    }
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("mint-material"))
    {
        return MintMaterialCommand(argc, argv);
    }
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("mint-clip"))
    {
        return MintClipCommand(argc, argv);
    }
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("mint-skeleton"))
    {
        return MintSkeletonCommand(argc, argv);
    }
    return Usage();
}
