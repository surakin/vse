// asset_mint: Phase 22 Day 316, the first cut (Day 333 completes the tool).
//
//   asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]
//   asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]
//   asset_mint mint-hull SOURCE.hull.src OUTPUT.blob [--target=N]
//   asset_mint mint-audio SOURCE.audio.src OUTPUT.blob [--target=N]
//   asset_mint mint-script SOURCE.script.src OUTPUT.blob [--search=DIR] [--bytecode] [--no-run] [--dev-out=DIR]
//   [--target=N] asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N] asset_mint
//   mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N] asset_mint mint-texture SOURCE.tex.src
//   OUTPUT.blob [--target=N] asset_mint mint-material SOURCE.mat.src OUTPUT.blob [--target=N] asset_mint mint-level
//   SOURCE.level.src OUTPUT.blob [--root=DIR] [--target=N] asset_mint unmint-level LEVEL.blob OUTPUT.level.src
//   [--root=DIR]
//
// packs the blobs of a directory (found by their own headers; the path is the file's location) into one pak for target
// N (default 0, the development target). It refuses an incomplete closure and lists each missing (blob, dependency)
// pair. The tool's options are its own command line, not engine settings.
#include "asset/filesystem_backend.h"
#include "log/output.h"
#include "asset/pak.h"
#include "asset_import/gltf_import.h"
#include "cstr/number.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "io/file.h"
#include "jobs/scheduler.h"
#include "io/file_system.h"
#include "io/path.h"
#include "animation/components.h"
#include "scene/scene_components.h"
#include "asset/level.h"
#include "clip_source.h"
#include "level_source.h"
#include "material_source.h"
#include "mesh_source.h"
#include "project.h"
#include "audio_source.h"
#include "script_source.h"
#if ASSET_MINT_HAVE_LUAJIT
#include "script/luajit/luajit_backend.h"
#endif
#include "hull_source.h"
#include "mcol_source.h"
#include "skeleton_source.h"
#include "height_source.h"
#include "instance_source.h"
#include "instance_tree_source.h"
#include "texture_source.h"
#include "os/command_line.h"
#include "os/console.h"
#include "storage/builtin_components.h"

using namespace vse;
using namespace vse::asset;

namespace
{

int Usage()
{
    out::PrintError(
        "usage: asset_mint adopt|mint|pack|verify|list|check|remint PROJECT.project [--force] [--workers=N]\n"
        "                  [--quiet] [--verbose] [--skeletons] [--target=N] [--require-current]\n"
        "       asset_mint refs|delete|reid|unmint PROJECT.project ASSET [--with-dependents] [--update-references] [--out=FILE]\n"
        "       asset_mint rename|move|copy PROJECT.project ASSET NEWNAME|NEWFOLDER|NEWPATH\n"
        "       asset_mint remint DIRECTORY\n"
        "       asset_mint import-gltf FILE.gltf|FILE.glb --out=DIRECTORY [--manifest=FILE] [--prefix=/PATH] [--scale=X]\n"
        "                  [--clip-rate=HZ] [--flat-normals] [--drop-morph-targets] [--emit-level] [--loop=NAME]...\n"
        "       asset_mint pack DIRECTORY OUTPUT.pak [--target=N] [--no-names]\n"
        "       asset_mint mint-skeleton SOURCE.skel.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-hull SOURCE.hull.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-audio SOURCE.audio.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-mcol SOURCE.mcol.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-script SOURCE.script.src OUTPUT.blob [--search=DIR] [--bytecode] [--no-run] [--dev-out=DIR] [--target=N]\n"
        "       asset_mint mint-mesh SOURCE.mesh.src OUTPUT.blob [--skeleton=SKELETON.blob] [--target=N]\n"
        "       asset_mint mint-clip SOURCE.anim.src OUTPUT.blob --skeleton=SKELETON.blob [--target=N]\n"
        "       asset_mint mint-texture SOURCE.tex.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-heightmap SOURCE.hgt.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-instances SOURCE.inst.src OUTPUT.blob [--target=N]\n"
        "       asset_mint mint-instance-tree SOURCE.itree.src OUTPUT.blob [--target=N]\n"
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
                                                 {"out", true},
                                                 {"manifest", true},
                                                 {"prefix", true},
                                                 {"scale", true},
                                                 {"clip-rate", true},
                                                 {"flat-normals", false},
                                                 {"drop-morph-targets", false},
                                                 {"emit-level", false},
                                                 {"loop", true, true},
                                                 {"search", true},
                                                 {"bytecode", false},
                                                 {"no-run", false},
                                                 {"dev-out", true}};
constexpr usize kCliOptionCount = sizeof(kCliOptions) / sizeof(kCliOptions[0]);

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
        out::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
    }
    if (packed.Failed())
    {
        return 1;
    }
    out::Print("packed %s for target %u\n", in_path, static_cast<unsigned>(target));
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
        out::PrintError("asset_mint: the source file could not be opened\n");
        return 1;
    }
    Vector<u8> text;
    if (io::ReadAll(*in, &text, u64{1} << 24).Failed())
    {
        out::PrintError("asset_mint: the source file could not be read\n");
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
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a skeleton of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Mints a hull source into a blob (Phase 25 Day 393).
int MintHullCommand(const Line& line)
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
        out::PrintError("asset_mint: the source file could not be opened\n");
        return 1;
    }
    Vector<u8> text;
    if (io::ReadAll(*in, &text, u64{1} << 24).Failed())
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    Vector<byte> blob;
    HullSourceError error;
    const Status status = MintHull(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), types, &blob,
                                   &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a hull of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

// Mints an audio source and the WAV file it names into a blob (Phase 26 Day 418).
int MintAudioCommand(const Line& line)
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
    Vector<u8> text;
    if (io::File::Open(source_path, io::OpenMode::Read, io::kOpenNone, &in).Failed() ||
        io::ReadAll(*in, &text, u64{1} << 24).Failed())
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    AudioSource source;
    AudioSourceError error;
    if (ParseAudioSource(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), &source, &error).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::Path wav_relative;
    io::Path wav_path;
    if (!io::Path::Parse(source.wav.View(), io::NativePathStyle(), &wav_relative) ||
        !source_path.Parent().TryJoin(wav_relative, &wav_path))
    {
        out::PrintError("asset_mint: %s: the wav path is not valid\n", in_path);
        return 1;
    }
    io::FilePtr wav_file;
    Vector<u8> wav;
    if (io::File::Open(wav_path, io::OpenMode::Read, io::kOpenNone, &wav_file).Failed() ||
        io::ReadAll(*wav_file, &wav, u64{1} << 30).Failed())
    {
        out::PrintError("asset_mint: %s: the wav file could not be read\n", in_path);
        return 1;
    }
    TypeRegistry types;
    Vector<byte> blob;
    if (MintAudio(source, ByteSpan(reinterpret_cast<const byte*>(wav.Data()), wav.Size()), types, &blob, &error, target)
            .Failed())
    {
        out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted an audio clip of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

#if ASSET_MINT_HAVE_LUAJIT
// Reads a whole text file (up to 16 MiB).
bool ReadTextFile(const io::Path& path, Vector<u8>* text)
{
    io::FilePtr file;
    return !io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file).Failed() &&
           !io::ReadAll(*file, text, u64{1} << 24).Failed();
}

// Finds the script of module `name` under the search directory (`a.b` is `a/b.script.src`); its id must be `expected`.
struct ScriptSearch
{
    io::Path directory;
};

bool LoadScriptModule(void* context, StringView name, ResourceId expected, ScriptSource* out, ScriptSourceError* error)
{
    const ScriptSearch* search = static_cast<const ScriptSearch*>(context);
    String<HeapAllocator> relative_text(HeapAllocator::Default());
    for (usize i = 0; i < name.Length(); ++i)
    {
        relative_text.Append(name[i] == '.' ? '/' : name[i]);
    }
    relative_text.Append(StringView(".script.src"));
    io::Path relative;
    io::Path path;
    Vector<u8> text;
    if (!io::Path::Parse(relative_text.View(), io::NativePathStyle(), &relative) ||
        !search->directory.TryJoin(relative, &path) || !ReadTextFile(path, &text))
    {
        SetScriptError(error, "no such file under the search directory");
        return false;
    }
    if (ParseScriptSource(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), out, error).Failed())
    {
        return false;
    }
    if (out->id != expected)
    {
        SetScriptError(error, "the file's id is not the id the dep line gives");
        return false;
    }
    return true;
}
#endif

// Mints a script source and the Lua file it names into a blob (Phase 27 Day 462).
int MintScriptCommand(const Line& line)
{
#if ASSET_MINT_HAVE_LUAJIT
    static const char* const allowed[] = {"target", "search", "bytecode", "no-run", "dev-out"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 5) || !TargetOption(line, &target))
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
    ScriptSearch search;
    search.directory = source_path.Parent();
    StringView search_text;
    if (line.TryGetOption(StringView("search"), search_text) &&
        !io::Path::Parse(search_text, io::NativePathStyle(), &search.directory))
    {
        return Usage();
    }
    Vector<u8> text;
    if (!ReadTextFile(source_path, &text))
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    ScriptSource source;
    ScriptSourceError error;
    if (ParseScriptSource(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), &source, &error)
            .Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    StringView root_name(in_path, CStrLength(in_path));
    for (usize i = root_name.Length(); i > 0; --i)
    {
        if (root_name[i - 1] == '/' || root_name[i - 1] == '\\')
        {
            root_name = root_name.Substring(i, root_name.Length() - i);
            break;
        }
    }
    const usize dot = root_name.Find(StringView("."));
    root_name = root_name.Substring(0, dot < root_name.Length() ? dot : root_name.Length());
    if (CheckScriptGraph(root_name, source, &LoadScriptModule, &search, &error, nullptr).Failed())
    {
        out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        return 1;
    }
    io::Path script_relative;
    io::Path script_path;
    Vector<u8> lua;
    if (!io::Path::Parse(source.lua.View(), io::NativePathStyle(), &script_relative) ||
        !source_path.Parent().TryJoin(script_relative, &script_path) || !ReadTextFile(script_path, &lua))
    {
        out::PrintError("asset_mint: %s: the lua file could not be read\n", in_path);
        return 1;
    }
    script::luajit::LuaJitOptions backend_options;
    UniquePtr<script::Backend> backend = script::luajit::CreateLuaJitBackend(backend_options);
    ScriptMintOptions options;
    options.backend = backend.Get();
    options.bytecode = line.HasFlag("bytecode");
    options.check_entries = !line.HasFlag("no-run");
    TypeRegistry types;
    Vector<byte> blob;
    if (MintScript(source, StringView(reinterpret_cast<const char*>(lua.Data()), lua.Size()), types, options, &blob,
                   &error, target)
            .Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", source.lua.CStr(), static_cast<unsigned>(error.line),
                        error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    StringView dev_out;
    if (line.TryGetOption(StringView("dev-out"), dev_out))
    {
        // The stored text, as the hot-reload watcher reads it: <hex id>.lua in the directory.
        SharedBytesPtr shared = CopyShared(ByteSpan(blob.Data(), blob.Size()));
        ScriptView view;
        TypeRegistry view_types;
        char hex[17];
        FormatResourceId(source.id, hex);
        String<HeapAllocator> name(HeapAllocator::Default(), StringView(hex, 16));
        name.Append(StringView(".lua"));
        io::Path directory;
        io::Path relative;
        io::Path dev_file;
        if (view.Open(shared, view_types).Failed() || !io::Path::Parse(dev_out, io::NativePathStyle(), &directory) ||
            !io::Path::Parse(name.View(), io::NativePathStyle(), &relative) ||
            !directory.TryJoin(relative, &dev_file) || io::CreateDirectories(directory).Failed() ||
            io::WriteFileAtomic(dev_file, view.Source().Data(), view.Source().Size()).Failed())
        {
            out::PrintError("asset_mint: the --dev-out file could not be written\n");
            return 1;
        }
    }
    out::Print("minted a script of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
#else
    (void)line;
    out::PrintError("asset_mint: mint-script is not available in this build (no LuaJIT on this platform)\n");
    return 1;
#endif
}

int MintMcolCommand(const Line& line)
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
        out::PrintError("asset_mint: the source file could not be opened\n");
        return 1;
    }
    Vector<u8> text;
    if (io::ReadAll(*in, &text, u64{1} << 24).Failed())
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    Vector<byte> blob;
    McolSourceError error;
    const Status status = MintMeshCollider(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), types,
                                           &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a mesh collider of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    Vector<u8> skeleton_bytes;
    SkeletonView skeleton;
    if (skeleton_path != nullptr)
    {
        if (!ReadWholeFile(skeleton_path, u64{1} << 24, &skeleton_bytes))
        {
            out::PrintError("asset_mint: the skeleton blob could not be read\n");
            return 1;
        }
        const Status opened = skeleton.Open(ByteSpan(skeleton_bytes.Data(), skeleton_bytes.Size()), types);
        if (opened.Failed())
        {
            out::PrintError("asset_mint: the skeleton blob: %s: %s\n", ErrorCodeName(opened.Code()), opened.Message());
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
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a mesh of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    TextureSource source;
    TextureSourceError error;
    if (ParseTextureSource(text_view, &source, &error).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
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
            out::PrintError("asset_mint: %s:%u: the image path is not valid\n", in_path,
                            static_cast<unsigned>(source.sources[i].line));
            return 1;
        }
        Vector<u8> bytes;
        if (!ReadWholeFilePath(image_path, u64{1} << 31, &bytes))
        {
            out::PrintError("asset_mint: %s:%u: the image file could not be read\n", in_path,
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
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a texture of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

int MintHeightMapCommand(const Line& line)
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
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    HeightMapSource source;
    HeightMapSourceError error;
    if (ParseHeightMapSource(text_view, &source, &error).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::Path samples_path;
    io::Path relative;
    if (!io::Path::Parse(StringView(source.source.Data(), CStrLength(source.source.Data())), io::NativePathStyle(),
                         &relative) ||
        !source_path.Parent().TryJoin(relative, &samples_path))
    {
        out::PrintError("asset_mint: %s:%u: the sample path is not valid\n", in_path,
                        static_cast<unsigned>(source.source_line));
        return 1;
    }
    Vector<u8> samples;
    if (!ReadWholeFilePath(samples_path, u64{1} << 28, &samples))
    {
        out::PrintError("asset_mint: %s:%u: the sample file could not be read\n", in_path,
                        static_cast<unsigned>(source.source_line));
        return 1;
    }
    TypeRegistry types;
    Vector<byte> blob;
    if (MintHeightMap(text_view, ByteSpan(samples.Data(), samples.Size()), types, &blob, &error, target).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a height map of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

int MintInstancesCommand(const Line& line)
{
    static const char* const allowed[] = {"target"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 1) || !TargetOption(line, &target))
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
    if (!ReadWholeFile(in_path, u64{1} << 30, &text))
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    TypeRegistry types;
    Vector<byte> blob;
    InstanceSourceError error;
    if (MintInstances(text_view, types, &blob, &error, target).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted an instance set of %u bytes\n", static_cast<unsigned>(blob.Size()));
    return 0;
}

int MintInstanceTreeCommand(const Line& line)
{
    static const char* const allowed[] = {"target"};
    u32 target = 0;
    if (line.ArgCount() != 3 || !OnlyOptions(line, allowed, 1) || !TargetOption(line, &target))
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
    if (!ReadWholeFile(in_path, u64{1} << 30, &text))
    {
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    const StringView text_view(reinterpret_cast<const char*>(text.Data()), text.Size());
    TypeRegistry types;
    Vector<byte> blob;
    InstanceTreeSourceError error;
    if (MintInstanceTree(text_view, types, &blob, &error, target).Failed())
    {
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted an instance tree of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
        out::PrintError("asset_mint: the source file could not be read\n");
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
        out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line), error.message.Data());
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    out::Print("minted a material of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
        out::PrintError("asset_mint: the source or the skeleton blob could not be read\n");
        return 1;
    }
    TypeRegistry types;
    SkeletonView skeleton;
    const Status opened = skeleton.Open(ByteSpan(skeleton_bytes.Data(), skeleton_bytes.Size()), types);
    if (opened.Failed())
    {
        out::PrintError("asset_mint: the skeleton blob: %s: %s\n", ErrorCodeName(opened.Code()), opened.Message());
        return 1;
    }
    const ClipLimits
        limits;  // the defaults equal the `assets.anim_*` settings' defaults (Day 339 wires the tool to them)
    Vector<byte> blob;
    ClipSourceError error;
    const Status status = MintClip(StringView(reinterpret_cast<const char*>(text.Data()), text.Size()), skeleton, types,
                                   limits, &blob, &error, target);
    if (status.Failed())
    {
        if (error.line != 0)
        {
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        return 1;
    }
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return 1;
    }
    const io::Status written = file->Write(blob.Data(), blob.Size(), 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
        return 1;
    }
    ClipView minted;
    if (minted.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok())
    {
        out::Print("minted a clip of %u bytes (measured error: rotation %.4f degrees, translation %.2e of the range)\n",
                   static_cast<unsigned>(blob.Size()), static_cast<double>(minted.RotationError()),
                   static_cast<double>(minted.TranslationError()));
        return 0;
    }
    out::Print("minted a clip of %u bytes\n", static_cast<unsigned>(blob.Size()));
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

// The components a level may hold: the engine's own, the animation ones and the scene ones (an imported level's
// lights); a game adds its own where it builds its tools.
bool RegisterLevelRegistry(storage::ComponentRegistry* registry)
{
    return storage::RegisterBuiltinComponents(registry).Ok() && RegisterLevelComponents(registry).Ok() &&
           animation::RegisterAnimationComponents(registry).Ok() && scene::RegisterSceneComponents(registry).Ok();
}

bool WriteOutput(const io::Path& output, const void* data, usize size)
{
    io::FilePtr file;
    if (io::File::Open(output, io::OpenMode::Write, io::kOpenNone, &file).Failed())
    {
        out::PrintError("asset_mint: the output file could not be created\n");
        return false;
    }
    const io::Status written = file->Write(data, size, 0);
    if (written.Failed() || file->Close().Failed())
    {
        file->Close();
        io::Remove(output);
        out::PrintError("asset_mint: writing the output failed\n");
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
        out::PrintError("asset_mint: the source file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    storage::ComponentRegistry registry;
    if (!RegisterLevelRegistry(&registry))
    {
        out::PrintError("asset_mint: the components could not be registered\n");
        return 1;
    }
    ProjectResolver resolver(types);
    const Status opened = resolver.Open(root);
    if (opened.Failed())
    {
        out::PrintError("asset_mint: the project folder could not be read: %s\n", opened.Message());
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
            out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        }
        else
        {
            out::PrintError("asset_mint: %s:%u: %s\n", in_path, static_cast<unsigned>(error.line),
                            error.message.Data());
        }
        return 1;
    }
    if (!WriteOutput(output, blob.Data(), blob.Size()))
    {
        return 1;
    }
    out::Print("minted a level of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
        out::PrintError("asset_mint: the level file could not be read\n");
        return 1;
    }
    TypeRegistry types;
    storage::ComponentRegistry registry;
    if (!RegisterLevelRegistry(&registry))
    {
        out::PrintError("asset_mint: the components could not be registered\n");
        return 1;
    }
    ProjectResolver resolver(types);
    const Status opened = resolver.Open(root);
    if (opened.Failed())
    {
        out::PrintError("asset_mint: the project folder could not be read: %s\n", opened.Message());
        return 1;
    }
    String<HeapAllocator> text(HeapAllocator::Default());
    LevelSourceError error;
    if (FormatLevelBlob(ByteSpan(blob.Data(), blob.Size()), types, registry, resolver, &text, &error).Failed())
    {
        out::PrintError("asset_mint: %s: %s\n", in_path, error.message.Data());
        return 1;
    }
    if (!WriteOutput(output, text.View().Data(), text.Length()))
    {
        return 1;
    }
    out::Print("wrote the text of a level of %u bytes\n", static_cast<unsigned>(blob.Size()));
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
                out::PrintError("asset_mint: %s\n", before.Lines()[i].CStr());
            }
            return 1;
        }
        if (!quiet)
        {
            out::Print("adopted %u sources\n", static_cast<unsigned>(adopted));
        }
        if (verbose)
        {
            for (usize i = 0; i < before.Lines().Size(); ++i)
            {
                if (before.Lines()[i].View().StartsWith(StringView("note:")))
                {
                    out::PrintError("asset_mint: %s\n", before.Lines()[i].CStr());
                }
            }
        }
        status = project.Load(project_file, &diagnostics);
        for (usize i = 0; i < diagnostics.Lines().Size(); ++i)
        {
            out::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
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
                out::Print("%u sources: %u minted, %u unchanged, %u blob files written, %u failed\n",
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
                out::Print("%u sources minted again; the output is the current version\n",
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
                out::Print("packed the project for target %u\n",
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
                out::Print("the output verifies\n");
            }
        }
        else if (command == StringView("refs"))
        {
            status = project.Refs(line.Arg(2), &diagnostics, &listing);
            if (status.Ok() && !quiet)
            {
                out::Print("%s", listing.CStr());
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
                out::Print("%s", listing.CStr());
            }
        }
        else if (command == StringView("list"))
        {
            status = project.List(skeletons, &diagnostics, &listing);
            if (status.Ok() && !quiet)
            {
                out::Print("%s", listing.CStr());
            }
        }
        else if (command == StringView("check"))
        {
            status = project.Check(&diagnostics);
            if (status.Ok() && !quiet)
            {
                out::Print("the project of %u sources is consistent\n", static_cast<unsigned>(project.SourceCount()));
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
            out::PrintError("asset_mint: %s\n", text.CStr());
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
        out::PrintError("asset_mint: %s\n", diagnostics.Lines()[i].CStr());
    }
    out::Print("%u blobs: %u current, %u upgraded, %u failed\n", static_cast<unsigned>(stats.blobs),
               static_cast<unsigned>(stats.current), static_cast<unsigned>(stats.upgraded),
               static_cast<unsigned>(stats.failed));
    return status.Ok() ? 0 : 1;
}

// Reads the files a glTF names from the folder it is in.
class DirectoryGltfSource final : public asset::GltfSource
{
public:
    explicit DirectoryGltfSource(const io::Path& directory) : directory_(directory) {}
    asset::Status Read(StringView name, u64 max_bytes, Vector<byte>* out) override
    {
        io::Path relative;
        io::Path full;
        if (!io::Path::Parse(name, io::NativePathStyle(), &relative) || !directory_.TryJoin(relative, &full))
        {
            return asset::Status::Make(asset::ErrorCode::NotFound, "the file name is not a path");
        }
        Vector<u8> bytes;
        if (!ReadWholeFilePath(full, max_bytes, &bytes))
        {
            return asset::Status::Make(asset::ErrorCode::NotFound, "the file cannot be read");
        }
        *out = static_cast<Vector<byte>&&>(bytes);
        return asset::Status::Success();
    }

private:
    io::Path directory_;
};

bool ParseFloatOption(const Line& line, const char* name, f32* out)
{
    StringView text;
    if (!line.TryGetOption(StringView(name), text))
    {
        return true;
    }
    const ParseDoubleResult parsed = ParseDoubleExact(text.Data(), text.Data() + text.Length());
    if (!parsed.ok || parsed.end != text.Data() + text.Length())
    {
        return false;
    }
    *out = static_cast<f32>(parsed.value);
    return true;
}

// Imports a glTF into blobs: `asset_mint import-gltf FILE --out=DIRECTORY` (Day 338), a thin command over
// asset::ImportGltf.
int ImportGltfCommand(const Line& line)
{
    static const char* const allowed[] = {"out",          "manifest",           "prefix",     "scale", "clip-rate",
                                          "flat-normals", "drop-morph-targets", "emit-level", "loop"};
    StringView out_text;
    if (line.ArgCount() != 2 || !OnlyOptions(line, allowed, 9) || !line.TryGetOption(StringView("out"), out_text))
    {
        return Usage();
    }
    const char* in_path = line.Arg(1).Data();
    io::Path file_path;
    io::Path out_dir;
    if (!io::Path::Parse(StringView(in_path, CStrLength(in_path)), io::NativePathStyle(), &file_path) ||
        !io::Path::Parse(out_text, io::NativePathStyle(), &out_dir))
    {
        return Usage();
    }
    asset::ImportOptions options;
    if (!ParseFloatOption(line, "scale", &options.scale) || !ParseFloatOption(line, "clip-rate", &options.clip_rate) ||
        !(options.scale > 0.0f && options.scale < 1e9f) || !(options.clip_rate > 0.0f && options.clip_rate <= 1000.0f))
    {
        out::PrintError("asset_mint: --scale takes a number above 0 and --clip-rate one from above 0 to 1000\n");
        return 64;
    }
    StringView prefix;
    if (line.TryGetOption(StringView("prefix"), prefix))
    {
        options.path_prefix = prefix;
    }
    options.flat_normals = line.HasFlag("flat-normals");
    options.drop_morph_targets = line.HasFlag("drop-morph-targets");
    options.emit_level = line.HasFlag("emit-level");
    Vector<StringView> loops;
    for (usize i = 0; i < line.OptionCount(StringView("loop")); ++i)
    {
        loops.Push(line.OptionAt(StringView("loop"), i));
    }
    options.looping_clips = Span<const StringView>(loops.Data(), loops.Size());
    // The manifest of an earlier import keeps the ids: next to the glTF unless --manifest says where.
    StringView manifest_text;
    String<HeapAllocator> manifest_name(HeapAllocator::Default(), StringView(in_path, CStrLength(in_path)));
    manifest_name.Append(StringView(".import"));
    io::Path manifest_path;
    const bool have_manifest_option = line.TryGetOption(StringView("manifest"), manifest_text);
    if (!io::Path::Parse(have_manifest_option ? manifest_text : manifest_name.View(), io::NativePathStyle(),
                         &manifest_path))
    {
        return Usage();
    }
    asset::ImportManifest manifest;
    Vector<u8> manifest_bytes;
    if (io::Exists(manifest_path) && !ReadWholeFilePath(manifest_path, u64{1} << 26, &manifest_bytes))
    {
        out::PrintError("asset_mint: the manifest exists but cannot be read; it is not replaced\n");
        return 1;
    }
    if (manifest_bytes.Size() != 0)
    {
        asset::GltfError manifest_error;
        if (manifest
                .Parse(StringView(reinterpret_cast<const char*>(manifest_bytes.Data()), manifest_bytes.Size()),
                       &manifest_error)
                .Failed())
        {
            out::PrintError("asset_mint: %s: %s\n", manifest_name.CStr(), manifest_error.message.Data());
            return 1;
        }
        options.manifest = &manifest;
    }
    Vector<u8> file;
    if (!ReadWholeFilePath(file_path, options.limits.max_file_bytes, &file))
    {
        out::PrintError("asset_mint: the glTF file could not be read\n");
        return 1;
    }
    DirectoryGltfSource source(file_path.Parent());
    asset::ImportResult result;
    asset::GltfError error;
    const asset::Status status = asset::ImportGltf(ByteSpan(file.Data(), file.Size()), source, options, &result,
                                                   &error);
    for (usize i = 0; i < result.report.notes.Size(); ++i)
    {
        out::PrintError("asset_mint: %s: %s\n", in_path, result.report.notes[i].CStr());
    }
    if (status.Failed())
    {
        out::PrintError("asset_mint: %s: %s\n", in_path, asset::GltfMessage(error, status));
        return 1;
    }
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        const asset::ImportedResource& r = result.resources[i];
        String<HeapAllocator> relative(HeapAllocator::Default(), r.path.View().Substring(1));
        relative.Append(StringView(".blob"));
        io::Path blob_path;
        io::Path relative_path;
        if (!io::Path::Parse(relative.View(), io::NativePathStyle(), &relative_path) ||
            !out_dir.TryJoin(relative_path, &blob_path) || io::CreateDirectories(blob_path.Parent()).Failed() ||
            io::WriteFileAtomic(blob_path, r.blob.Data(), r.blob.Size()).Failed())
        {
            out::PrintError("asset_mint: %s could not be written\n", relative.CStr());
            return 1;
        }
    }
    String<HeapAllocator> text(HeapAllocator::Default());
    result.manifest.Format(&text);
    if (io::WriteFileAtomic(manifest_path, text.CStr(), text.Length()).Failed())
    {
        out::PrintError("asset_mint: the manifest could not be written\n");
        return 1;
    }
    out::Print("imported %u resources from %s (%u warnings)\n", static_cast<unsigned>(result.resources.Size()), in_path,
               static_cast<unsigned>(result.report.warnings));
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const Line line(HeapAllocator::Default(), argc, argv,
                    Span<const os::CommandLineOption>(kCliOptions, kCliOptionCount));
    if (!line.Ok())
    {
        out::PrintError("asset_mint: %s\n", line.Error().Data());
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
    if (command == StringView("import-gltf"))
    {
        return ImportGltfCommand(line);
    }
    if (command == StringView("mint-mesh"))
    {
        return MintMeshCommand(line);
    }
    if (command == StringView("mint-texture"))
    {
        return MintTextureCommand(line);
    }
    if (command == StringView("mint-instance-tree"))
    {
        return MintInstanceTreeCommand(line);
    }
    if (command == StringView("mint-instances"))
    {
        return MintInstancesCommand(line);
    }
    if (command == StringView("mint-heightmap"))
    {
        return MintHeightMapCommand(line);
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
    if (command == StringView("mint-mcol"))
    {
        return MintMcolCommand(line);
    }
    if (command == StringView("mint-hull"))
    {
        return MintHullCommand(line);
    }
    if (command == StringView("mint-script"))
    {
        return MintScriptCommand(line);
    }
    if (command == StringView("mint-audio"))
    {
        return MintAudioCommand(line);
    }
    return Usage();
}
