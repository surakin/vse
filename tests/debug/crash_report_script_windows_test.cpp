// scripts/store-symbols.ps1, crash-report.ps1, find-dump.ps1 and open-dump.ps1 on a dump from a crashing child (Phase 13 Day 156; the Linux
// twin is crash_report_script_linux_test.cpp): the store files the PDB under the dump's build id, the report names the faulting function and
// its line from the store, a different build of the program is refused, and a machine without a debugger gets an instruction.
#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "debug/death_test.h"
#include "io/directory.h"
#include "io/file.h"
#include "io/file_system.h"
#include "io/path.h"
#include "support/death_test_skip.h"
#include "test_text.h"

#include "containers/array.h"
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "lang/initializer_list.h"
#include "os/windows/utf16.h"

#include "os/windows/win32.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using engine_test::MakeText;
using engine_test::Text;

namespace {

struct ScriptRun {
    Text output = MakeText();
    int exit_code = -1;
};

// Runs one of the PowerShell scripts with its arguments (each quoted), standard error with standard output.
ScriptRun RunScript(const char* script, InitializerList<const char*> arguments) {
    _putenv_s("ENGINE_CRASH_EXTRACT", ENGINE_CRASH_EXTRACT_PATH);
    Text command = engine_test::Format("\"powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\\scripts\\%s\"", ENGINE_SOURCE_DIR, script);
    for (const char* argument : arguments) command.Append(engine_test::Format(" \"%s\"", argument).View());
    command.Append(StringView(" 2>&1\"")); // the outer quotes are cmd.exe's (_popen runs cmd /c)
    // cmd.exe runs the line, as _popen would; standard output and standard error share one pipe, read until the script ends.
    ScriptRun run;
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) return run;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    const Text line = engine_test::Format("cmd.exe /d /c %s", command.CStr());
    Vector<wchar_t> wide;
    wide.Resize(line.View().Length() + 1);
    const usize units = os::windows::Utf8ToUtf16(line.View(), wide.Data(), wide.Size());
    wide[units < wide.Size() ? units : wide.Size() - 1] = L'\0';
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    PROCESS_INFORMATION process = {};
    const bool started = CreateProcessW(nullptr, wide.Data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
    CloseHandle(write_end);
    if (started) {
        Array<char, 4096> buffer;
        DWORD got = 0;
        while (ReadFile(read_end, buffer.Data(), static_cast<DWORD>(buffer.Size()), &got, nullptr) && got > 0) run.output.AppendRawBytes(buffer.Data(), got);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        run.exit_code = static_cast<int>(code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    CloseHandle(read_end);
    return run;
}

bool Has(const Text& text, const char* needle) {
    return text.View().Find(StringView(needle)) != text.Length();
}

class ScriptTempDir {
public:
    ScriptTempDir() {
        io::Path base;
        ok_ = io::TempDirectory(&base).Ok() && io::CreateTempDirectory(base, "engine-script-", &path_).Ok();
        if (ok_) text_ = MakeText(path_.View());
    }
    ~ScriptTempDir() {
        if (ok_) (void)io::RemoveAll(path_);
    }
    ScriptTempDir(const ScriptTempDir&) = delete;
    ScriptTempDir& operator=(const ScriptTempDir&) = delete;
    const char* Path() const { return text_.CStr(); }
    const io::Path& IoPath() const { return path_; }

private:
    io::Path path_;
    Text text_ = MakeText();
    bool ok_ = false;
};

io::WalkAction FindEnvelope(void* user, const io::WalkEntry& entry) {
    if (entry.type == io::FileType::Regular && entry.name.EndsWith(".envelope")) {
        *static_cast<Text*>(user) = MakeText(entry.path.View());
        return io::WalkAction::Stop;
    }
    return io::WalkAction::Continue;
}

// Crashes a child with the crash handler installed into `directory` (the end-to-end test's null write) and returns its envelope.
Text CrashInto(const ScriptTempDir& directory) {
    ChildOptions options;
    options.timeout_ms = 60000;
    (void)RunInChild("dt_e2e_win_null_write", directory.Path(), options);
    Text envelope = MakeText();
    io::WalkOptions walk;
    (void)io::WalkDirectory(directory.IoPath(), walk, &FindEnvelope, &envelope);
    return envelope;
}

Text OwnExecutable() {
    wchar_t wide[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, wide, static_cast<DWORD>(sizeof(wide) / sizeof(wide[0])));
    char utf8[MAX_PATH * 4] = {};
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return MakeText(StringView(utf8));
}

} // namespace

TEST_CASE("Windows: crash-report.ps1 names the faulting function from a store that store-symbols.ps1 filled", "[crash][crashreport][death][debug][windows]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ScriptTempDir dumps;
    ScriptTempDir store;
    REQUIRE(dumps.Path()[0] != '\0');
    const Text envelope = CrashInto(dumps);
    REQUIRE(envelope.Length() > 0);
    const Text exe = OwnExecutable();

    const ScriptRun stored = RunScript("store-symbols.ps1", {exe.CStr(), store.Path()});
    INFO(stored.output.CStr());
    REQUIRE(stored.exit_code == 0);
    // The PDB is filed under the build id the dump records for the program, in upper case.
    const ScriptRun modules = RunScript("crash-report.ps1", {envelope.CStr()});
    REQUIRE(modules.exit_code == 0);
    const usize line_end = stored.output.View().IndexOf('\n');
    Text id = MakeText(stored.output.View().Substring(0, line_end));
    if (id.Length() > 0 && id.CStr()[id.Length() - 1] == '\r') id = MakeText(id.View().Substring(0, id.Length() - 1));
    INFO(id.CStr());
    CHECK(Has(modules.output, id.CStr()));
    Text upper = MakeText();
    for (usize i = 0; i < id.Length(); ++i) {
        const char c = id.CStr()[i];
        upper.Append(c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c);
    }
    CHECK(Has(stored.output, engine_test::Format("debug_tests%s.pdb\\%s\\", Has(exe, "_asan") ? "_asan" : "", upper.CStr()).CStr()));

    const ScriptRun report = RunScript("crash-report.ps1", {envelope.CStr(), exe.CStr(), store.Path()});
    INFO(report.output.CStr());
    CHECK(report.exit_code == 0);
    CHECK(Has(report.output, "exception: thread "));
    CHECK(Has(report.output, "=== CRASH REPORT ==="));
    CHECK(Has(report.output, "InjectFault"));
    CHECK(Has(report.output, "fault_injection.cpp:"));
}

TEST_CASE("Windows: crash-report.ps1 refuses a different build of the program, and says how it is used", "[crash][crashreport][death][debug][windows]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ScriptRun usage = RunScript("crash-report.ps1", {});
    CHECK(usage.exit_code == 64);
    CHECK(Has(usage.output, "usage:"));
    // Another build with the same file name: the other flavor of this test program, copied under this one's name.
    const Text exe = OwnExecutable();
    const bool asan = Has(exe, "_asan.exe");
    const Text other = asan ? engine_test::Concat(exe.View().Substring(0, exe.Length() - 9), ".exe")
                            : engine_test::Concat(exe.View().Substring(0, exe.Length() - 4), "_asan.exe");
    io::Path other_path;
    io::FileInfo info;
    if (!io::Path::Parse(other.View(), io::NativePathStyle(), &other_path) || io::Stat(other_path, &info).Failed()) {
        SKIP("the other flavor of the test program is not built, so there is no different build to give");
    }
    ScriptTempDir dumps;
    ScriptTempDir copy;
    const Text envelope = CrashInto(dumps);
    REQUIRE(envelope.Length() > 0);
    const char* name = CStrFindLastChar(exe.CStr(), '\\');
    REQUIRE(name != nullptr);
    io::Path target;
    REQUIRE(io::Path::Parse(engine_test::Concat(StringView(copy.Path()), StringView(name)).View(), io::NativePathStyle(), &target));
    REQUIRE(io::Copy(other_path, target, false).Ok());
    const Text target_text = MakeText(target.View());
    const ScriptRun mismatch = RunScript("crash-report.ps1", {envelope.CStr(), target_text.CStr()});
    INFO(mismatch.output.CStr());
    CHECK(mismatch.exit_code == 3);
    CHECK(Has(mismatch.output, "different build"));
}

TEST_CASE("Windows: find-dump.ps1 lists an application's envelopes, newest first, and refuses a path for a name", "[crash][debug][windows]") {
    ScriptTempDir local;
    REQUIRE(local.Path()[0] != '\0');
    io::Path saved = local.IoPath().Join("engine-find-test").Join("crashes").Join("saved");
    REQUIRE(io::CreateDirectories(saved).Ok());
    REQUIRE(io::WriteFile(saved.Join("one.envelope"), "x", 1).Ok());
    // The script reads %LOCALAPPDATA%: point it at the temporary tree for this one call.
    char previous[MAX_PATH] = {};
    const DWORD had = GetEnvironmentVariableA("LOCALAPPDATA", previous, MAX_PATH);
    _putenv_s("LOCALAPPDATA", local.Path());
    const ScriptRun found = RunScript("find-dump.ps1", {"engine-find-test"});
    const ScriptRun none = RunScript("find-dump.ps1", {"engine-find-test-absent"});
    const ScriptRun path = RunScript("find-dump.ps1", {"..\\elsewhere"});
    _putenv_s("LOCALAPPDATA", had > 0 && had < MAX_PATH ? previous : "");
    INFO(found.output.CStr());
    CHECK(found.exit_code == 0);
    CHECK(Has(found.output, "one.envelope"));
    CHECK(Has(found.output, "engine"));
    CHECK(none.exit_code == 1);
    CHECK(path.exit_code == 64);
}

TEST_CASE("Windows: open-dump.ps1 opens a dump in cdb, or says how to install a debugger", "[crash][death][debug][windows]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    ScriptTempDir dumps;
    const Text envelope = CrashInto(dumps);
    REQUIRE(envelope.Length() > 0);
    const ScriptRun run = RunScript("open-dump.ps1", {envelope.CStr(), "-Batch"});
    INFO(run.output.CStr());
    if (run.exit_code == 66) {
        CHECK(Has(run.output, "no debugger was found"));
    } else {
        CHECK(run.exit_code == 0);
        CHECK(Has(run.output, "debug_tests"));
    }
}
