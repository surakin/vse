// The crash handler end to end on Linux: a child process installs it and crashes; the parent finds the envelope, reads the
// minidump out of it with the crash_extract parser, and checks the other results. Also the crash dump directory function.
#include <catch2/catch_test_macros.hpp>

#include "debug/crash_context.h"
#include "debug/crash_handler.h"
#include "debug/death_test.h"
#include "debug/fault_injection.h"
#include "envelope.h"
#include "os/os.h"
#include "support/death_test_skip.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <dirent.h>
#include <ftw.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace engine_placeholder;
using namespace engine_placeholder::debug;

namespace {

// A fresh temporary directory, removed with everything in it at the end of the scope.
class CrashTempDir {
public:
    CrashTempDir() {
        const char* tmp = std::getenv("TMPDIR");
        std::snprintf(path_, sizeof(path_), "%s/engine-crash-XXXXXX", tmp != nullptr ? tmp : "/tmp");
        if (mkdtemp(path_) == nullptr) {
            path_[0] = '\0';
        }
    }
    ~CrashTempDir() {
        if (path_[0] != '\0') {
            nftw(path_, &Remove, 16, FTW_DEPTH | FTW_PHYS);
        }
    }
    CrashTempDir(const CrashTempDir&) = delete;
    CrashTempDir& operator=(const CrashTempDir&) = delete;
    const char* Path() const { return path_; }

private:
    static int Remove(const char* entry, const struct stat*, int, struct FTW*) { return ::remove(entry); }
    char path_[256];
};

void CollectEnvelopes(const std::string& directory, std::vector<std::string>* found) {
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        return;
    }
    while (const dirent* entry = readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        const std::string path = directory + "/" + entry->d_name;
        struct stat info;
        if (::stat(path.c_str(), &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            CollectEnvelopes(path, found);
        } else if (path.size() > 9 && path.compare(path.size() - 9, 9, ".envelope") == 0) {
            found->push_back(path);
        }
    }
    closedir(dir);
}

std::string ReadFile(const std::string& path) {
    std::string content;
    if (std::FILE* file = std::fopen(path.c_str(), "rb")) {
        char chunk[4096];
        std::size_t n;
        while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
            content.append(chunk, n);
        }
        std::fclose(file);
    }
    return content;
}

void PutLine(const char* label, int value) {
    char line[64];
    const int n = std::snprintf(line, sizeof(line), "%s=%d\n", label, value);
    os::Write(1, line, static_cast<usize>(n));
}

} // namespace

// ---- children -------------------------------------------------------------------------------------------------------------
// Each takes the dump directory as its argument.

ENGINE_DEATH_TEST_FN(dt_crash_install_then_fault) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    options.release = "crash-test@1";
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_default_release_then_fault) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options); // no release given: the build id is used
    InitCrashContext(0, nullptr);
    char line[96];
    const int n = std::snprintf(line, sizeof(line), "BUILD_ID=%s\n", GetCrashContext().build_id);
    os::Write(1, line, static_cast<usize>(n));
    InjectFault(FaultKind::NullWrite);
}

TEST_CASE("Without a release string the envelope names the binary by its build id", "[crash][crashreport][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_default_release_then_fault", dir.Path(), options);
    const std::string out(r.stdout_text, r.stdout_length);
    const usize at = out.find("BUILD_ID=");
    REQUIRE(at != std::string::npos);
    const std::string id = out.substr(at + 9, out.find('\n', at) - at - 9);
    REQUIRE(id.size() >= 8);
    std::vector<std::string> envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    REQUIRE(envelopes.size() == 1);
    CHECK(ReadFile(envelopes[0]).find(id) != std::string::npos);
}

ENGINE_DEATH_TEST_FN(dt_crash_install_twice) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("FIRST", static_cast<int>(InstallCrashHandler(options)));
    PutLine("SECOND", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    UninstallCrashHandler();
    PutLine("AFTER-UNINSTALL", CrashHandlerInstalled() ? 1 : 0);
}

ENGINE_DEATH_TEST_FN(dt_crash_uninstall_then_fault) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    InstallCrashHandler(options);
    UninstallCrashHandler();
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_fault_without_handler) {
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_bad_directory) {
    CrashHandlerOptions options;
    options.dump_directory = "/proc/self/not-a-place-for-dumps/crashes"; // /proc cannot hold new directories
    PutLine("RESULT", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
    os::Write(1, "PROGRAM-RAN-ON\n", 15);
}

// Uses the default directory ($XDG_STATE_HOME/<app>/crashes), set by the parent through the environment.
ENGINE_DEATH_TEST_FN(dt_crash_default_directory_then_fault) {
    CrashHandlerOptions options; // no dump_directory
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    InjectFault(FaultKind::NullWrite);
}

ENGINE_DEATH_TEST_FN(dt_crash_print_default_directory) {
    char path[os::kMaxPathBytes];
    const bool ok = os::GetCrashDumpDirectory(path, sizeof(path));
    PutLine("OK", ok ? 1 : 0);
    os::Write(1, "PATH=", 5);
    os::Write(1, path, std::strlen(path));
    os::Write(1, "\n", 1);
}

// ---- tests ----------------------------------------------------------------------------------------------------------------

TEST_CASE("A crash with the handler installed leaves one envelope holding a minidump and still kills the process", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    REQUIRE(dir.Path()[0] != '\0');
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_install_then_fault", dir.Path(), options);
    INFO("child stdout: " << std::string(r.stdout_text, r.stdout_length));
    CHECK(r.StdoutContains("INSTALLED=0")); // CrashHandlerResult::Installed
    CHECK(r.terminated);
    CHECK(r.reason == CrashReason::AccessViolation);
    CHECK(r.native_code == SIGSEGV); // the original fault is still what the process dies from

    std::vector<std::string> envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    REQUIRE(envelopes.size() == 1);
    const std::string content = ReadFile(envelopes[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(content.data(), content.size(), &dump) == crash_extract::EnvelopeResult::Ok);
    CHECK(dump.size > 1000);
    CHECK(std::memcmp(dump.data, "MDMP", 4) == 0);
    // The release string given at install time travels with the dump.
    CHECK(content.find("crash-test@1") != std::string::npos);
}

TEST_CASE("Installing the crash handler twice changes nothing the second time, and it can be uninstalled", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    const ChildResult r = RunInChild("dt_crash_install_twice", dir.Path());
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("FIRST=0"));            // Installed
    CHECK(r.StdoutContains("SECOND=1"));           // AlreadyInstalled
    CHECK(r.StdoutContains("INSTALLED=1"));
    CHECK(r.StdoutContains("AFTER-UNINSTALL=0"));
    // A normal install says nothing: the library's complaint about having no server address is filtered out.
    CHECK_FALSE(r.StdoutContains("LogCrash"));
    CHECK_FALSE(r.StdoutContains("DSN"));
}

TEST_CASE("After the crash handler is uninstalled a fault behaves as if it never was installed", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_uninstall_then_fault", dir.Path(), options);
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
    std::vector<std::string> envelopes;
    CollectEnvelopes(dir.Path(), &envelopes);
    CHECK(envelopes.empty());
}

TEST_CASE("A fault before the crash handler is installed leaves no dump and the original signal", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_crash_fault_without_handler");
    CHECK(r.terminated);
    CHECK(r.native_code == SIGSEGV);
}

TEST_CASE("A dump directory that cannot be created fails the install, logs, and the program runs on", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult r = RunInChild("dt_crash_bad_directory");
    REQUIRE(r.exited);
    CHECK(r.exit_code == 0);
    CHECK(r.StdoutContains("RESULT=3")); // DirectoryFailed
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.StdoutContains("PROGRAM-RAN-ON"));
    // The failure is logged as a warning on the crash channel.
    CHECK(r.StdoutContains("crash handler not installed"));
}

TEST_CASE("Without a directory in the options the dump goes under the user's state directory", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir state_home;
    const std::string state_env = std::string("XDG_STATE_HOME=") + state_home.Path();
    const char* env[] = {state_env.c_str(), "ENGINE_APP_NAME=engine-crash-test"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 2;
    options.timeout_ms = 30000;
    const ChildResult r = RunInChild("dt_crash_default_directory_then_fault", "", options);
    CHECK(r.StdoutContains("INSTALLED=0"));
    CHECK(r.terminated);
    std::vector<std::string> envelopes;
    CollectEnvelopes(std::string(state_home.Path()) + "/engine-crash-test/crashes", &envelopes);
    CHECK(envelopes.size() == 1);
}

TEST_CASE("The crash dump directory is created private to the user and named after the application", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir state_home;
    const std::string state_env = std::string("XDG_STATE_HOME=") + state_home.Path();
    const char* env[] = {state_env.c_str(), "ENGINE_APP_NAME=engine-crash-test"};
    ChildOptions options;
    options.extra_env = env;
    options.extra_env_count = 2;
    const ChildResult r = RunInChild("dt_crash_print_default_directory", "", options);
    REQUIRE(r.exited);
    CHECK(r.StdoutContains("OK=1"));
    const std::string expected = std::string("PATH=") + state_home.Path() + "/engine-crash-test/crashes\n";
    CHECK(r.StdoutContains(expected.c_str()));
    struct stat app_info;
    struct stat crashes_info;
    REQUIRE(::stat((std::string(state_home.Path()) + "/engine-crash-test").c_str(), &app_info) == 0);
    REQUIRE(::stat((std::string(state_home.Path()) + "/engine-crash-test/crashes").c_str(), &crashes_info) == 0);
    CHECK((app_info.st_mode & 0777) == 0700);
    CHECK((crashes_info.st_mode & 0777) == 0700);
    CHECK(S_ISDIR(crashes_info.st_mode));
}

// Changes the environment, so it runs in a child and never in the multithreaded test process.
ENGINE_DEATH_TEST_FN(dt_crash_directory_without_home) {
    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    char path[os::kMaxPathBytes] = "untouched";
    PutLine("FOUND", os::GetCrashDumpDirectory(path, sizeof(path)) ? 1 : 0);
    PutLine("EMPTY", path[0] == '\0' ? 1 : 0);
}

ENGINE_DEATH_TEST_FN(dt_crash_directory_buffer_too_small) {
    char tiny[4];
    PutLine("TINY", os::GetCrashDumpDirectory(tiny, sizeof(tiny)) ? 1 : 0);
    PutLine("ZERO", os::GetCrashDumpDirectory(tiny, 0) ? 1 : 0);
}

TEST_CASE("The crash dump directory is not found without a home or state directory, or without room for the path", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    const ChildResult none = RunInChild("dt_crash_directory_without_home");
    REQUIRE(none.exited);
    CHECK(none.StdoutContains("FOUND=0"));
    CHECK(none.StdoutContains("EMPTY=1"));
    const ChildResult small = RunInChild("dt_crash_directory_buffer_too_small");
    REQUIRE(small.exited);
    CHECK(small.StdoutContains("TINY=0"));
    CHECK(small.StdoutContains("ZERO=0"));
}

TEST_CASE("An application name that would leave the intended directory or be cut short is refused", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    // The state directory is one level inside the temporary directory, so a name that climbed out of it would land in the
    // temporary directory (which is removed at the end) and not somewhere shared like /tmp.
    CrashTempDir outer;
    const std::string state_home = std::string(outer.Path()) + "/state";
    REQUIRE(::mkdir(state_home.c_str(), 0700) == 0);
    const std::string state_env = "XDG_STATE_HOME=" + state_home;
    const std::string too_long = "ENGINE_APP_NAME=" + std::string(300, 'a');
    for (const char* bad : {"ENGINE_APP_NAME=..", "ENGINE_APP_NAME=.", "ENGINE_APP_NAME=a/b", "ENGINE_APP_NAME=../x"}) {
        const char* env[] = {state_env.c_str(), bad};
        ChildOptions options;
        options.extra_env = env;
        options.extra_env_count = 2;
        const ChildResult r = RunInChild("dt_crash_print_default_directory", "", options);
        INFO(bad);
        REQUIRE(r.exited);
        CHECK(r.StdoutContains("OK=0"));
    }
    const char* env_long[] = {state_env.c_str(), too_long.c_str()};
    ChildOptions long_options;
    long_options.extra_env = env_long;
    long_options.extra_env_count = 2;
    CHECK(RunInChild("dt_crash_print_default_directory", "", long_options).StdoutContains("OK=0"));
    // Nothing was created: not beside the state directory (where `..` would have put it), not inside it.
    struct stat info;
    CHECK(::stat((std::string(outer.Path()) + "/crashes").c_str(), &info) != 0);
    CHECK(::stat((state_home + "/crashes").c_str(), &info) != 0);
}

TEST_CASE("A private directory must be a real directory owned by the user, and is tightened to the owner only", "[crash][debug][linux]") {
    CrashTempDir dir;
    const std::string root = dir.Path();
    // An existing directory open to others is tightened.
    const std::string open_dir = root + "/open";
    REQUIRE(::mkdir(open_dir.c_str(), 0755) == 0);
    REQUIRE(::chmod(open_dir.c_str(), 0755) == 0);
    CHECK(os::EnsurePrivateDirectory(StringView(open_dir.c_str())));
    struct stat info;
    REQUIRE(::stat(open_dir.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    // A trailing slash makes no difference.
    CHECK(os::EnsurePrivateDirectory(StringView((open_dir + "/").c_str())));
    // A symbolic link is refused, even one that points at a good directory, with or without a trailing slash.
    const std::string link = root + "/link";
    REQUIRE(::symlink(open_dir.c_str(), link.c_str()) == 0);
    CHECK_FALSE(os::EnsurePrivateDirectory(StringView(link.c_str())));
    CHECK_FALSE(os::EnsurePrivateDirectory(StringView((link + "/").c_str())));
    // A file is refused.
    const std::string file = root + "/file";
    std::FILE* f = std::fopen(file.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fclose(f);
    CHECK_FALSE(os::EnsurePrivateDirectory(StringView(file.c_str())));
    // A directory the user cannot write to is refused (root can write anywhere, so the check is skipped for root).
    if (::geteuid() != 0) {
        const std::string read_only = root + "/readonly";
        REQUIRE(::mkdir(read_only.c_str(), 0500) == 0);
        CHECK_FALSE(os::EnsurePrivateDirectory(StringView(read_only.c_str())));
        ::chmod(read_only.c_str(), 0700); // so the temp directory can be removed
    }
}

ENGINE_DEATH_TEST_FN(dt_crash_install_into) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("RESULT", static_cast<int>(InstallCrashHandler(options)));
    PutLine("INSTALLED", CrashHandlerInstalled() ? 1 : 0);
}

TEST_CASE("The crash handler is not installed into a symbolic link or a directory that is not writable", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    const std::string target = std::string(dir.Path()) + "/target";
    REQUIRE(::mkdir(target.c_str(), 0700) == 0);
    const std::string link = std::string(dir.Path()) + "/link";
    REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);
    const ChildResult through_link = RunInChild("dt_crash_install_into", link.c_str());
    REQUIRE(through_link.exited);
    CHECK(through_link.StdoutContains("RESULT=3")); // DirectoryFailed
    CHECK(through_link.StdoutContains("INSTALLED=0"));
    if (::geteuid() != 0) {
        const std::string read_only = std::string(dir.Path()) + "/readonly";
        REQUIRE(::mkdir(read_only.c_str(), 0500) == 0);
        const ChildResult unwritable = RunInChild("dt_crash_install_into", read_only.c_str());
        CHECK(unwritable.StdoutContains("RESULT=3"));
        ::chmod(read_only.c_str(), 0700);
    }
}

TEST_CASE("EnsureDirectory creates missing parents, accepts an existing directory and refuses a file", "[crash][debug][linux]") {
    CrashTempDir dir;
    const std::string nested = std::string(dir.Path()) + "/a/b/c";
    CHECK(os::EnsureDirectory(StringView(nested.c_str()), 0700));
    struct stat info;
    REQUIRE(::stat(nested.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0700);
    CHECK(os::EnsureDirectory(StringView(nested.c_str()), 0700)); // already there
    const std::string file = std::string(dir.Path()) + "/a/file";
    std::FILE* f = std::fopen(file.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fclose(f);
    CHECK_FALSE(os::EnsureDirectory(StringView(file.c_str()), 0700));
    CHECK_FALSE(os::EnsureDirectory(StringView((file + "/below").c_str()), 0700));
    CHECK_FALSE(os::EnsureDirectory(StringView(""), 0700));
}

// ---- a crash must survive the next start ----------------------------------------------------------------------------------

// Installs the handler in the given directory and exits normally: what a program does on the launch after a crash.
ENGINE_DEATH_TEST_FN(dt_crash_install_and_exit) {
    CrashHandlerOptions options;
    options.dump_directory = argument;
    PutLine("INSTALLED", static_cast<int>(InstallCrashHandler(options)));
    UninstallCrashHandler();
}

TEST_CASE("A crash dump is still there after the program is started again", "[crash][death][debug][linux]") {
    ENGINE_REQUIRE_DEATH_TEST_SUPPORT();
    CrashTempDir dir;
    ChildOptions options;
    options.timeout_ms = 30000;
    const ChildResult crashed = RunInChild("dt_crash_install_then_fault", dir.Path(), options);
    REQUIRE(crashed.terminated);
    std::vector<std::string> before;
    CollectEnvelopes(dir.Path(), &before);
    REQUIRE(before.size() == 1);
    const std::string original = ReadFile(before[0]);
    REQUIRE(original.size() > 1000);

    // The next launch installs the handler again (twice, as a restart loop would), and must not lose the earlier dump.
    for (int launch = 0; launch < 2; ++launch) {
        const ChildResult restarted = RunInChild("dt_crash_install_and_exit", dir.Path(), options);
        REQUIRE(restarted.exited);
        CHECK(restarted.exit_code == 0);
        CHECK(restarted.StdoutContains("INSTALLED=0"));
    }
    std::vector<std::string> after;
    CollectEnvelopes(dir.Path(), &after);
    REQUIRE(after.size() == 1);
    const std::string kept = ReadFile(after[0]);
    crash_extract::Minidump dump;
    REQUIRE(crash_extract::FindMinidump(kept.data(), kept.size(), &dump) == crash_extract::EnvelopeResult::Ok);
    CHECK(std::memcmp(dump.data, "MDMP", 4) == 0);
    CHECK(dump.size > 1000);
}
