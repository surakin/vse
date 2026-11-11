// The macOS half of the job system's platform interface (Phase 26 Day 305): thread names are set from inside the thread
// and read back, cut to 63 bytes on a UTF-8 boundary; the CPU list is every active logical CPU; pinning does not exist.
#include <catch2/catch_test_macros.hpp>

#include <pthread.h>
#include <sys/sysctl.h>

#include "jobs/detail/thread_platform.h"
#include "string_view.h"
#include "sync/thread.h"
#include "types.h"

using namespace engine_placeholder;

namespace
{

// Names a new thread from inside it and returns what the system then reports for it.
void NameAndReadBack(const char* name, char (&read_back)[128])
{
    read_back[0] = '\0';
    Thread t(
        [&]
        {
            jobs::detail::NameCurrentThread(name);
            ::pthread_getname_np(::pthread_self(), read_back, sizeof read_back);
        });
    t.Join();
}

}  // namespace

TEST_CASE("jobs on macOS: a thread names itself, and the name reads back", "[jobs][macos]")
{
    char got[128];
    NameAndReadBack("worker-3", got);
    CHECK(StringView(got) == StringView("worker-3"));
}

TEST_CASE("jobs on macOS: a long name is cut to 63 bytes, never inside a UTF-8 character", "[jobs][macos]")
{
    char got[128];
    // 62 ASCII bytes, then a 2-byte character (bytes 62-63): byte 63 would split it, so the cut keeps 62 bytes.
    char name[80];
    for (int i = 0; i < 62; ++i)
    {
        name[i] = 'a';
    }
    name[62] = '\xC3';
    name[63] = '\xA9';  // é
    name[64] = 'z';
    name[65] = '\0';
    NameAndReadBack(name, got);
    CHECK(StringView(got) == StringView(name, 62));
    // 61 ASCII bytes and the same character fit exactly in 63.
    name[61] = '\xC3';
    name[62] = '\xA9';
    name[63] = 'z';
    name[64] = '\0';
    NameAndReadBack(name, got);
    CHECK(StringView(got) == StringView(name, 63));
    // A plain ASCII name over the limit keeps 63 bytes.
    for (int i = 0; i < 79; ++i)
    {
        name[i] = 'b';
    }
    name[79] = '\0';
    NameAndReadBack(name, got);
    CHECK(StringView(got).Length() == 63);
}

TEST_CASE("jobs on macOS: every active CPU is allowed, and pinning is not available", "[jobs][macos]")
{
    int active = 0;
    size_t size = sizeof active;
    REQUIRE(::sysctlbyname("hw.activecpu", &active, &size, nullptr, 0) == 0);
    u32 cpus[256];
    const usize n = jobs::detail::QueryAllowedCpus(cpus, 256);
    REQUIRE(n == static_cast<usize>(active));
    for (usize i = 0; i < n; ++i)
    {
        CHECK(cpus[i] == i);
    }
    CHECK(n == Thread::HardwareConcurrency());  // the logical count, as the scheduler's default worker count assumes
    CHECK_FALSE(jobs::detail::CanPinThreads());
    CHECK_FALSE(jobs::detail::PinCurrentThread(0));
    CHECK(jobs::detail::CurrentThreadId() > 0);
}
