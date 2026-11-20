#include "sync/atomic.h"
#include "cstr/format.h"
#include "os/console.h"
#include "sync/thread.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "test_env.h"
#include "test_text.h"
#include "build_defs.h"
#if ENGINE_PLATFORM_WINDOWS
#include "os/windows/win32.h"
#endif

#include "io/directory.h"
#include "io/file.h"
#include "jobs/topology.h"
#include "jobs/detail/thread_platform.h"
#include "jobs/worker_pool.h"

using namespace vse;
using namespace vse::jobs;

namespace
{

// True when the OS says thread `tid` may run on CPU `cpu` alone.
bool ThreadPinnedTo([[maybe_unused]] i32 tid, [[maybe_unused]] i32 cpu)
{  // both unused on macOS
#if ENGINE_PLATFORM_WINDOWS
   // CPU ids are group * 64 + bit (jobs/windows/thread_platform.cpp).
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(tid));
    if (thread == nullptr)
    {
        return false;
    }
    GROUP_AFFINITY affinity{};
    const BOOL got = GetThreadGroupAffinity(thread, &affinity);
    CloseHandle(thread);
    return got && affinity.Group == static_cast<WORD>(cpu / 64) && affinity.Mask == (KAFFINITY{1} << (cpu % 64));
#elif ENGINE_PLATFORM_LINUX
   // The Cpus_allowed_list line of /proc/self/task/<tid>/status: exactly one CPU, so the list is that single number.
    char path[64];
    FormatTo(path, sizeof(path), "/proc/self/task/%d/status", tid);
    io::Path status_path;
    Vector<u8> bytes;
    if (!io::Path::Parse(StringView(path), io::PathStyle::Posix, &status_path) ||
        io::ReadFile(status_path, &bytes, 1 << 16).Failed())
    {
        return false;
    }
    StringView rest(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    const StringView key("Cpus_allowed_list:");
    while (!rest.Empty())
    {
        const usize end = rest.IndexOf('\n');
        StringView line = rest.Substring(0, end);
        rest = end < rest.Length() ? rest.Substring(end + 1) : StringView();
        if (!line.StartsWith(key))
        {
            continue;
        }
        line = line.Substring(key.Length());
        while (!line.Empty() && (line[0] == ' ' || line[0] == '\t'))
        {
            line = line.Substring(1);
        }
        char expected[16];
        const int n = FormatTo(expected, sizeof(expected), "%d", cpu);
        return line == StringView(expected, static_cast<usize>(n));
    }
    return false;
#else
   // macOS: no affinity API, so no thread is ever pinned (the pinning test checks that no worker reports a CPU).
    return false;
#endif
}

struct EnvGuard
{
    engine_test::Text saved = engine_test::MakeText();
    bool had;
    explicit EnvGuard(const char* value)
    {
        const char* old = os::GetEnvironment("ENGINE_JOB_THREADS");
        had = old != nullptr;
        if (had)
        {
            saved = engine_test::MakeText(StringView(old));
        }
        if (value != nullptr)
        {
            test_support::SetEnv("ENGINE_JOB_THREADS", value);
        }
        else
        {
            test_support::UnsetEnv("ENGINE_JOB_THREADS");
        }
    }
    ~EnvGuard()
    {
        if (had)
        {
            test_support::SetEnv("ENGINE_JOB_THREADS", saved.CStr());
        }
        else
        {
            test_support::UnsetEnv("ENGINE_JOB_THREADS");
        }
    }
};

}  // namespace

TEST_CASE("GetAllowedCpus reports a non-empty ascending list", "[jobs][pool]")
{
    u32 cpus[kMaxParticipants];
    usize n = GetAllowedCpus(cpus, kMaxParticipants);
    REQUIRE(n >= 1);
    for (usize i = 1; i < n; ++i)
    {
        REQUIRE(cpus[i] > cpus[i - 1]);
    }
}

TEST_CASE("ParseParticipantOverride accepts, clamps, and rejects", "[jobs][pool]")
{
    bool warned = false;
    REQUIRE(ParseParticipantOverride(nullptr, 7, &warned) == 7);
    REQUIRE_FALSE(warned);
    REQUIRE(ParseParticipantOverride("1", 7, &warned) == 1);
    REQUIRE_FALSE(warned);
    REQUIRE(ParseParticipantOverride("12", 7, &warned) == 12);
    REQUIRE(ParseParticipantOverride("256", 7, &warned) == 256);
    REQUIRE_FALSE(warned);
    REQUIRE(ParseParticipantOverride("100000", 7, &warned) == kMaxParticipants);
    REQUIRE(warned);
    for (const char* bad : {"", "0", "-3", "abc", "4x", " "})
    {
        warned = false;
        REQUIRE(ParseParticipantOverride(bad, 7, &warned) == 7);
        REQUIRE(warned);
    }
}

TEST_CASE("DefaultParticipantCount honors ENGINE_JOB_THREADS", "[jobs][pool]")
{
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    {
        EnvGuard g(nullptr);
        REQUIRE(DefaultParticipantCount() == allowed);
    }
    {
        EnvGuard g("1");
        REQUIRE(DefaultParticipantCount() == 1);
    }
    {
        EnvGuard g("3");
        REQUIRE(DefaultParticipantCount() == 3);
    }
    {
        EnvGuard g("garbage");
        REQUIRE(DefaultParticipantCount() == allowed);
    }
}

TEST_CASE("A pool of one participant spawns no threads", "[jobs][pool]")
{
    auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{1});
    REQUIRE(pool->ParticipantCount() == 1);
    REQUIRE(pool->SpawnedWorkerCount() == 0);
    REQUIRE(WorkerPool::IsParticipantThread());
    REQUIRE(WorkerPool::CurrentParticipantIndex() == 0);
}

TEST_CASE("Participant identity: main is 0, plain threads are not participants", "[jobs][pool]")
{
    REQUIRE_FALSE(WorkerPool::IsParticipantThread());
    {
        auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{3});
        REQUIRE(WorkerPool::IsParticipantThread());
        REQUIRE(WorkerPool::CurrentParticipantIndex() == 0);
        bool other_is_participant = true;
        i32 other_index = 0;
        Thread t(
            [&]
            {
                other_is_participant = WorkerPool::IsParticipantThread();
                other_index = WorkerPool::CurrentParticipantIndex();
            });
        t.Join();
        REQUIRE_FALSE(other_is_participant);
        REQUIRE(other_index == -1);
    }
    // Destroying the pool restores the previous (non-participant) identity.
    REQUIRE_FALSE(WorkerPool::IsParticipantThread());
}

namespace
{
struct IdentityProbe
{
    Atomic<u32> seen_mask{0};
    Atomic<int> mismatches{0};
};
void ProbeBody(void* user, Participant& self)
{
    auto* probe = static_cast<IdentityProbe*>(user);
    const Participant* cur = WorkerPool::Current();
    if (cur != &self || WorkerPool::CurrentParticipantIndex() != static_cast<i32>(self.index))
    {
        probe->mismatches.FetchAdd(1);
    }
    probe->seen_mask.FetchOr(1u << self.index);
}
}  // namespace

TEST_CASE("Each spawned worker registers a thread-local identity", "[jobs][pool]")
{
    IdentityProbe probe;
    WorkerPool::Config cfg;
    cfg.participants = 4;
    cfg.worker_fn = ProbeBody;
    cfg.user = &probe;
    auto pool = MakeUnique<WorkerPool>(cfg);
    pool->Join();
    REQUIRE(probe.mismatches.Load() == 0);
    REQUIRE(probe.seen_mask.Load() == 0b1110);  // workers 1..3, not main
}

TEST_CASE("Spawned workers are each pinned to one distinct CPU", "[jobs][pool]")
{
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    if (allowed < 2)
    {
        SKIP("needs at least 2 allowed CPUs (one for main, one for a worker)");
    }
    usize n = allowed < 8 ? allowed : 8;  // workers = n-1 <= allowed-1: all distinct
    auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{n});
    REQUIRE(pool->SpawnedWorkerCount() == n - 1);
    if (!jobs::detail::CanPinThreads())
    {
        // A platform without an affinity API (macOS, Phase 21 Day 289): no worker is pinned, and none is reported as
        // pinned.
        for (usize i = 1; i < n; ++i)
        {
            REQUIRE(pool->GetParticipant(i).tid > 0);
            CHECK(pool->GetParticipant(i).cpu == -1);
        }
        return;
    }

    i32 seen_cpu[kMaxParticipants];
    for (usize i = 1; i < n; ++i)
    {
        const Participant& p = pool->GetParticipant(i);
        REQUIRE(p.tid > 0);
        if (p.cpu < 0)
        {
            SKIP("the OS rejected pinning here (restricted cpuset or affinity?); pinning cannot be verified");
        }
        REQUIRE(static_cast<u32>(p.cpu) != cpus[0]);  // first CPU left to main
        REQUIRE(ThreadPinnedTo(p.tid, p.cpu));
        seen_cpu[i] = p.cpu;
        for (usize j = 1; j < i; ++j)
        {
            REQUIRE(seen_cpu[j] != seen_cpu[i]);
        }
    }
    // Main is not pinned.
    REQUIRE(pool->GetParticipant(0).cpu == -1);
}

TEST_CASE("Oversubscribed pools wrap CPUs and still start and stop", "[jobs][pool]")
{
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    usize want = allowed + 3 < kMaxParticipants ? allowed + 3 : kMaxParticipants;
    auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{want});
    REQUIRE(pool->SpawnedWorkerCount() == want - 1);
    pool.Reset();  // joins cleanly
}

TEST_CASE("Repeated start/stop cycles join cleanly", "[jobs][pool]")
{
    for (int i = 0; i < 50; ++i)
    {
        auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{4});
        if (i % 2 == 0)
        {
            pool->RequestStop();
        }
    }
}

TEST_CASE("The default parked worker body wakes on RequestStop and Join is idempotent", "[jobs][pool]")
{
    auto pool = MakeUnique<WorkerPool>(WorkerPool::Config{3});
    REQUIRE_FALSE(pool->StopRequested());
    pool->RequestStop();
    REQUIRE(pool->StopRequested());
    pool->Join();
    pool->Join();
}

TEST_CASE("Two overlapping pools keep the right identity when destroyed in reverse order", "[jobs][pool]")
{
    auto a = MakeUnique<WorkerPool>(WorkerPool::Config{2});
    const Participant* before = WorkerPool::Current();
    {
        WorkerPool b(WorkerPool::Config{2});
        REQUIRE(WorkerPool::Current() != before);
    }
    REQUIRE(WorkerPool::Current() == before);
}
