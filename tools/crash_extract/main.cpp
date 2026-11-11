// crash_extract: pulls the minidump out of a crash envelope, and reads what a crash left behind.
//
//   crash_extract <envelope> [<output.dmp>]            the minidump inside the envelope, as a .dmp file
//   crash_extract --modules <dump-or-envelope>         the exception and every module: base, size, build id, file
//   (Phase 13 Day 156) crash_extract --report <envelope>                  the crash report the handler attached
//   (crash-report.txt) crash_extract --image <exe-or-dll>                 a Windows image's build id, PDB and the
//   symbol-store entries it is filed under crash_extract --resolve <store> <dump-or-envelope> [<report>]
//                                                      the report with its frames named from the PDBs in the store
//                                                      (Windows: DbgHelp); the report is the envelope's own unless one
//                                                      is given
//
// The crash handler (engine/foundation/debug/crash_handler.h) leaves one `.envelope` file per crash in its dump
// directory; the minidump inside it is what `lldb -c`, `minidump-stackwalk` and WinDbg open. Without an output path the
// dump is written next to the envelope with the extension `.dmp`. scripts/crash-report.ps1 and store-symbols.ps1 drive
// the other modes.
//
// Exit status: 0 done; 1 the envelope has no minidump (or no report); 2 the input is damaged or a file could not be
// read or written; 3 --resolve named no frame; 64 wrong arguments; 69 --resolve is not available on this platform. A
// dev tool: never linked into the engine or a game, and it reads nothing from the network (the symbol search path is
// the store alone).
#include "envelope.h"
#include "minidump.h"
#include "resolve.h"

#include "os/console.h"
#include "cstr/cstr.h"

#include "io/file.h"
#include "types.h"
#include "cstr/mem_ops.h"

namespace
{

namespace io = engine_placeholder::io;
using engine_placeholder::u8;
using engine_placeholder::Vector;

constexpr engine_placeholder::u64 kMaxInputBytes = engine_placeholder::u64{1024} * 1024 * 1024;

bool ReadWholeFile(const char* path, Vector<u8>* bytes)
{
    io::Path file;
    return io::Path::Parse(engine_placeholder::StringView(path), io::NativePathStyle(), &file) &&
           io::ReadFile(file, bytes, kMaxInputBytes).Ok();
}

// The minidump in a file that is either a minidump itself or an envelope holding one.
bool DumpOf(const Vector<u8>& bytes, const char** data, engine_placeholder::usize* size)
{
    const char* p = reinterpret_cast<const char*>(bytes.Data());
    if (bytes.Size() >= 4 && engine_placeholder::MemCompare(p, "MDMP", 4) == 0)
    {
        *data = p;
        *size = bytes.Size();
        return true;
    }
    crash_extract::Minidump dump;
    if (crash_extract::FindMinidump(p, bytes.Size(), &dump) != crash_extract::EnvelopeResult::Ok)
    {
        return false;
    }
    *data = dump.data;
    *size = dump.size;
    return true;
}

constexpr engine_placeholder::usize kMaxModules = 512;
crash_extract::DumpModule g_modules[kMaxModules];  // ~800 KiB: static, not on the stack

int ListModules(const char* path)
{
    Vector<u8> bytes;
    const char* dump = nullptr;
    engine_placeholder::usize size = 0;
    if (!ReadWholeFile(path, &bytes) || !DumpOf(bytes, &dump, &size))
    {
        engine_placeholder::os::PrintError("crash_extract: %s is not a minidump or an envelope holding one\n", path);
        return 2;
    }
    crash_extract::DumpException exception;
    engine_placeholder::usize count = 0;
    if (crash_extract::ReadDumpException(dump, size, &exception) != crash_extract::DumpResult::Ok ||
        crash_extract::ReadDumpModules(dump, size, g_modules, kMaxModules, &count) != crash_extract::DumpResult::Ok)
    {
        engine_placeholder::os::PrintError("crash_extract: %s is a damaged minidump\n", path);
        return 2;
    }
    if (exception.present)
    {
        engine_placeholder::os::Print("exception: thread %u, code 0x%08x, address 0x%016llx\n", exception.thread_id,
                                      exception.code, static_cast<unsigned long long>(exception.address));
    }
    for (engine_placeholder::usize i = 0; i < count && i < kMaxModules; ++i)
    {
        const crash_extract::DumpModule& m = g_modules[i];
        engine_placeholder::os::Print("module: 0x%016llx 0x%08x %s %s\n", static_cast<unsigned long long>(m.base),
                                      m.size, m.build_id[0] != '\0' ? m.build_id : "-", m.name);
    }
    return 0;
}

int PrintReport(const char* path)
{
    Vector<u8> bytes;
    if (!ReadWholeFile(path, &bytes))
    {
        engine_placeholder::os::PrintError("crash_extract: cannot read %s\n", path);
        return 2;
    }
    crash_extract::Attachment report;
    const crash_extract::EnvelopeResult result = crash_extract::FindAttachment(
        reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), "crash-report.txt", &report);
    if (result == crash_extract::EnvelopeResult::Malformed)
    {
        engine_placeholder::os::PrintError("crash_extract: %s is not a complete envelope\n", path);
        return 2;
    }
    if (result != crash_extract::EnvelopeResult::Ok)
    {
        engine_placeholder::os::PrintError("crash_extract: %s holds no crash report\n", path);
        return 1;
    }
    engine_placeholder::os::PrintBytes(report.data, report.size);
    return 0;
}

int DescribeImage(const char* path)
{
    Vector<u8> bytes;
    crash_extract::ImageInfo info;
    if (!ReadWholeFile(path, &bytes) ||
        !crash_extract::ReadImageInfo(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), &info))
    {
        engine_placeholder::os::PrintError(
            "crash_extract: %s is not a Windows image with a CodeView record (link with /DEBUG)\n", path);
        return 2;
    }
    char id[sizeof(info.build_id)];
    engine_placeholder::usize n = 0;
    for (; info.build_id[n] != '\0'; ++n)
    {
        id[n] = info.build_id[n] >= 'a' && info.build_id[n] <= 'f' ? static_cast<char>(info.build_id[n] - 32)
                                                                   : info.build_id[n];
    }
    id[n] = '\0';
    const char* pdb = crash_extract::BaseName(info.debug_file);
    engine_placeholder::os::Print("build id: %s\n", info.build_id);
    engine_placeholder::os::Print("debug file: %s\n", info.debug_file);
    engine_placeholder::os::Print("pdb store entry: %s/%s/%s\n", pdb, id, pdb);
    engine_placeholder::os::Print("image store entry: %s/%08X%x/%s\n", crash_extract::BaseName(path), info.timestamp,
                                  info.size_of_image, crash_extract::BaseName(path));
    return 0;
}

int Resolve(const char* store, const char* dump_path, const char* report_path)
{
    Vector<u8> bytes;
    const char* dump = nullptr;
    engine_placeholder::usize size = 0;
    if (!ReadWholeFile(dump_path, &bytes) || !DumpOf(bytes, &dump, &size))
    {
        engine_placeholder::os::PrintError("crash_extract: %s is not a minidump or an envelope holding one\n",
                                           dump_path);
        return 2;
    }
    engine_placeholder::usize count = 0;
    if (crash_extract::ReadDumpModules(dump, size, g_modules, kMaxModules, &count) != crash_extract::DumpResult::Ok)
    {
        engine_placeholder::os::PrintError("crash_extract: %s is a damaged minidump\n", dump_path);
        return 2;
    }
    Vector<u8> report_bytes;
    const char* report = nullptr;
    engine_placeholder::usize report_size = 0;
    if (report_path != nullptr)
    {
        if (!ReadWholeFile(report_path, &report_bytes))
        {
            engine_placeholder::os::PrintError("crash_extract: cannot read %s\n", report_path);
            return 2;
        }
        report = reinterpret_cast<const char*>(report_bytes.Data());
        report_size = report_bytes.Size();
    }
    else
    {
        crash_extract::Attachment attached;
        if (crash_extract::FindAttachment(reinterpret_cast<const char*>(bytes.Data()), bytes.Size(), "crash-report.txt",
                                          &attached) != crash_extract::EnvelopeResult::Ok)
        {
            engine_placeholder::os::PrintError("crash_extract: %s holds no crash report; name the report file\n",
                                               dump_path);
            return 1;
        }
        report = attached.data;
        report_size = attached.size;
    }
    const int named = crash_extract::ResolveReport(report, report_size, g_modules,
                                                   count < kMaxModules ? count : kMaxModules, store);
    if (named < 0)
    {
        engine_placeholder::os::PrintError(
            "crash_extract: --resolve needs DbgHelp (Windows); on Linux use scripts/crash-report.sh\n");
        return 69;
    }
    if (named == 0)
    {
        engine_placeholder::os::PrintError(
            "crash_extract: no frame could be named from %s (store the program's symbols with scripts/store-symbols.ps1)\n",
            store);
        return 3;
    }
    return 0;
}

int Extract(const char* envelope_path, const char* output_path)
{
    char default_output[4096];
    if (output_path == nullptr)
    {
        // Only an input named *.envelope gets a derived name; anything else would end up as foo.dmp.dmp or overwrite
        // the input.
        const engine_placeholder::usize length = engine_placeholder::CStrLength(envelope_path);
        const char* extension = ".envelope";
        const engine_placeholder::usize extension_length = engine_placeholder::CStrLength(extension);
        if (length <= extension_length ||
            engine_placeholder::CStrCompare(envelope_path + length - extension_length, extension) != 0)
        {
            engine_placeholder::os::PrintError("crash_extract: %s does not end in .envelope; name the output file\n",
                                               envelope_path);
            return 2;
        }
        const engine_placeholder::usize stem = length - extension_length;
        if (stem + 5 > sizeof(default_output))
        {
            engine_placeholder::os::PrintError("crash_extract: the path is too long\n");
            return 2;
        }
        engine_placeholder::MemCopy(default_output, envelope_path, stem);
        engine_placeholder::MemCopy(default_output + stem, ".dmp", 5);
        output_path = default_output;
    }
    if (engine_placeholder::CStrCompare(output_path, envelope_path) == 0)
    {
        engine_placeholder::os::PrintError("crash_extract: the output would overwrite the envelope\n");
        return 2;
    }

    Vector<u8> envelope;
    if (!ReadWholeFile(envelope_path, &envelope))
    {
        engine_placeholder::os::PrintError("crash_extract: cannot read %s\n", envelope_path);
        return 2;
    }
    const char* data = reinterpret_cast<const char*>(envelope.Data());
    const engine_placeholder::usize size = envelope.Size();
    crash_extract::Minidump dump;
    const crash_extract::EnvelopeResult result = crash_extract::FindMinidump(data, size, &dump);
    if (result == crash_extract::EnvelopeResult::Malformed)
    {
        engine_placeholder::os::PrintError("crash_extract: %s is not a complete envelope\n", envelope_path);
        return 2;
    }
    if (result == crash_extract::EnvelopeResult::NoMinidump)
    {
        engine_placeholder::os::PrintError("crash_extract: %s holds no minidump\n", envelope_path);
        return 1;
    }
    // Written atomically (a temporary file, then a rename), so the output either is complete or does not exist.
    io::Path output;
    const bool written = io::Path::Parse(engine_placeholder::StringView(output_path), io::NativePathStyle(), &output) &&
                         io::WriteFileAtomic(output, dump.data, dump.size).Ok();
    if (!written)
    {
        engine_placeholder::os::PrintError("crash_extract: cannot write %s\n", output_path);
        return 2;
    }
    engine_placeholder::os::Print("%s\n", output_path);
    return 0;
}

int Usage()
{
    engine_placeholder::os::PrintError("usage: crash_extract <envelope> [<output.dmp>]\n"
                                       "       crash_extract --modules <dump-or-envelope>\n"
                                       "       crash_extract --report <envelope>\n"
                                       "       crash_extract --image <exe-or-dll>\n"
                                       "       crash_extract --resolve <store> <dump-or-envelope> [<report>]\n");
    return 64;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return Usage();
    }
    const char* mode = argv[1];
    if (engine_placeholder::CStrCompare(mode, "--modules") == 0)
    {
        return argc == 3 ? ListModules(argv[2]) : Usage();
    }
    if (engine_placeholder::CStrCompare(mode, "--report") == 0)
    {
        return argc == 3 ? PrintReport(argv[2]) : Usage();
    }
    if (engine_placeholder::CStrCompare(mode, "--image") == 0)
    {
        return argc == 3 ? DescribeImage(argv[2]) : Usage();
    }
    if (engine_placeholder::CStrCompare(mode, "--resolve") == 0)
    {
        return argc == 4 || argc == 5 ? Resolve(argv[2], argv[3], argc == 5 ? argv[4] : nullptr) : Usage();
    }
    if (mode[0] == '-' && mode[1] == '-')
    {
        return Usage();
    }
    return argc <= 3 ? Extract(argv[1], argc == 3 ? argv[2] : nullptr) : Usage();
}
