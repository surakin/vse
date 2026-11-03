#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "jobs/topology.h"
#include "jobs/worker_pool.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace {

// Reads the Cpus_allowed_list line of /proc/self/task/<tid>/status.
// Returns false if the file/line is missing.
bool ReadCpusAllowedList(i32 tid, char* out, usize cap) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/status", tid);
    FILE* f = std::fopen(path, "r");
    if (f == nullptr) return false;
    char line[512];
    bool found = false;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "Cpus_allowed_list:", 18) == 0) {
            const char* p = line + 18;
            while (*p == ' ' || *p == '\t') ++p;
            usize n = std::strcspn(p, "\n");
            if (n >= cap) n = cap - 1;
            std::memcpy(out, p, n);
            out[n] = '\0';
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

struct EnvGuard {
    std::string saved;
    bool had;
    explicit EnvGuard(const char* value) {
        const char* old = std::getenv("ENGINE_JOB_THREADS");
        had = old != nullptr;
        if (had) saved = old;
        if (value != nullptr) setenv("ENGINE_JOB_THREADS", value, 1);
        else unsetenv("ENGINE_JOB_THREADS");
    }
    ~EnvGuard() {
        if (had) setenv("ENGINE_JOB_THREADS", saved.c_str(), 1);
        else unsetenv("ENGINE_JOB_THREADS");
    }
};

} // namespace

TEST_CASE("GetAllowedCpus reports a non-empty ascending list", "[jobs][pool]") {
    u32 cpus[kMaxParticipants];
    usize n = GetAllowedCpus(cpus, kMaxParticipants);
    REQUIRE(n >= 1);
    for (usize i = 1; i < n; ++i) REQUIRE(cpus[i] > cpus[i - 1]);
}

TEST_CASE("ParseParticipantOverride accepts, clamps, and rejects", "[jobs][pool]") {
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
    for (const char* bad : {"", "0", "-3", "abc", "4x", " "}) {
        warned = false;
        REQUIRE(ParseParticipantOverride(bad, 7, &warned) == 7);
        REQUIRE(warned);
    }
}

TEST_CASE("DefaultParticipantCount honors ENGINE_JOB_THREADS", "[jobs][pool]") {
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

TEST_CASE("A pool of one participant spawns no threads", "[jobs][pool]") {
    auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{1});
    REQUIRE(pool->ParticipantCount() == 1);
    REQUIRE(pool->SpawnedWorkerCount() == 0);
    REQUIRE(WorkerPool::IsParticipantThread());
    REQUIRE(WorkerPool::CurrentParticipantIndex() == 0);
}

TEST_CASE("Participant identity: main is 0, plain threads are not participants", "[jobs][pool]") {
    REQUIRE_FALSE(WorkerPool::IsParticipantThread());
    {
        auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{3});
        REQUIRE(WorkerPool::IsParticipantThread());
        REQUIRE(WorkerPool::CurrentParticipantIndex() == 0);
        bool other_is_participant = true;
        i32 other_index = 0;
        std::thread t([&] {
            other_is_participant = WorkerPool::IsParticipantThread();
            other_index = WorkerPool::CurrentParticipantIndex();
        });
        t.join();
        REQUIRE_FALSE(other_is_participant);
        REQUIRE(other_index == -1);
    }
    // Destroying the pool restores the previous (non-participant) identity.
    REQUIRE_FALSE(WorkerPool::IsParticipantThread());
}

namespace {
struct IdentityProbe {
    std::atomic<u32> seen_mask{0};
    std::atomic<int> mismatches{0};
};
void ProbeBody(void* user, Participant& self) {
    auto* probe = static_cast<IdentityProbe*>(user);
    const Participant* cur = WorkerPool::Current();
    if (cur != &self || WorkerPool::CurrentParticipantIndex() != static_cast<i32>(self.index)) {
        probe->mismatches.fetch_add(1);
    }
    probe->seen_mask.fetch_or(1u << self.index);
}
} // namespace

TEST_CASE("Each spawned worker registers a thread-local identity", "[jobs][pool]") {
    IdentityProbe probe;
    WorkerPool::Config cfg;
    cfg.participants = 4;
    cfg.worker_fn = ProbeBody;
    cfg.user = &probe;
    auto pool = std::make_unique<WorkerPool>(cfg);
    pool->Join();
    REQUIRE(probe.mismatches.load() == 0);
    REQUIRE(probe.seen_mask.load() == 0b1110); // workers 1..3, not main
}

TEST_CASE("Spawned workers are each pinned to one distinct CPU", "[jobs][pool]") {
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    if (allowed < 2) {
        SKIP("needs at least 2 allowed CPUs (one for main, one for a worker)");
    }
    usize n = allowed < 8 ? allowed : 8; // workers = n-1 <= allowed-1: all distinct
    auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{n});
    REQUIRE(pool->SpawnedWorkerCount() == n - 1);

    i32 seen_cpu[kMaxParticipants];
    for (usize i = 1; i < n; ++i) {
        const Participant& p = pool->GetParticipant(i);
        REQUIRE(p.tid > 0);
        if (p.cpu < 0) {
            SKIP("pthread_setaffinity_np was rejected here (restricted cpuset?); pinning cannot be verified");
        }
        REQUIRE(static_cast<u32>(p.cpu) != cpus[0]); // first CPU left to main
        char list[128];
        REQUIRE(ReadCpusAllowedList(p.tid, list, sizeof(list)));
        // Exactly one CPU: the list is that single number.
        char expected[16];
        std::snprintf(expected, sizeof(expected), "%d", p.cpu);
        REQUIRE(std::strcmp(list, expected) == 0);
        seen_cpu[i] = p.cpu;
        for (usize j = 1; j < i; ++j) REQUIRE(seen_cpu[j] != seen_cpu[i]);
    }
    // Main is not pinned.
    REQUIRE(pool->GetParticipant(0).cpu == -1);
}

TEST_CASE("Oversubscribed pools wrap CPUs and still start and stop", "[jobs][pool]") {
    u32 cpus[kMaxParticipants];
    usize allowed = GetAllowedCpus(cpus, kMaxParticipants);
    usize want = allowed + 3 < kMaxParticipants ? allowed + 3 : kMaxParticipants;
    auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{want});
    REQUIRE(pool->SpawnedWorkerCount() == want - 1);
    pool.reset(); // joins cleanly
}

TEST_CASE("Repeated start/stop cycles join cleanly", "[jobs][pool]") {
    for (int i = 0; i < 50; ++i) {
        auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{4});
        if (i % 2 == 0) pool->RequestStop();
    }
}

TEST_CASE("The default parked worker body wakes on RequestStop and Join is idempotent", "[jobs][pool]") {
    auto pool = std::make_unique<WorkerPool>(WorkerPool::Config{3});
    REQUIRE_FALSE(pool->StopRequested());
    pool->RequestStop();
    REQUIRE(pool->StopRequested());
    pool->Join();
    pool->Join();
}

TEST_CASE("Two overlapping pools keep the right identity when destroyed in reverse order", "[jobs][pool]") {
    auto a = std::make_unique<WorkerPool>(WorkerPool::Config{2});
    const Participant* before = WorkerPool::Current();
    {
        WorkerPool b(WorkerPool::Config{2});
        REQUIRE(WorkerPool::Current() != before);
    }
    REQUIRE(WorkerPool::Current() == before);
}
