// Post-mortem dumps on Windows (Phase 13 Day 156; os/windows/debug/BACKEND.md, EnablePostMortemDumps). Windows Error
// Reporting writes a local dump only for what the machine's LocalDumps key lists, which needs an administrator, so the
// function only reads: these tests check what it reports in each state of the machine and that it writes nothing (no
// per-user key appears: WER ignores that one, measured on Day 156).
#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "debug/death_test.h"
#include "debug/post_mortem.h"
#include "support/death_test_skip.h"

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "os/console.h"

#include "os/windows/debug/post_mortem_windows.h"
#include "os/windows/win32.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace
{

const wchar_t* ExeName()
{
    static wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    const wchar_t* name = exe;
    for (const wchar_t* p = exe; *p != 0; ++p)
    {
        if (*p == L'\\')
        {
            name = p + 1;
        }
    }
    return name;
}

bool KeyExists(HKEY root, const wchar_t* key)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
    {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

// True if WER keeps local dumps of this program on this machine (its own key or the machine-wide one).
bool MachineKeepsLocalDumps()
{
    wchar_t key[MAX_PATH * 2];
    WFormatTo(key, sizeof(key) / sizeof(key[0]),
              L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\%ls", ExeName());
    return KeyExists(HKEY_LOCAL_MACHINE, key) ||
           KeyExists(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps");
}

bool UserKeyExists()
{
    wchar_t key[MAX_PATH * 2];
    WFormatTo(key, sizeof(key) / sizeof(key[0]),
              L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\%ls", ExeName());
    return KeyExists(HKEY_CURRENT_USER, key);
}

void PrintReport(const PostMortemReport& report)
{
    os::Print("RESULT=%d;\nDESTINATION=%s;\nNOTE=%s;\n", static_cast<int>(report.result), report.destination,
              report.note);
    os::Flush();
}

}  // namespace

ENGINE_DEATH_TEST_FN(dt_pm_win_in_death_child)
{
    PrintReport(EnablePostMortemDumps(PostMortemOptions()));
}

ENGINE_DEATH_TEST_FN(dt_pm_win_error_mode_cleared)
{
    SetErrorMode(0);
    PrintReport(EnablePostMortemDumps(PostMortemOptions()));
}

// The reading itself, against a key of the test's own (a throw-away HKCU key, removed at the end), since the machine's
// needs an administrator.
TEST_CASE("Windows: a LocalDumps key is read with its folder's variables expanded, the program's own key first",
          "[postmortem][debug][windows]")
{
    wchar_t base[128];
    WFormatTo(base, sizeof(base) / sizeof(base[0]), L"Software\\engine-post-mortem-test-%lu",
              static_cast<unsigned long>(GetCurrentProcessId()));
    wchar_t dumps_key[192];
    WFormatTo(dumps_key, sizeof(dumps_key) / sizeof(dumps_key[0]), L"%ls\\LocalDumps", base);
    wchar_t program_key[256];
    WFormatTo(program_key, sizeof(program_key) / sizeof(program_key[0]), L"%ls\\game.exe", dumps_key);
    struct RemoveTree
    {
        const wchar_t* key;
        ~RemoveTree() { RegDeleteTreeW(HKEY_CURRENT_USER, key); }
    } remove{base};
    HKEY handle = nullptr;
    REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, program_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) ==
            ERROR_SUCCESS);
    const wchar_t folder[] = L"%TEMP%\\engine-pm-folder";
    const DWORD full = 2;
    CHECK(RegSetValueExW(handle, L"DumpFolder", 0, REG_EXPAND_SZ, reinterpret_cast<const BYTE*>(folder),
                         sizeof(folder)) == ERROR_SUCCESS);
    CHECK(RegSetValueExW(handle, L"DumpType", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&full), sizeof(full)) ==
          ERROR_SUCCESS);
    RegCloseKey(handle);

    PostMortemReport report;
    u32 dump_type = 0;
    REQUIRE(debug::detail::DescribeLocalDumps(HKEY_CURRENT_USER, dumps_key, L"game.exe", &report, &dump_type));
    INFO(report.destination);
    CHECK(dump_type == 2);
    CHECK(CStrFind(report.destination, "%TEMP%") == nullptr);  // expanded
    CHECK(CStrFind(report.destination, "engine-pm-folder (full dump)") != nullptr);
    CHECK(report.current_limit_bytes == ~u64{0});

    // Another program falls back to the values on the LocalDumps key itself; here there are none, so WER's defaults are
    // named.
    PostMortemReport other;
    REQUIRE(debug::detail::DescribeLocalDumps(HKEY_CURRENT_USER, dumps_key, L"other.exe", &other, &dump_type));
    CHECK(CStrFind(other.destination, "CrashDumps (mini dump)") != nullptr);

    // No key at all: false, with the key to create.
    wchar_t missing[192];
    WFormatTo(missing, sizeof(missing) / sizeof(missing[0]), L"%ls\\Absent", base);
    PostMortemReport none;
    CHECK_FALSE(debug::detail::DescribeLocalDumps(HKEY_CURRENT_USER, missing, L"game.exe", &none, &dump_type));
    CHECK(CStrFind(none.note, "LocalDumps\\game.exe") != nullptr);
}

TEST_CASE("Windows: in a death-test child post-mortem dumps are Limited, with a note",
          "[postmortem][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const bool user_key_before = UserKeyExists();
    const ChildResult r = RunInChild("dt_pm_win_in_death_child");
    INFO(Catch::Detail::stringify(StringView(r.stdout_text, r.stdout_length)));
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("RESULT=1;"));
    CHECK_FALSE(r.StdoutContains("NOTE=;"));
    CHECK_FALSE(r.StdoutContains("DESTINATION=;"));
    if (MachineKeepsLocalDumps())
    {
        CHECK(r.StdoutContains("SEM_NOGPFAULTERRORBOX"));  // the child's error mode is what stops it
    }
    CHECK(UserKeyExists() == user_key_before);  // nothing written
}

TEST_CASE("Windows: post-mortem dumps report the machine's LocalDumps setting, or how to set it",
          "[postmortem][death][debug][windows]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const bool user_key_before = UserKeyExists();
    const ChildResult r = RunInChild("dt_pm_win_error_mode_cleared");
    INFO(Catch::Detail::stringify(StringView(r.stdout_text, r.stdout_length)));
    REQUIRE(r.exited);
    if (MachineKeepsLocalDumps())
    {
        CHECK((r.StdoutContains("RESULT=0;") ||
               r.StdoutContains("RESULT=1;")));  // Limited only when WER is turned off on the machine
        CHECK(r.StdoutContains("DESTINATION=Windows Error Reporting LocalDumps: "));
    }
    else
    {
        // This PC (Day 156): no LocalDumps key, so the note names the one to create, for this program.
        CHECK(r.StdoutContains("RESULT=1;"));
        CHECK(r.StdoutContains("HKLM\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\debug_tests"));
        CHECK(r.StdoutContains("DESTINATION=none: "));
    }
    CHECK(UserKeyExists() == user_key_before);
}
