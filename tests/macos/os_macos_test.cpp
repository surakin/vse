// The macOS os backend (Phase 21 Day 285): what os/macos/os.cpp and session.mm do differently from Linux, and what only
// a macOS run can show (the clock across threads, the shortest sleeps, the page size, the process figures, library
// loading without the current directory, arguments and log text through a child's pipe).
#include <catch2/catch_test_macros.hpp>

#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cstr/mem_ops.h"
#include "log/output.h"
#include "os/console.h"
#include "os/last_error.h"
#include "os/os.h"
#include "os/session.h"
#include "string_view.h"
#include "sync/atomic.h"
#include "sync/mutex.h"
#include "sync/thread.h"
#include "types.h"

using namespace vse;

TEST_CASE("macOS: MonotonicTimeNanos never goes backwards across threads", "[os][timing]")
{
    // Every reading is taken and compared under one lock, so the readings are in a single order: each must be at least
    // the one before, whichever thread took it.
    Mutex lock;
    u64 last = 0;
    Atomic<int> backwards{0};
    Thread threads[4];
    for (Thread& t : threads)
    {
        t = Thread(
            [&]
            {
                for (int i = 0; i < 50000; ++i)
                {
                    LockGuard guard(lock);
                    const u64 now = os::MonotonicTimeNanos();
                    if (now < last)
                    {
                        backwards.FetchAdd(1);
                    }
                    last = now;
                }
            });
    }
    for (Thread& t : threads)
    {
        t.Join();
    }
    CHECK(backwards.Load() == 0);
}

TEST_CASE("macOS: SleepNanos never returns early; the shortest real sleeps", "[os][timing]")
{
    for (const u64 requested : {1'000ULL, 100'000ULL, 1'000'000ULL})
    {
        u64 shortest = ~0ULL;
        for (int i = 0; i < 20; ++i)
        {
            const u64 start = os::MonotonicTimeNanos();
            os::SleepNanos(requested);
            const u64 slept = os::MonotonicTimeNanos() - start;
            if (slept < shortest)
            {
                shortest = slept;
            }
        }
        out::Print("SleepNanos(%llu ns): shortest of 20 was %llu ns\n", static_cast<unsigned long long>(requested),
                   static_cast<unsigned long long>(shortest));
        CHECK(shortest >= requested);
    }
}

TEST_CASE("macOS: ReserveMemory gives zeroed pages aligned to the system page size", "[os][memory]")
{
    const usize page = static_cast<usize>(::getpagesize());
    CHECK((page == 4096 || page == 16384));  // x86-64 and Apple silicon
    const usize size = 3 * page + 1;
    auto* const bytes = static_cast<u8*>(os::ReserveMemory(size));
    REQUIRE(bytes != nullptr);
    CHECK(reinterpret_cast<usize>(bytes) % page == 0);
    bool zero = true;
    for (usize i = 0; i < size; ++i)
    {
        zero = zero && bytes[i] == 0;
    }
    CHECK(zero);
    bytes[size - 1] = 1;  // the whole size is writable
    os::ReleaseMemory(bytes, size);
}

TEST_CASE("macOS: ResidentMemoryKiB and OpenHandleCount follow the process", "[os][process]")
{
    const i64 before = os::ResidentMemoryKiB();
    CHECK(before > 0);
    constexpr usize kSize = 64u << 20;
    auto* const bytes = static_cast<u8*>(os::ReserveMemory(kSize));
    REQUIRE(bytes != nullptr);
    MemSet(bytes, 1, kSize);  // touched, so resident
    CHECK(os::ResidentMemoryKiB() >= before + 32 * 1024);
    os::ReleaseMemory(bytes, kSize);

    const i64 handles = os::OpenHandleCount();
    CHECK(handles >= 3);  // standard input, output and error at least
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    CHECK(os::OpenHandleCount() == handles + 2);
    os::Close(fds[0]);
    os::Close(fds[1]);
    CHECK(os::OpenHandleCount() == handles);
}

TEST_CASE("macOS: LoadLibrary takes absolute paths and bare names, never a relative directory", "[os][library]")
{
    os::DynamicLibraryHandle system = os::LoadLibrary("/usr/lib/libSystem.B.dylib");
    REQUIRE(system != nullptr);
    CHECK(os::GetSymbol(system, "getpid") != nullptr);
    CHECK(os::GetSymbol(system, "engine_no_such_symbol") == nullptr);
    os::UnloadLibrary(system);

    os::DynamicLibraryHandle by_name = os::LoadLibrary("libSystem.B.dylib");  // found in /usr/lib
    CHECK(by_name != nullptr);
    os::UnloadLibrary(by_name);

    CHECK(os::LoadLibrary("./libSystem.B.dylib") == nullptr);
    CHECK(os::LoadLibrary("lib/libSystem.B.dylib") == nullptr);
    CHECK(os::LoadLibrary("libengine_test_should_not_exist.dylib") == nullptr);
    CHECK(os::LoadLibrary("") == nullptr);
    char long_path[os::kMaxPathBytes + 8];
    MemSet(long_path, 'a', sizeof long_path);
    long_path[0] = '/';
    CHECK(os::LoadLibrary(StringView(long_path, sizeof long_path)) == nullptr);
}

TEST_CASE("macOS: the session query answers, and Unknown outside a window-server session", "[os][session]")
{
    const os::SessionState state = os::QuerySessionState();
    out::Print("session state: %s\n", os::SessionStateName(state));
    // Over ssh the process is in no window-server session: Unknown (to be checked by hand, KNOWN-ISSUES.md). At the
    // desk: active, locked, idle or inactive. A second query may differ (the screen can lock between them), so only its
    // being one of the states is checked.
    for (const os::SessionState s : {state, os::QuerySessionState()})
    {
        CHECK((s == os::SessionState::Active || s == os::SessionState::Locked || s == os::SessionState::Idle ||
               s == os::SessionState::Inactive || s == os::SessionState::Unknown));
    }
}

TEST_CASE("macOS: a child reads non-ASCII, quoted and backslashed arguments and logs UTF-8 to a pipe unchanged",
          "[os][command_line]")
{
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    ::posix_spawn_file_actions_addclose(&actions, fds[1]);
    const char* const args[] = {ENGINE_TEST_CHILD,
                                "h\xC3\xA9llo w\xC3\xB6rld",
                                "a\"b'c",
                                "back\\slash\\",
                                "\xE6\x97\xA5\xE6\x9C\xAC",
                                "",
                                nullptr};
    pid_t pid = 0;
    const int started = ::posix_spawn(&pid, ENGINE_TEST_CHILD, &actions, nullptr, const_cast<char* const*>(args),
                                      *::_NSGetEnviron());
    ::posix_spawn_file_actions_destroy(&actions);
    os::Close(fds[1]);
    REQUIRE(started == 0);
    char out[1024];
    usize n = 0;
    for (;;)
    {
        const long got = os::Read(fds[0], out + n, sizeof out - 1 - n);
        if (got < 0 && os::LastError() == EINTR)
        {
            continue;
        }
        if (got <= 0)
        {
            break;
        }
        n += static_cast<usize>(got);
    }
    os::Close(fds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && os::LastError() == EINTR)
    {
    }
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    const StringView output(out, n);
    const StringView expected_args("h\xC3\xA9llo w\xC3\xB6rld\na\"b'c\nback\\slash\\\n\xE6\x97\xA5\xE6\x9C\xAC\n\n");
    REQUIRE(output.Length() > expected_args.Length());
    CHECK(output.Substring(0, expected_args.Length()) == expected_args);
    const StringView log_line = output.Substring(expected_args.Length(), output.Length() - expected_args.Length());
    CHECK(log_line.Find(StringView("gr\xC3\xBC\xC3\x9F"
                                   "e \xE6\x97\xA5\xE6\x9C\xAC \xE2\x9C\x93")) < log_line.Length());
    CHECK(log_line[log_line.Length() - 1] == '\n');
}

TEST_CASE("macOS: WriteAllWithTimeout writes to a device poll does not support", "[os][descriptor]")
{
    // macOS's poll answers POLLNVAL for /dev/null (Linux: POLLOUT); a crash report to a stderr on /dev/null must still
    // count as written (Day 285 review).
    const int fd = os::Open("/dev/null", O_WRONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    char bytes[2000];
    MemSet(bytes, 'x', sizeof bytes);
    CHECK(os::WriteAllWithTimeout(fd, bytes, sizeof bytes, 100'000'000) == static_cast<long>(sizeof bytes));
    os::Close(fd);
    CHECK(os::WriteAllWithTimeout(fd, bytes, sizeof bytes, 0) == 0);  // a closed descriptor is still a failure
}
