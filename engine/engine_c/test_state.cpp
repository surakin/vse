// The state table of the boundary tests (Phase 23 Days 343 and 345; Day 346 moves it behind the extension
// `engine.test_state` of the function table).
//
// A struct of the addresses of the engine's singletons (the allocator, the await-path counters, the log's channel list,
// the global frame pool) and of the few operations the tests of tests/engine_library need: a log call and the log's
// line count, an allocation and its release, a job run, a crash handler install and a crash inside a job. A plugin that
// links only the C API reads these and sees the engine's one copy; a plugin that carries its own copy of the foundation
// sees another one (tests/engine_library/dup_singletons). Compiled only when ENGINE_C_TEST_EXTENSION is ON (the default
// while the phase is under way); a release build of the library has no such table.
#include "build_defs.h"
#include "debug/crash_handler.h"
#include "engine_c/engine_internal.h"
#include "engine_c/engine_objects.h"
#include "engine_c/runtime.h"
#include "engine_c/surface.h"
#include "engine_c/engine_test_extension.h"
#include "engine_c/test_state.h"
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

int ObjectAcquire()
{
    return engine_c::EngineObjectAcquire() ? 1 : 0;
}

void ObjectRelease()
{
    engine_c::EngineObjectRelease();
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

int LoseSurfaceDevice(unsigned long long surface)
{
    engine_placeholder::engine_c::Runtime* runtime = engine_placeholder::engine_c::CurrentRuntime();
    engine_placeholder::engine_c::SurfaceHost* host = runtime != nullptr ? runtime->SurfacesIfCreated() : nullptr;
    if (host == nullptr)
    {
        return 0;
    }
    engine_handle handle;
    handle.value = surface;
    return host->InjectDeviceLoss(handle) == ENGINE_OK ? 1 : 0;
}

}  // namespace

namespace engine_placeholder::engine_c
{

const engine_test_extension* TestStateExtension()
{
    // Filled once (a thread-safe static initialization): every field is an address that never changes after the first
    // call.
    static const engine_test_extension filled = {sizeof(engine_test_extension),
                                                 &HeapAllocator::Default(),
                                                 &jobs::Counter::PathStats(),
                                                 LogChannel::First(),
                                                 &jobs::FramePool::Global(),
                                                 &LogLines,
                                                 &LogMessage,
                                                 &Allocate,
                                                 &Release,
                                                 &RunJobs,
                                                 &InstallCrash,
                                                 &UninstallCrash,
                                                 &CrashInJob,
                                                 &LiveBlocks,
                                                 &ObjectAcquire,
                                                 &ObjectRelease,
                                                 &LoseSurfaceDevice};
    return &filled;
}

}  // namespace engine_placeholder::engine_c
