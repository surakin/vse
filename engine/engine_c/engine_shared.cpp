// The shared engine library's one export before the C API exists (Phase 23 Day 343): runs `count` jobs on a scheduler
// of four participants and returns how many ran, so a program linked against libengine proves that the job system, the
// allocator and the log work through the library boundary.
#include "build_defs.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "sync/atomic.h"

using namespace engine_placeholder;

namespace
{

jobs::Job CountOne(Atomic<int>* ran)
{
    ran->FetchAdd(1);
    co_return;
}

}  // namespace

extern "C" ENGINE_API int engine_shared_probe(int count)
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
