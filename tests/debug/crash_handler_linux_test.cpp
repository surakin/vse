// The crash handler end to end on Linux: a child process installs it and crashes; the parent finds the envelope, reads
// the minidump out of it with the crash_extract parser, and checks the other results. Also the crash dump directory
// function.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "io/file_system.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "envelope.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include "os/console.h"
#include "os/signal.h"
#include <sys/stat.h>
#include <unistd.h>
#include "support/test_support_posix.h"
#include "types.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::debug;
using debug_test::Cat;
using debug_test::Text;

namespace
{

// A fresh temporary directory, removed with everything in it at the end of the scope.
class CrashTempDir
{
public:
    CrashTempDir()
    {
        const char* tmp = os::GetEnvironment("TMPDIR");
        FormatTo(path_, sizeof(path_), "%s/engine-crash-XXXXXX", tmp != nullptr ? tmp : "/tmp");
        if (mkdtemp(path_) == nullptr)
        {
            path_[0] = '\0';
        }
    }
    ~CrashTempDir()
    {
        if (path_[0] != '\0')
        {
            debug_test::RemoveTree(path_);
        }
    }
    CrashTempDir(const CrashTempDir&) = delete;
    CrashTempDir& operator=(const CrashTempDir&) = delete;
    const char* Path() const { return path_; }

private:
    char path_[256];
};

void CollectEnvelopes(const char* directory, engine_test::TextList* found)
{
    debug_test::FindFiles(directory, ".envelope", found);
}

void PutLine(const char* label, int value)
{
    char line[64];
    const int n = FormatTo(line, sizeof(line), "%s=%d\n", label, value);
    os::Write(1, line, static_cast<usize>(n));
}

}  // namespace

// ---- children
// ------------------------------------------------------------------------------------------------------------- Each
// takes the dump directory as its argument.

ENGINE_DEATH_TEST_FN(dt_crash_install_then_fault)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    options.release = "crash-test@1";
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_default_release_then_fault)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);  // no release given: the build id is used
    InitCrashContext(0, nullptr);
    char line[96];
    const int n = FormatTo(line, sizeof(line), "BUILD_ID=%s\n", GetCrashContext().build_id);
    os::Write(1, line, static_cast<usize>(n));
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("Without a release string the envelope names the binary by its build id",
          "[crash][crashreport][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_default_release_then_fault", dir.Path(), options);
    const StringView out = debug_test::StdoutView(r);
    REQUIRE(out.Contains("BUILD_ID="));
    const Text id = engine_test::MakeText(debug_test::ValueAfter(out, "BUILD_ID="));
    REQUIRE(id.Length() >= 8);
    engine_test::TextList envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    REQUIRE(envelopes.Size() == 1);
    CHECK(debug_test::ReadWholeFile(envelopes[0]).View().Contains(id.View()));
}

ENGINE_DEATH_TEST_FN(dt_crash_install_twice)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("FIRST", static_cast<int>(InstallCrashHandler(options)));
    PutLine("SECOND", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    UninstallCrashHandler();
    PutLine("AFTER-UNINSTALL", CrashHandlerInstalled() ? 1 : 0);
}

ENGINE_DEATH_TEST_FN(dt_crash_uninstall_then_fault)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    UninstallCrashHandler();
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_fault_without_handler)
{
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_bad_directory)
{
    CrashHandlerOptions options;
    options.dump_directory = "/proc/self/not-a-place-for-dumps/crashes";  // /proc cannot hold new directories
    PutLine("RESULT", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    os::Write(1, "PROGRAM-RAN-ON\n", 15);
}

// Uses the default directory ($XDG_STATE_HOME/<app>/crashes), set by the parent through the environment.
ENGINE_DEATH_TEST_FN(dt_crash_default_directory_then_fault)
{
    CrashHandlerOptions options;  // no dump_directory
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_print_default_directory)
{
    io::Path directory;
    const bool ok = io::CrashDumpDirectory(&directory).Ok();
    PutLine("OK", ok ? 1 : 0);
    os::Write(1, "PATH=", 5);
    os::Write(1, directory.View().Data(), directory.View().Length());
    os::Write(1, "\n", 1);
}

// ---- tests
// ----------------------------------------------------------------------------------------------------------------

TEST_CASE("A crash with the handler installed leaves one envelope holding a minidump and still kills the process",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    REQUIRE(dir.Path()[0] != '\0');
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_install_then_fault", dir.Path(), options);
    INFO("child stdout: " << Cat(debug_test::StdoutView(r)).CStr());
    CHECK(r.StdoutContains("INSTALLED=0"));  // CrashHandlerResult::Installed
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(r.native_code == SIGSEGV);  // the original fault is still what the process dies from

    engine_test::TextList envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    REQUIRE(envelopes.Size() == 1);
    const Text content = debug_test::ReadWholeFile(envelopes[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(content.CStr(), content.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
    CHECK(dump.size > 1000);
    CHECK(MemCompare(dump.data, "MDMP", 4) == 0);
    // The release string given at install time travels with the dump.
    CHECK(content.View().Contains("crash-test@1"));
}

TEST_CASE("Installing the crash handler twice changes nothing the second time, and it can be uninstalled",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    const ChildResult r = RunInChild("dt_crash_install_twice", dir.Path());
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("FIRST=0"));   // Installed
    CHECK(r.StdoutContains("SECOND=1"));  // AlreadyInstalled
    CHECK(r.StdoutContains("INSTALLED=1"));
    CHECK(r.StdoutContains("AFTER-UNINSTALL=0"));
    // A normal install says nothing: the library's complaint about having no server address is filtered out.
    CHECK_FALSE(r.StdoutContains("LogCrash"));
    CHECK_FALSE(r.StdoutContains("DSN"));
}

TEST_CASE("After the crash handler is uninstalled a fault behaves as if it never was installed",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_uninstall_then_fault", dir.Path(), options);
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
    engine_test::TextList envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    CHECK(envelopes.Empty());
}

TEST_CASE("A fault before the crash handler is installed leaves no dump and the original signal",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_crash_fault_without_handler");
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
}

TEST_CASE("A dump directory that cannot be created fails the install, logs, and the program runs on",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_crash_bad_directory");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("RESULT=3"));  // DirectoryFailed
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.StdoutContains("PROGRAM-RAN-ON"));
    // The failure is logged as a warning on the crash channel.
    CHECK(r.StdoutContains("crash handler not installed"));
}

TEST_CASE("Without a directory in the options the dump goes under the user's state directory",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir state_home;
    const Text state_env = Cat("XDG_STATE_HOME=", state_home.Path());
    const char* env[] = {state_env.CStr(), "ENGINE_APP_NAME=engine-crash-test"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 2;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_default_directory_then_fault", "", options);
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.terminated);
    engine_test::TextList envelopes;
    CollectEnvelopes(Cat(state_home.Path(), "/engine-crash-test/crashes").CStr(), &envelopes);
    CHECK(envelopes.Size() == 1);
}

TEST_CASE("The crash dump directory is created private to the user and named after the application",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir state_home;
    const Text state_env = Cat("XDG_STATE_HOME=", state_home.Path());
    const char* env[] = {state_env.CStr(), "ENGINE_APP_NAME=engine-crash-test"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 2;
    const ChildResult r = RunInChild("dt_crash_print_default_directory", "", options);
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("OK=1"));
    const Text expected = Cat("PATH=", state_home.Path(), "/engine-crash-test/crashes\n");
    CHECK(r.StdoutContains(expected.CStr()));
    struct stat app_info;
    struct stat crashes_info;
    REQUIRE(::stat(Cat(state_home.Path(), "/engine-crash-test").CStr(), &app_info) == 0);
    REQUIRE(::stat(Cat(state_home.Path(), "/engine-crash-test/crashes").CStr(), &crashes_info) == 0);
    CHECK((app_info.st_mode & 0777) == 0700);
    CHECK((crashes_info.st_mode & 0777) == 0700);
    CHECK(S_ISDIR(crashes_info.st_mode));
}

// Changes the environment, so it runs in a child and never in the multithreaded test process.
ENGINE_DEATH_TEST_FN(dt_crash_directory_without_home)
{
    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    io::Path path("untouched");
    PutLine("FOUND", io::CrashDumpDirectory(&path).Ok() ? 1 : 0);
    PutLine("EMPTY", path == io::Path("untouched") ? 1 : 0);  // unchanged on failure
}

TEST_CASE("The crash dump directory is not found without a home or state directory", "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult none = RunInChild("dt_crash_directory_without_home");
    REQUIRE(none.exited);
    CHECK(none.StdoutContains("FOUND=0"));
    CHECK(none.StdoutContains("EMPTY=1"));
}

TEST_CASE("An application name that would leave the intended directory or be cut short is refused",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // The state directory is one level inside the temporary directory, so a name that climbed out of it would land in
    // the temporary directory (which is removed at the end) and not somewhere shared like /tmp.
    CrashTempDir outer;
    const Text state_home = Cat(outer.Path(), "/state");
    REQUIRE(::mkdir(state_home.CStr(), 0700) == 0);
    const Text state_env = Cat("XDG_STATE_HOME=", state_home);
    const Text too_long = Cat("ENGINE_APP_NAME=", engine_test::Repeated(300, 'a'));
    for (const char* bad : {"ENGINE_APP_NAME=..", "ENGINE_APP_NAME=.", "ENGINE_APP_NAME=a/b", "ENGINE_APP_NAME=../x"})
    {
        const char* env[] = {state_env.CStr(), bad};
        ChildOptions options;
        options.extra_env = env;
        options.extra_env_count = 2;
        const ChildResult r = RunInChild("dt_crash_print_default_directory", "", options);
        INFO(bad);
        REQUIRE(r.exited);
        CHECK(r.StdoutContains("OK=0"));
    }
    const char* env_long[] = {state_env.CStr(), too_long.CStr()};
    ChildOptions long_options;
    long_options.extra_env = env_long;
    long_options.extra_env_count = 2;
    CHECK(RunInChild("dt_crash_print_default_directory", "", long_options).StdoutContains("OK=0"));
    // Nothing was created: not beside the state directory (where `..` would have put it), not inside it.
    struct stat info;
    CHECK(::stat(Cat(outer.Path(), "/crashes").CStr(), &info) != 0);
    CHECK(::stat(Cat(state_home, "/crashes").CStr(), &info) != 0);
}

TEST_CASE("A private directory must be a real directory owned by the user, and is tightened to the owner only",
          "[crash][debug][linux]")
{
    CrashTempDir dir;
    const Text root = engine_test::MakeText(dir.Path());
    // An existing directory open to others is tightened.
    const Text open_dir = Cat(root, "/open");
    REQUIRE(::mkdir(open_dir.CStr(), 0755) == 0);
    REQUIRE(::chmod(open_dir.CStr(), 0755) == 0);
    CHECK(io::CreatePrivateDirectory(io::Path(open_dir.View())).Ok());
    struct stat info;
    REQUIRE(::stat(open_dir.CStr(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    // A trailing slash makes no difference.
    CHECK(io::CreatePrivateDirectory(io::Path(Cat(open_dir, "/").View())).Ok());
    // A symbolic link is refused, even one that points at a good directory, with or without a trailing slash.
    const Text link = Cat(root, "/link");
    REQUIRE(::symlink(open_dir.CStr(), link.CStr()) == 0);
    CHECK(io::CreatePrivateDirectory(io::Path(link.View())).Failed());
    CHECK(io::CreatePrivateDirectory(io::Path(Cat(link, "/").View())).Failed());
    // A file is refused.
    const Text file = Cat(root, "/file");
    REQUIRE(debug_test::WriteWholeFile(file.View(), "", 0));
    CHECK(io::CreatePrivateDirectory(io::Path(file.View())).Failed());
    // A directory the user cannot write to is refused (root can write anywhere, so the check is skipped for root).
    if (::geteuid() != 0)
    {
        const Text read_only = Cat(root, "/readonly");
        REQUIRE(::mkdir(read_only.CStr(), 0500) == 0);
        CHECK(io::CreatePrivateDirectory(io::Path(read_only.View())).Failed());
        ::chmod(read_only.CStr(), 0700);  // so the temp directory can be removed
    }
}

ENGINE_DEATH_TEST_FN(dt_crash_install_into)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("RESULT", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
}

TEST_CASE("The crash handler is not installed into a symbolic link or a directory that is not writable",
          "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    const Text target = Cat(dir.Path(), "/target");
    REQUIRE(::mkdir(target.CStr(), 0700) == 0);
    const Text link = Cat(dir.Path(), "/link");
    REQUIRE(::symlink(target.CStr(), link.CStr()) == 0);
    const ChildResult through_link = RunInChild("dt_crash_install_into", link.CStr());
    REQUIRE(through_link.exited);
    CHECK(through_link.StdoutContains("RESULT=3"));  // DirectoryFailed
    CHECK(through_link.StdoutContains("INSTALLED=0"));
    if (::geteuid() != 0)
    {
        const Text read_only = Cat(dir.Path(), "/readonly");
        REQUIRE(::mkdir(read_only.CStr(), 0500) == 0);
        const ChildResult unwritable = RunInChild("dt_crash_install_into", read_only.CStr());
        CHECK(unwritable.StdoutContains("RESULT=3"));
        ::chmod(read_only.CStr(), 0700);
    }
}

TEST_CASE("CreateDirectories creates missing parents, accepts an existing directory and refuses a file",
          "[crash][debug][linux]")
{
    CrashTempDir dir;
    const Text nested = Cat(dir.Path(), "/a/b/c");
    CHECK(io::CreateDirectories(io::Path(nested.View())).Ok());
    struct stat info;
    REQUIRE(::stat(nested.CStr(), &info) == 0);
    CHECK(S_ISDIR(info.st_mode));
    CHECK(io::CreateDirectories(io::Path(nested.View())).Ok());  // already there
    const Text file = Cat(dir.Path(), "/a/file");
    REQUIRE(debug_test::WriteWholeFile(file.View(), "", 0));
    CHECK(io::CreateDirectories(io::Path(file.View())).Failed());
    CHECK(io::CreateDirectories(io::Path(Cat(file, "/below").View())).Failed());
    CHECK(io::CreateDirectories(io::Path()).Failed());
    // A private directory's missing parents are private as well.
    const Text private_nested = Cat(dir.Path(), "/p/q/r");
    REQUIRE(io::CreatePrivateDirectory(io::Path(private_nested.View())).Ok());
    REQUIRE(::stat(Cat(dir.Path(), "/p/q").CStr(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
}

// ---- a crash must survive the next start
// ----------------------------------------------------------------------------------

// Installs the handler in the given directory and exits normally: what a program does on the launch after a crash.
ENGINE_DEATH_TEST_FN(dt_crash_install_and_exit)
{
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    UninstallCrashHandler();
}

TEST_CASE("A crash dump is still there after the program is started again", "[crash][death][debug][linux]")
{
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult crashed = RunInChild("dt_crash_install_then_fault", dir.Path(), options);
    REQUIRE(crashed.terminated);
    engine_test::TextList before;
    CollectEnvelopes(dir.Path(), &before);
    REQUIRE(before.Size() == 1);
    const Text original = debug_test::ReadWholeFile(before[0]);
    REQUIRE(original.Length() > 1000);

    // The next launch installs the handler again (twice, as a restart loop would), and must not lose the earlier dump.
    for (int launch = 0; launch < 2; ++launch)
    {
        const ChildResult restarted = RunInChild("dt_crash_install_and_exit", dir.Path(), options);
        REQUIRE(restarted.exited);
        CHECK(restarted.exit_code == 0);
        CHECK(restarted.StdoutContains("INSTALLED=0"));
    }
    engine_test::TextList after;
    CollectEnvelopes(dir.Path(), &after);
    REQUIRE(after.Size() == 1);
    const Text kept = debug_test::ReadWholeFile(after[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(kept.CStr(), kept.Length(), &dump) == crash_extract::EnvelopeResult::Ok);
    CHECK(MemCompare(dump.data, "MDMP", 4) == 0);
    CHECK(dump.size > 1000);
}
