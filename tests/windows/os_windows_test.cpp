// The Windows half of os/os.h and the UTF-16 boundary helpers (Phase 13 Day 149). The shared os tests
// (tests/foundation/os_timing_test.cpp, gpu_driver_loading_test.cpp, session_state_test.cpp, command_line_test.cpp) run
// unchanged; these cover what only Windows has.
#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include <fcntl.h>

#include "os/command_line.h"
#include "os/os.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

using namespace vse;
namespace win = vse::os::windows;

namespace
{

// UTF-8 of one code point, written independently of the code under test.
usize EncodeForTest(u32 cp, char* out)
{
    if (cp < 0x80)
    {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

// Runs windows_os_probe with `arguments` (a raw command-line tail, as CommandLineToArgvW will split it), capturing
// standard output. When `read_stderr` is false the probe's standard error is a pipe nobody reads (the "stopped reader"
// case).
struct ProbeResult
{
    DWORD exit_code = 0xFFFFFFFF;
    char out[8192] = {};
    char err[8192] = {};
};

void ReadAll(HANDLE h, char* buffer, usize capacity)
{
    usize used = 0;
    for (;;)
    {
        DWORD got = 0;
        if (used + 1 >= capacity ||
            !ReadFile(h, buffer + used, static_cast<DWORD>(capacity - 1 - used), &got, nullptr) || got == 0)
        {
            break;
        }
        used += got;
    }
    buffer[used] = '\0';
}

ProbeResult RunProbe(const wchar_t* arguments, bool read_stderr)
{
    ProbeResult result;
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
    HANDLE out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
    REQUIRE(CreatePipe(&out_read, &out_write, &inherit, 0));
    REQUIRE(CreatePipe(&err_read, &err_write, &inherit, 4096));  // small, so a reader that stops makes writes block
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    wchar_t command[2048];
    WFormatTo(command, 2048, L"\"%ls\" %ls", WINDOWS_OS_PROBE_PATH, arguments);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
    PROCESS_INFORMATION process = {};
    REQUIRE(CreateProcessW(nullptr, command, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                           &process));
    CloseHandle(out_write);
    CloseHandle(err_write);
    ReadAll(out_read, result.out, sizeof(result.out));
    if (read_stderr)
    {
        ReadAll(err_read, result.err, sizeof(result.err));
    }
    REQUIRE(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(out_read);
    CloseHandle(err_read);
    return result;
}

void Hex(const char* utf8, char* out)
{
    for (; *utf8 != '\0'; ++utf8, out += 2)
    {
        FormatTo(out, 3, "%02x", static_cast<unsigned>(static_cast<u8>(*utf8)));
    }
    *out = '\0';
}

}  // namespace

TEST_CASE("UTF-8 to UTF-16 and back round-trips every scalar value of every plane", "[windows][utf16]")
{
    char utf8[4];
    wchar_t wide[3];
    char back[5];
    usize mismatches = 0;
    for (u32 cp = 0; cp <= 0x10FFFF; ++cp)
    {
        if (cp >= 0xD800 && cp <= 0xDFFF)
        {
            continue;
        }
        const usize n = EncodeForTest(cp, utf8);
        usize bad = 99;
        const usize units = win::Utf8ToUtf16(StringView(utf8, n), wide, 3, &bad);
        const usize bytes = win::Utf16ToUtf8(wide, units, back, 5, nullptr);
        if (bad != 0 || units != (cp >= 0x10000 ? 2u : 1u) || bytes != n || MemCompare(back, utf8, n) != 0)
        {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

TEST_CASE("invalid UTF-8 becomes U+FFFD, counted, never dropped", "[windows][utf16]")
{
    struct Case
    {
        const char* bytes;
        usize length;
        usize replaced;
        const wchar_t* expect;
    };
    const Case cases[] = {
        {"a\x80z", 3, 1, L"a\xFFFDz"},        // a lone continuation byte
        {"a\xE2\x9C", 3, 1, L"a\xFFFD"},      // a truncated sequence at the end
        {"\xC0\x80", 2, 2, L"\xFFFD\xFFFD"},  // an overlong NUL: C0 is never valid, then a lone continuation
        {"\xED\xA0\x80", 3, 3,
         L"\xFFFD\xFFFD\xFFFD"},  // a surrogate: ED A0 is no valid prefix, so three maximal subparts
        {"\xF4\x90\x80\x80", 4, 4, L"\xFFFD\xFFFD\xFFFD\xFFFD"},  // above U+10FFFF: likewise one per byte
        {"\xE0\x80\x80", 3, 3, L"\xFFFD\xFFFD\xFFFD"},            // an overlong three-byte form
        {"\xE2\x9C"
         "A",
         3, 1,
         L"\xFFFD"
         L"A"},                             // a truncated sequence before ASCII: one subpart, the A survives
        {"\xE2\x9C\x93", 3, 0, L"\x2713"},  // valid, for contrast
    };
    for (const Case& c : cases)
    {
        wchar_t out[8];
        usize replaced = 0;
        const usize n = win::Utf8ToUtf16(StringView(c.bytes, c.length), out, 8, &replaced);
        CHECK(replaced == c.replaced);
        CHECK(n == WStrLength(c.expect));
        CHECK(WStrCompare(out, c.expect) == 0);
    }
}

TEST_CASE("unpaired UTF-16 surrogates from Windows become U+FFFD, counted", "[windows][utf16]")
{
    const wchar_t high_alone[] = {L'a', 0xD83D, L'b'};
    const wchar_t low_alone[] = {0xDE00, L'c'};
    const wchar_t high_at_end[] = {L'd', 0xD83D};
    char out[16];
    usize replaced = 0;
    CHECK(win::Utf16ToUtf8(high_alone, 3, out, 16, &replaced) == 5);
    CHECK(replaced == 1);
    CHECK(CStrCompare(out, "a\xEF\xBF\xBD"
                           "b") == 0);
    CHECK(win::Utf16ToUtf8(low_alone, 2, out, 16, &replaced) == 4);
    CHECK(replaced == 1);
    CHECK(win::Utf16ToUtf8(high_at_end, 2, out, 16, &replaced) == 4);
    CHECK(replaced == 1);
    CHECK(CStrCompare(out, "d\xEF\xBF\xBD") == 0);
}

TEST_CASE("UTF-16 conversion reports the full length and writes whole code points only", "[windows][utf16]")
{
    const char text[] = "ab\xF0\x9F\x98\x80"
                        "c";  // a, b, U+1F600 (two UTF-16 units), c
    CHECK(win::Utf8ToUtf16(StringView(text), nullptr, 0) == 5);
    wchar_t small[4];  // room for 3 units: "ab" fits, the pair does not
    CHECK(win::Utf8ToUtf16(StringView(text), small, 4) == 5);
    CHECK(WStrCompare(small, L"ab") == 0);
    wchar_t exact[6];
    CHECK(win::Utf8ToUtf16(StringView(text), exact, 6) == 5);
    CHECK(exact[5] == 0);
    char narrow[5];  // "ab" + 4-byte sequence does not fit before the terminator
    const wchar_t wide[] = {L'a', L'b', 0xD83D, 0xDE00};
    CHECK(win::Utf16ToUtf8(wide, 4, narrow, 5) == 6);
    CHECK(CStrCompare(narrow, "ab") == 0);
    CHECK(win::Utf8ToUtf16(StringView(""), exact, 6) == 0);
    CHECK(exact[0] == 0);
}

TEST_CASE("PathToWin32 converts separators and adds the long-path prefix only to long absolute paths",
          "[windows][utf16]")
{
    wchar_t out[1024];
    CHECK(win::PathToWin32(StringView("C:/Users/x/file.txt"), out, 1024) == 19);
    CHECK(WStrCompare(out, L"C:\\Users\\x\\file.txt") == 0);

    // The prefix starts at 248 UTF-16 units, CreateDirectoryW's limit without it (MAX_PATH - 12).
    char at_247[300] = "C:/";
    char at_248[300] = "C:/";
    for (int i = 0; i < 244; ++i)
    {
        CStrAppend(at_247, "d");
    }
    for (int i = 0; i < 245; ++i)
    {
        CStrAppend(at_248, "d");
    }
    CHECK(win::PathToWin32(StringView(at_247), out, 1024) == 247);
    CHECK(out[0] == L'C');
    CHECK(win::PathToWin32(StringView(at_248), out, 1024) == 4 + 248);
    CHECK(WStrCompareN(out, L"\\\\?\\C:\\", 7) == 0);

    char long_drive[400] = "C:/";
    char long_unc[400] = "//server/share/";
    char long_relative[400] = "dir/";
    for (int i = 0; i < 300; ++i)
    {
        CStrAppend(long_drive, "a");
        CStrAppend(long_unc, "b");
        CStrAppend(long_relative, "c");
    }
    REQUIRE(win::PathToWin32(StringView(long_drive), out, 1024) == 4 + 303);
    CHECK(WStrCompareN(out, L"\\\\?\\C:\\aaa", 10) == 0);
    REQUIRE(win::PathToWin32(StringView(long_unc), out, 1024) == 8 + 313);
    CHECK(WStrCompareN(out, L"\\\\?\\UNC\\server\\share\\bbb", 24) == 0);
    REQUIRE(win::PathToWin32(StringView(long_relative), out, 1024) == 304);
    CHECK(WStrCompareN(out, L"dir\\ccc", 7) == 0);  // never prefixed: the prefix turns off relative-path rules
    CHECK(win::PathToWin32(StringView("\\\\?\\C:\\x"), out, 1024) == 8);
    CHECK(WStrCompare(out, L"\\\\?\\C:\\x") == 0);
    CHECK(win::PathToWin32(StringView(long_drive), out, 10) >= 10);  // does not fit: reported, nothing usable written
    CHECK(out[0] == 0);
}

TEST_CASE("a prefixed long path is normalized as Win32 would normalize it without the prefix", "[windows][utf16]")
{
    // The \\?\ prefix turns off Win32's own handling of `.`, `..` and trailing dots and spaces, so PathToWin32 does it.
    char path[600] = "C:/top/";
    for (int i = 0; i < 260; ++i)
    {
        CStrAppend(path, "x");
    }
    CStrAppend(path, "/./skip/../keep. ./file.txt");
    wchar_t out[1024];
    const usize n = win::PathToWin32(StringView(path), out, 1024);
    wchar_t want[1024] = L"\\\\?\\C:\\top\\";
    for (int i = 0; i < 260; ++i)
    {
        WStrAppend(want, L"x");
    }
    WStrAppend(want, L"\\keep\\file.txt");
    CHECK(n == WStrLength(want));
    CHECK(WStrCompare(out, want) == 0);

    char climb[600] = "C:/";
    for (int i = 0; i < 260; ++i)
    {
        CStrAppend(climb, "y");
    }
    CStrAppend(climb, "/../../../z");
    CHECK(win::PathToWin32(StringView(climb), out, 1024) == 4 + 4);
    CHECK(WStrCompare(out, L"\\\\?\\C:\\z") == 0);  // `..` stops at the root

    char unc[600] = "//server/share/";
    for (int i = 0; i < 260; ++i)
    {
        CStrAppend(unc, "u");
    }
    CStrAppend(unc, "/..");
    CHECK(win::PathToWin32(StringView(unc), out, 1024) == 8 + 13);
    CHECK(WStrCompare(out, L"\\\\?\\UNC\\server\\share\\") ==
          0);  // `..` stops at the share, which keeps its separator as C:\ does
}

TEST_CASE("os descriptors write, read and close files through the engine's handle table", "[windows][os]")
{
    char path[MAX_PATH];
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    FormatTo(path, sizeof(path), "%sengine_os_windows_test_%lu.txt", temp, GetCurrentProcessId());
    const int out = os::CreateFileForWriting(StringView(path));
    REQUIRE(out >= 3);
    const char text[] = "h\xC3\xA9llo";
    CHECK(os::Write(out, text, sizeof(text) - 1) == static_cast<long>(sizeof(text) - 1));
    CHECK(os::Close(out) == 0);
    CHECK(os::Close(out) == -1);  // closed twice
    CHECK(os::Write(out, text, 1) == -1);

    const int in = os::Open(StringView(path), _O_RDONLY);
    REQUIRE(in >= 3);
    char back[16] = {};
    CHECK(os::Read(in, back, sizeof(back)) == static_cast<long>(sizeof(text) - 1));
    CHECK(CStrCompare(back, text) == 0);
    CHECK(os::Read(in, back, sizeof(back)) == 0);  // end of file
    CHECK(os::Close(in) == 0);
    CHECK(os::Open(StringView(path), _O_WRONLY | _O_CREAT | _O_EXCL) == -1);  // exists
    DeleteFileA(path);
    CHECK(os::Open(StringView(path), _O_RDONLY) == -1);  // gone
    CHECK(os::Read(12345, back, 1) == -1);
}

TEST_CASE("the descriptor table holds 256 open files and reuses closed slots", "[windows][os]")
{
    int fds[300];
    int opened = 0;
    for (int i = 0; i < 300; ++i)
    {
        fds[i] = os::Open(StringView("NUL"), _O_WRONLY);
        if (fds[i] < 0)
        {
            break;
        }
        ++opened;
    }
    CHECK(opened == 256);  // nothing else in this test process holds a slot
    for (int i = 0; i < opened; ++i)
    {
        CHECK(os::Close(fds[i]) == 0);
    }
    const int again = os::Open(StringView("NUL"), _O_WRONLY);
    CHECK(again >= 3);
    os::Close(again);
}

TEST_CASE("ReserveMemory returns zeroed, writable memory", "[windows][os]")
{
    constexpr usize kSize = 64 * 1024 * 1024;
    u8* p = static_cast<u8*>(os::ReserveMemory(kSize));
    REQUIRE(p != nullptr);
    CHECK(p[0] == 0);
    CHECK(p[kSize / 2] == 0);
    CHECK(p[kSize - 1] == 0);
    p[kSize - 1] = 7;
    os::ReleaseMemory(p, kSize);
}

TEST_CASE("SleepNanos never returns early, and its resolution is recorded", "[windows][os]")
{
    const u64 requests[] = {1'000, 100'000, 1'000'000};
    for (u64 request : requests)
    {
        u64 shortest = ~0ull;
        u64 longest = 0;
        for (int i = 0; i < 50; ++i)
        {
            const u64 start = os::MonotonicTimeNanos();
            os::SleepNanos(request);
            const u64 slept = os::MonotonicTimeNanos() - start;
            CHECK(slept >= request);
            shortest = slept < shortest ? slept : shortest;
            longest = slept > longest ? slept : longest;
        }
        char line[160];
        FormatTo(line, sizeof(line), "SleepNanos(%llu ns): shortest %llu ns, longest %llu ns of 50",
                 static_cast<unsigned long long>(request), static_cast<unsigned long long>(shortest),
                 static_cast<unsigned long long>(longest));
        WARN(line);
    }
}

TEST_CASE("LoadLibrary finds system libraries by name and never resolves a relative path with a directory",
          "[windows][os]")
{
    os::DynamicLibraryHandle kernel = os::LoadLibrary(StringView("kernel32.dll"));
    REQUIRE(kernel != nullptr);
    CHECK(os::GetSymbol(kernel, "GetTickCount64") != nullptr);
    os::UnloadLibrary(kernel);
    char system_dir[MAX_PATH];
    GetSystemDirectoryA(system_dir, MAX_PATH);
    char absolute[MAX_PATH + 32];
    FormatTo(absolute, sizeof(absolute), "%s/kernel32.dll", system_dir);  // forward slash: converted
    os::DynamicLibraryHandle by_path = os::LoadLibrary(StringView(absolute));
    CHECK(by_path != nullptr);
    os::UnloadLibrary(by_path);
    CHECK(os::LoadLibrary(StringView("sub/kernel32.dll")) == nullptr);
    CHECK(os::LoadLibrary(StringView("..\\System32\\kernel32.dll")) == nullptr);
}

TEST_CASE("EnvironmentEntry skips the drive entries and converts values to UTF-8", "[windows][os]")
{
    REQUIRE(SetEnvironmentVariableW(L"RE_WINDOWS_ENV_TEST", L"h\x00E9llo \x2713 \xD83D\xDE00"));
    const char expect[] = "RE_WINDOWS_ENV_TEST=h\xC3\xA9llo \xE2\x9C\x93 \xF0\x9F\x98\x80";
    bool found = false;
    usize drive_entries = 0;
    const usize count = os::EnvironmentCount();
    for (usize i = 0; i < count; ++i)
    {
        char entry[4096];
        usize length = 0;
        REQUIRE(os::EnvironmentEntry(i, entry, sizeof(entry), &length));
        drive_entries += entry[0] == '=' ? 1 : 0;
        found = found || CStrCompare(entry, expect) == 0;
    }
    CHECK(found);
    CHECK(drive_entries == 0);
    char unused[8];
    usize length = 0;
    CHECK_FALSE(os::EnvironmentEntry(count, unused, sizeof(unused), &length));
    SetEnvironmentVariableW(L"RE_WINDOWS_ENV_TEST", nullptr);
}

TEST_CASE("arguments reach the program as UTF-8, split by the Windows rules for quotes and backslashes",
          "[windows][command_line]")
{
    // CommandLineToArgvW's rules: a quoted argument keeps its spaces; 2n backslashes before a quote give n backslashes;
    // 2n+1 give n and a literal quote; backslashes not before a quote are literal.
    const ProbeResult r = RunProbe(
        L"args \"h\x00E9llo w\x00F6rld\" a\\\\\\\"b \"C:\\path with space\\\\\" \x2713\xD83D\xDE00 plain", true);
    REQUIRE(r.exit_code == 0);
    const char* expected[] = {"h\xC3\xA9llo w\xC3\xB6rld", "a\\\"b", "C:\\path with space\\",
                              "\xE2\x9C\x93\xF0\x9F\x98\x80", "plain"};
    char want[2048] = {};
    for (int i = 0; i < 5; ++i)
    {
        char hex[256];
        Hex(expected[i], hex);
        char line[300];
        FormatTo(line, sizeof(line), "arg %d %s\n", i, hex);
        CStrAppend(want, line);
    }
    // The C runtime may end lines with \r\n on a text-mode standard output.
    char got[8192];
    usize j = 0;
    for (const char* c = r.out; *c != '\0'; ++c)
    {
        if (*c != '\r')
        {
            got[j++] = *c;
        }
    }
    got[j] = '\0';
    CHECK(CStrCompare(got, want) == 0);
}

TEST_CASE("the log writes a non-ASCII line to a pipe byte for byte", "[windows][os][log]")
{
    const ProbeResult r = RunProbe(L"log", true);
    REQUIRE(r.exit_code == 0);
    CHECK(CStrFind(r.out, "probe: h\xC3\xA9llo w\xC3\xB6rld \xE2\x9C\x93 \xF0\x9F\x98\x80") != nullptr);
}

TEST_CASE("WriteAllWithTimeout gives up on a pipe whose reader stopped", "[windows][os]")
{
    const ProbeResult r = RunProbe(L"blocked-stderr", false);
    REQUIRE(r.exit_code == 0);
    long written = -1;
    unsigned long long total = 0, ms = 0;
    REQUIRE(ScanFrom(r.out, "wrote %ld of %llu in %llu ms", &written, &total, &ms) == 3);
    CHECK(written >= 0);
    CHECK(static_cast<unsigned long long>(written) < total);  // the 4 KiB pipe filled; nothing read it
    CHECK(ms >= 150);
    CHECK(ms < 2000);  // the 200 ms deadline held (scheduling allows some slack)
}

TEST_CASE("WriteAllWithTimeout gives up at once while an earlier timed-out write is still blocked", "[windows][os]")
{
    // The first write's helper thread is still stuck in the full pipe and holds the staging buffer; the second call
    // must not wait.
    const ProbeResult r = RunProbe(L"blocked-stderr-twice", false);
    REQUIRE(r.exit_code == 0);
    long written = -1;
    unsigned long long ms = 0;
    REQUIRE(ScanFrom(CStrFind(r.out, "second"), "second wrote %ld in %llu ms", &written, &ms) == 2);
    CHECK(written == 0);
    CHECK(ms < 50);
}
