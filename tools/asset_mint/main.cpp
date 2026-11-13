// asset_mint: Phase 22 Day 316, the first cut (Day 327 completes the tool).
//
//   asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]
//
// packs the blobs of a directory (found by their own headers; the path is the file's location) into one pak for target
// N (default 0, the development target). It refuses an incomplete closure and lists each missing (blob, dependency)
// pair. The tool's options are its own command line, not engine settings.
#include "asset/filesystem_backend.h"
#include "asset/pak.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "os/console.h"

using namespace engine_placeholder;
using namespace engine_placeholder::asset;

namespace
{

int Usage()
{
    os::PrintError("usage: asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]\n");
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

}  // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && StringView(argv[1], CStrLength(argv[1])) == StringView("pack"))
    {
        return Pack(argc, argv);
    }
    return Usage();
}
