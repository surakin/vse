// The minidump and image readers of tools/crash_extract (Phase 13 Day 156) and the report-line parsing the resolver
// uses: a minidump built here byte by byte is read back; cut or damaged copies are rejected without reading outside the
// buffer; names taken from a dump are refused as paths when they could leave the store.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "debug/crash_context.h"
#include "minidump.h"
#include "resolve.h"

#include "cstr/cstr.h"
#include "cstr/mem_ops.h"
#include "build_defs.h"

using namespace engine_placeholder;

namespace
{

void Put32(Vector<u8>& out, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        out.Push(static_cast<u8>(v >> (8 * i)));
    }
}
void Put64(Vector<u8>& out, u64 v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.Push(static_cast<u8>(v >> (8 * i)));
    }
}
void Set32(Vector<u8>& out, usize at, u32 v)
{
    for (int i = 0; i < 4; ++i)
    {
        out[at + static_cast<usize>(i)] = static_cast<u8>(v >> (8 * i));
    }
}

// A minidump with an exception stream and a module list of one module ("C:\dir\game.exe", an RSDS record naming
// "game.pdb").
Vector<u8> MakeDump()
{
    Vector<u8> d;
    Put32(d, 0x504d444d);  // "MDMP"
    Put32(d, 0xa793);      // version
    Put32(d, 2);           // streams
    Put32(d, 32);          // directory at 32
    for (int i = 0; i < 4; ++i)
    {
        Put32(d, 0);
    }
    // Directory (2 entries x 12 bytes) at 32: filled in below.
    const usize directory = d.Size();
    for (int i = 0; i < 6; ++i)
    {
        Put32(d, 0);
    }
    // Exception stream.
    const usize exception = d.Size();
    Put32(d, 4242);  // thread
    Put32(d, 0);     // alignment
    Put32(d, 0xC0000005);
    Put32(d, 0);
    Put64(d, 0);
    Put64(d, 0x7ff600001234ull);
    Put32(d, 0);
    Put32(d, 0);
    for (int i = 0; i < 15; ++i)
    {
        Put64(d, 0);
    }
    Put32(d, 0);
    Put32(d, 0);  // thread context location
    const u32 exception_size = static_cast<u32>(d.Size() - exception);
    // Module list: count, then one 108-byte module.
    const usize list = d.Size();
    Put32(d, 1);
    const usize module = d.Size();
    Put64(d, 0x7ff600000000ull);  // base
    Put32(d, 0x10000);            // size
    Put32(d, 0);                  // checksum
    Put32(d, 0x6AEF7A07);         // timestamp
    Put32(d, 0);                  // name RVA: below
    for (int i = 0; i < 13; ++i)
    {
        Put32(d, 0);  // version info
    }
    Put32(d, 0);  // CodeView size: below
    Put32(d, 0);  // CodeView RVA: below
    for (int i = 0; i < 6; ++i)
    {
        Put32(d, 0);  // misc record, reserved
    }
    const u32 list_size = static_cast<u32>(d.Size() - list);
    // The name: a MINIDUMP_STRING.
    const usize name = d.Size();
    const char text[] = "C:\\dir\\game.exe";
    Put32(d, static_cast<u32>((sizeof(text) - 1) * 2));
    for (usize i = 0; i + 1 < sizeof(text); ++i)
    {
        d.Push(static_cast<u8>(text[i]));
        d.Push(0);
    }
    // The CodeView record.
    const usize cv = d.Size();
    Put32(d, 0x53445352);  // "RSDS"
    Put32(d, 0x01234567);
    d.Push(0xab);
    d.Push(0x89);  // Data2 0x89ab
    d.Push(0xef);
    d.Push(0xcd);  // Data3 0xcdef
    for (u8 b : {u8{0x10}, u8{0x32}, u8{0x54}, u8{0x76}, u8{0x98}, u8{0xba}, u8{0xdc}, u8{0xfe}})
    {
        d.Push(b);
    }
    Put32(d, 3);  // age
    const char pdb[] = "D:\\build\\game.pdb";
    for (usize i = 0; i < sizeof(pdb); ++i)
    {
        d.Push(static_cast<u8>(pdb[i]));
    }
    const u32 cv_size = static_cast<u32>(d.Size() - cv);
    Set32(d, module + 20, static_cast<u32>(name));
    Set32(d, module + 76, cv_size);
    Set32(d, module + 80, static_cast<u32>(cv));
    Set32(d, directory + 0, 6);
    Set32(d, directory + 4, exception_size);
    Set32(d, directory + 8, static_cast<u32>(exception));
    Set32(d, directory + 12, 4);
    Set32(d, directory + 16, list_size);
    Set32(d, directory + 20, static_cast<u32>(list));
    return d;
}

const char* AsChars(const Vector<u8>& v)
{
    return reinterpret_cast<const char*>(v.Data());
}

// A dump as Breakpad writes it on an x86-64 Mac (Phase 21 Day 292): system info naming macOS (or `platform`) and
// `architecture`, a thread list padded to 8 bytes after its count with one thread, an exception stream, and two
// x86-64 contexts whose flags are `flags`; a module list, padded the same way, of one module at 0x100000000.
struct MacDump
{
    Vector<u8> bytes;
    usize thread_context = 0;     // where each context starts
    usize exception_context = 0;  //
};

MacDump MakeMacDump(u32 platform = 0x8101, u32 architecture = 9, u32 flags = 0x00100000)
{
    MacDump m;
    Vector<u8>& d = m.bytes;
    Put32(d, 0x504d444d);
    Put32(d, 0xa793);
    Put32(d, 4);   // streams
    Put32(d, 32);  // directory at 32
    for (int i = 0; i < 4; ++i)
    {
        Put32(d, 0);
    }
    const usize directory = d.Size();
    for (int i = 0; i < 12; ++i)
    {
        Put32(d, 0);
    }
    // System info (56 bytes): architecture (u16), level, revision, processors, product type, version, build, platform.
    const usize system = d.Size();
    Put32(d, architecture);
    Put32(d, 0);
    Put32(d, 15);
    Put32(d, 0);
    Put32(d, 0);
    Put32(d, platform);
    for (int i = 0; i < 8; ++i)
    {
        Put32(d, 0);
    }
    const u32 system_size = static_cast<u32>(d.Size() - system);
    // Thread list: count, 4 bytes of padding, one 48-byte thread whose context location is at +40.
    const usize threads = d.Size();
    Put32(d, 1);
    Put32(d, 0);
    const usize thread = d.Size();
    for (int i = 0; i < 12; ++i)
    {
        Put32(d, 0);
    }
    const u32 threads_size = static_cast<u32>(d.Size() - threads);
    // Exception stream (168 bytes): thread, alignment, the 152-byte record, the context's location at +160.
    const usize exception = d.Size();
    Put32(d, 0x103);
    Put32(d, 0);
    Put32(d, 1);  // EXC_BAD_ACCESS
    for (int i = 0; i < 37; ++i)
    {
        Put32(d, 0);
    }
    Put32(d, 0);
    Put32(d, 0);
    const u32 exception_size = static_cast<u32>(d.Size() - exception);
    // Module list: count, padding, one module at 0x100000000 (no name, no CodeView record).
    const usize modules = d.Size();
    Put32(d, 1);
    Put32(d, 0);
    Put64(d, 0x100000000ull);
    Put32(d, 0x4000);
    for (int i = 0; i < 24; ++i)
    {
        Put32(d, 0);
    }
    const u32 modules_size = static_cast<u32>(d.Size() - modules);
    // The two contexts (1232 bytes each, CONTEXT_AMD64's size), flags at +0x30, a marker in rip (+0xF8).
    for (int c = 0; c < 2; ++c)
    {
        const usize at = d.Size();
        for (int i = 0; i < 1232 / 4; ++i)
        {
            Put32(d, 0);
        }
        Set32(d, at + 0x30, flags);
        Set32(d, at + 0xF8, 0x1234 + static_cast<u32>(c));
        (c == 0 ? m.thread_context : m.exception_context) = at;
    }
    Set32(d, thread + 40, 1232);
    Set32(d, thread + 44, static_cast<u32>(m.thread_context));
    Set32(d, exception + 160, 1232);
    Set32(d, exception + 164, static_cast<u32>(m.exception_context));
    const u32 entries[4][3] = {{7, system_size, static_cast<u32>(system)},
                               {3, threads_size, static_cast<u32>(threads)},
                               {6, exception_size, static_cast<u32>(exception)},
                               {4, modules_size, static_cast<u32>(modules)}};
    for (usize e = 0; e < 4; ++e)
    {
        for (usize k = 0; k < 3; ++k)
        {
            Set32(d, directory + e * 12 + k * 4, entries[e][k]);
        }
    }
    return m;
}

u32 Get32(const Vector<u8>& d, usize at)
{
    return u32{d[at]} | (u32{d[at + 1]} << 8) | (u32{d[at + 2]} << 16) | (u32{d[at + 3]} << 24);
}

Vector<u8> Copy(const Vector<u8>& from, usize length)
{
    Vector<u8> out;
    for (usize i = 0; i < length; ++i)
    {
        out.Push(from[i]);
    }
    return out;
}

}  // namespace

TEST_CASE("A minidump's exception and modules are read with the build id the crash context prints",
          "[crash-extract][debug]")
{
    const Vector<u8> dump = MakeDump();
    crash_extract::DumpException exception;
    REQUIRE(crash_extract::ReadDumpException(AsChars(dump), dump.Size(), &exception) == crash_extract::DumpResult::Ok);
    CHECK(exception.present);
    CHECK(exception.thread_id == 4242);
    CHECK(exception.code == 0xC0000005u);
    CHECK(exception.address == 0x7ff600001234ull);
    crash_extract::DumpModule modules[2];
    usize count = 0;
    REQUIRE(crash_extract::ReadDumpModules(AsChars(dump), dump.Size(), modules, 2, &count) ==
            crash_extract::DumpResult::Ok);
    REQUIRE(count == 1);
    CHECK(modules[0].base == 0x7ff600000000ull);
    CHECK(modules[0].size == 0x10000u);
    CHECK(CStrCompare(modules[0].name, "C:\\dir\\game.exe") == 0);
    CHECK(CStrCompare(modules[0].build_id, "0123456789abcdef1032547698badcfe3") ==
          0);  // Data1..3 as numbers, Data4's bytes, the age
    CHECK(CStrCompare(modules[0].debug_file, "D:\\build\\game.pdb") == 0);
    char path[512];
    REQUIRE(crash_extract::StorePdbPath("S", modules[0], path, sizeof(path)));
    CHECK(CStrCompare(path, "S\\game.pdb\\0123456789ABCDEF1032547698BADCFE3\\game.pdb") == 0);
}

TEST_CASE("A minidump cut at every length, or with any byte changed, never reads outside it", "[crash-extract][debug]")
{
    const Vector<u8> dump = MakeDump();
    crash_extract::DumpModule modules[2];
    crash_extract::DumpException exception;
    usize count = 0;
    for (usize length = 0; length < dump.Size(); ++length)
    {
        Vector<u8> cut;
        for (usize i = 0; i < length; ++i)
        {
            cut.Push(dump[i]);
        }
        (void)crash_extract::ReadDumpModules(AsChars(cut), cut.Size(), modules, 2, &count);
        (void)crash_extract::ReadDumpException(AsChars(cut), cut.Size(), &exception);
    }
    for (usize at = 0; at < dump.Size(); ++at)
    {
        Vector<u8> changed;
        for (usize i = 0; i < dump.Size(); ++i)
        {
            changed.Push(dump[i]);
        }
        changed[at] = static_cast<u8>(changed[at] ^ 0xff);
        if (crash_extract::ReadDumpModules(AsChars(changed), changed.Size(), modules, 2, &count) ==
                crash_extract::DumpResult::Ok &&
            count > 0)
        {
            CHECK(CStrLength(modules[0].name) < sizeof(modules[0].name));  // terminated, inside its buffer
        }
        (void)crash_extract::ReadDumpException(AsChars(changed), changed.Size(), &exception);
    }
    CHECK(crash_extract::ReadDumpModules(nullptr, 0, modules, 2, &count) == crash_extract::DumpResult::Malformed);
    SUCCEED();
}

TEST_CASE("A module list padded after its count, as Breakpad writes it on macOS, is read at the right offset",
          "[crash-extract][debug]")
{
    const MacDump m = MakeMacDump();
    crash_extract::DumpModule modules[2];
    usize count = 0;
    REQUIRE(crash_extract::ReadDumpModules(AsChars(m.bytes), m.bytes.Size(), modules, 2, &count) ==
            crash_extract::DumpResult::Ok);
    REQUIRE(count == 1);
    CHECK(modules[0].base == 0x100000000ull);  // read 4 bytes early, it was 0x0000000000000000 with size 1 (Day 292)
    CHECK(modules[0].size == 0x4000u);
}

TEST_CASE("The register flags of a macOS x86-64 dump are completed for lldb, and nothing else changes",
          "[crash-extract][debug]")
{
    MacDump m = MakeMacDump();
    const Vector<u8> before = Copy(m.bytes, m.bytes.Size());
    usize completed = 0;
    REQUIRE(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(m.bytes.Data()), m.bytes.Size(),
                                                   &completed) == crash_extract::DumpResult::Ok);
    CHECK(completed == 2);
    CHECK(Get32(m.bytes, m.thread_context + 0x30) == 0x00100007u);  // CONTROL | INTEGER | SEGMENTS
    CHECK(Get32(m.bytes, m.exception_context + 0x30) == 0x00100007u);
    usize changed = 0;
    for (usize i = 0; i < before.Size(); ++i)
    {
        changed += before[i] != m.bytes[i] ? 1 : 0;
    }
    CHECK(changed == 2);  // one byte of each flags word
    // A second pass finds nothing left to do.
    REQUIRE(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(m.bytes.Data()), m.bytes.Size(),
                                                   &completed) == crash_extract::DumpResult::Ok);
    CHECK(completed == 0);
}

TEST_CASE("Dumps from other systems or processors, and contexts that name their registers, are left alone",
          "[crash-extract][debug]")
{
    const u32 cases[4][3] = {{2, 9, 0x00100000},        // Windows NT
                             {0x8201, 9, 0x00100000},   // Linux
                             {0x8101, 12, 0x00100000},  // arm64
                             {0x8101, 9, 0x0010003f}};  // flags that already name the registers
    for (const auto& c : cases)
    {
        const MacDump m = MakeMacDump(c[0], c[1], c[2]);
        Vector<u8> copy = Copy(m.bytes, m.bytes.Size());
        usize completed = 0;
        REQUIRE(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(copy.Data()), copy.Size(), &completed) ==
                crash_extract::DumpResult::Ok);
        CHECK(completed == 0);
        CHECK(MemCompare(copy.Data(), m.bytes.Data(), copy.Size()) == 0);
    }
}

TEST_CASE("A macOS dump whose thread context lies past its end still has the exception's context completed",
          "[crash-extract][debug]")
{
    // A dump cut short, or written by a handler that stopped early: the thread's context points outside the file. The
    // contexts that are there are still completed, so lldb reads what it can (review finding, Day 292).
    MacDump m = MakeMacDump();
    const usize threads_rva = Get32(m.bytes, 32 + 12 + 8);  // the second directory entry: the thread list
    Set32(m.bytes, threads_rva + 8 + 44, static_cast<u32>(m.bytes.Size() + 4096));
    usize completed = 0;
    CHECK(crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(m.bytes.Data()), m.bytes.Size(), &completed) ==
          crash_extract::DumpResult::Ok);
    CHECK(completed == 1);
    CHECK(Get32(m.bytes, m.exception_context + 0x30) == 0x00100007u);
    CHECK(Get32(m.bytes, m.thread_context + 0x30) == 0x00100000u);
}

TEST_CASE("Completing a macOS dump cut at every length, or with any byte changed, never touches memory outside it",
          "[crash-extract][debug]")
{
    const MacDump m = MakeMacDump();
    usize completed = 0;
    for (usize length = 0; length < m.bytes.Size(); length += 7)
    {
        Vector<u8> cut = Copy(m.bytes, length);
        const crash_extract::DumpResult r = crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(cut.Data()),
                                                                                   cut.Size(), &completed);
        CHECK((r == crash_extract::DumpResult::Malformed || completed <= 2));
    }
    // Every byte of the header, the directory and the streams before the contexts (the contexts' own bytes change
    // only what is copied there).
    for (usize at = 0; at < m.thread_context; ++at)
    {
        Vector<u8> changed = Copy(m.bytes, m.bytes.Size());
        changed[at] = static_cast<u8>(changed[at] ^ 0xff);
        (void)crash_extract::CompleteMacContextFlags(reinterpret_cast<char*>(changed.Data()), changed.Size(),
                                                     &completed);
        CHECK(completed <= 2);
    }
    CHECK(crash_extract::CompleteMacContextFlags(nullptr, 0, &completed) == crash_extract::DumpResult::Malformed);
}

TEST_CASE("A name from a dump becomes a store path only if it is one plain file name", "[crash-extract][debug]")
{
    CHECK(crash_extract::SafeFileName("game.pdb"));
    CHECK_FALSE(crash_extract::SafeFileName(""));
    CHECK_FALSE(crash_extract::SafeFileName("."));
    CHECK_FALSE(crash_extract::SafeFileName(".."));
    CHECK_FALSE(crash_extract::SafeFileName("a\\b.pdb"));
    CHECK_FALSE(crash_extract::SafeFileName("C:x.pdb"));
    CHECK_FALSE(crash_extract::SafeFileName("x\x01.pdb"));
    crash_extract::DumpModule module;
    CStrCopy(module.debug_file, "C:\\x\\..\\..");
    CStrCopy(module.build_id, "abc1");
    char path[256];
    CHECK_FALSE(crash_extract::StorePdbPath("S", module, path, sizeof(path)));  // the last component is ".."
    CStrCopy(module.debug_file, "game.pdb");
    CStrCopy(module.build_id, "ab\\..1");
    CHECK_FALSE(crash_extract::StorePdbPath("S", module, path, sizeof(path)));  // a build id is hex only
}

TEST_CASE("Report frame lines are parsed into the module and the offset, other lines are not", "[crash-extract][debug]")
{
    unsigned number = 0;
    char module[256];
    u64 offset = 0;
    const char line[] = "#12  0x00007ff6b6a1328c  C:\\Git\\x+y\\debug_tests.exe+0xc328c";
    REQUIRE(crash_extract::ParseFrame(line, sizeof(line) - 1, &number, module, sizeof(module), &offset));
    CHECK(number == 12);
    CHECK(CStrCompare(module, "C:\\Git\\x+y\\debug_tests.exe") == 0);
    CHECK(offset == 0xc328cu);
    const char named[] = "#0  0x00007ff6b6a1328c  engine::Foo(int)  file.cpp:42";
    CHECK_FALSE(crash_extract::ParseFrame(named, sizeof(named) - 1, &number, module, sizeof(module), &offset));
    const char header[] = "--- stack trace ---";
    CHECK_FALSE(crash_extract::ParseFrame(header, sizeof(header) - 1, &number, module, sizeof(module), &offset));
    const char long_offset[] = "#1  0x1  m.exe+0x11112222333344445";
    CHECK_FALSE(
        crash_extract::ParseFrame(long_offset, sizeof(long_offset) - 1, &number, module, sizeof(module), &offset));
    crash_extract::DumpModule modules[1];
    CStrCopy(modules[0].name, "c:\\other\\DEBUG_TESTS.EXE");
    CHECK(crash_extract::FindModule("C:\\Git\\debug_tests.exe", modules, 1) == &modules[0]);
    CHECK(crash_extract::FindModule("C:\\Git\\other.exe", modules, 1) == nullptr);
}

#if ENGINE_PLATFORM_WINDOWS
#include "os/windows/win32.h"

#include "io/file.h"

TEST_CASE("Windows: the test program's own image names its PDB and the build id its crash context has",
          "[crash-extract][debug][windows]")
{
    wchar_t wide[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, wide, static_cast<DWORD>(sizeof(wide) / sizeof(wide[0])));
    REQUIRE(n > 0);
    char path[MAX_PATH * 4];
    REQUIRE(WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof(path), nullptr, nullptr) > 0);
    io::Path file;
    REQUIRE(io::Path::Parse(StringView(path), io::NativePathStyle(), &file));
    Vector<u8> bytes;
    REQUIRE(io::ReadFile(file, &bytes).Ok());
    crash_extract::ImageInfo info;
    REQUIRE(crash_extract::ReadImageInfo(AsChars(bytes), bytes.Size(), &info));
    CHECK(CStrLength(info.build_id) >= 33);
    CHECK(CStrFind(info.debug_file, ".pdb") != nullptr);
    CHECK(info.size_of_image > 0);
    debug::InitCrashContext(0, nullptr);
    CHECK(CStrCompare(info.build_id, debug::GetCrashContext().build_id) == 0);
    // Every cut of the image's first 4 KiB is refused or read without going outside it.
    for (usize length = 0; length < 4096 && length < bytes.Size(); length += 7)
    {
        (void)crash_extract::ReadImageInfo(AsChars(bytes), length, &info);
    }
}
#endif
