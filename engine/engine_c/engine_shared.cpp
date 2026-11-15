// The shared engine library's exports before the C API exists (Phase 23 Days 343 and 345).
//
// `engine_shared_probe` runs `count` jobs on a scheduler of four participants and returns how many ran, so a program linked
// against libengine proves that the job system, the allocator and the log work through the library boundary (Day 343).
//
// `engine_shared_state_probe` (Day 345) returns a table of the engine's singletons and of the few operations the boundary tests
// need: the addresses of the allocator, the await-path counters, the log's channel list and the global frame pool, a log call and
// the log's line count, an allocation and its release, a job run, a crash handler install and a crash inside a job. A plugin that
// links only the C API reads these through the table and sees the engine's one copy; a plugin that carries its own copy of the
// foundation sees another one (tests/engine_library/dup_singletons). Day 346 replaces both exports with `engine_get_api`.
#include "build_defs.h"
#include "debug/crash_handler.h"
#include "engine_c/engine_state_probe.h"
#include "debug/stack_trace.h"
#include "jobs/counter.h"
#include "jobs/frame_pool.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/log.h"
#include "mem/heap_allocator.h"
#include "sync/atomic.h"

using namespace engine_placeholder;

namespace
{

jobs::Job CountOne(Atomic<int>* ran)
{
    ran->FetchAdd(1);
    co_return;
}

jobs::Job Crash()
{
    *static_cast<volatile int*>(nullptr) = 1;
    co_return;
}

int RunJobs(int count)
{
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    jobs::Counter counter(scheduler);
    Atomic<int> ran{0};
    for (int i = 0; i < count; ++i)
    {
        jobs::Spawn(counter, CountOne(&ran));
    }
    scheduler.HelpUntil(counter);
    return ran.Load();
}

// The state table's functions: plain C-callable, no C++ type across the boundary.
unsigned long long LogLines()
{
    return LogLinesWritten();
}

void LogMessage(const char* text)
{
    Log(LogCore, LogVerbosity::Warn, StringView(text));
}

Atomic<long> g_live_blocks{0};

void* Allocate(unsigned long size)
{
    void* block = HeapAllocator::Default().Alloc(size, 16);
    if (block != nullptr)
    {
        g_live_blocks.FetchAdd(1);
    }
    return block;
}

void Release(void* block, unsigned long size)
{
    HeapAllocator::Default().Free(block, size, 16);
    g_live_blocks.FetchSub(1);
}

long LiveBlocks()
{
    return g_live_blocks.Load();
}

int InstallCrash(const char* directory)
{
    debug::InitDebugFacilities();
    debug::CrashHandlerOptions options;
    options.dump_directory = directory;
    const debug::CrashHandlerResult result = debug::InstallCrashHandler(options);
    return result == debug::CrashHandlerResult::Installed ? 1 : 0;
}

void UninstallCrash()
{
    debug::UninstallCrashHandler();
}

void CrashInJob()
{
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    jobs::Counter counter(scheduler);
    jobs::Spawn(counter, Crash());
    scheduler.HelpUntil(counter);
}

}  // namespace

extern "C"
{

ENGINE_API int engine_shared_probe(int count)
{
    return RunJobs(count);
}

ENGINE_API const struct engine_state_probe* engine_shared_state_probe(void)
{
    // Filled once (a thread-safe static initialization): every field is an address that never changes after the first call.
    static const engine_state_probe filled = {sizeof(engine_state_probe), &HeapAllocator::Default(),
                                              &jobs::Counter::PathStats(), LogChannel::First(),
                                              &jobs::FramePool::Global(), &LogLines, &LogMessage, &Allocate, &Release,
                                              &RunJobs, &InstallCrash, &UninstallCrash, &CrashInJob, &LiveBlocks};
    return &filled;
}

}  // extern "C"
