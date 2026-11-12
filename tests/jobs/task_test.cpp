#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "jobs/counter.h"
#include "jobs/scheduler.h"
#include "jobs/task.h"
#include "sync/atomic.h"
#include "sync/thread.h"

using namespace engine_placeholder;
using namespace engine_placeholder::jobs;

namespace
{

UniquePtr<Scheduler> TaskTestMakeScheduler(usize participants)
{
    Scheduler::Config cfg;
    cfg.participants = participants;
    return MakeUnique<Scheduler>(cfg);
}

Task<int> TaskTestConstant(int value)
{
    co_return value;
}

Task<int> TaskTestAdd(int a, int b)
{
    const int x = co_await TaskTestConstant(a);
    const int y = co_await TaskTestConstant(b);
    co_return x + y;
}

// A chain of n nested awaits: the deepest returns 1 and every level adds 1.
Task<int> TaskTestChain(int n)
{
    if (n == 0)
    {
        co_return 0;
    }
    const int below = co_await TaskTestChain(n - 1);
    co_return below + 1;
}

struct TaskMoveOnlyValue
{
    int serial = 0;
    UniquePtr<int> owned;
};

Task<TaskMoveOnlyValue> TaskTestMakeMoveOnly(int serial)
{
    TaskMoveOnlyValue m;
    m.serial = serial;
    m.owned = MakeUnique<int>(serial * 2);
    co_return static_cast<TaskMoveOnlyValue&&>(m);
}

struct TaskOutcome
{
    int value = 0;
    bool on_participant = false;
    Atomic<int> done{0};
};

Job TaskTestRunAdd(TaskOutcome* out, int a, int b)
{
    out->value = co_await TaskTestAdd(a, b);
    out->on_participant = WorkerPool::IsParticipantThread();
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job TaskTestRunChain(TaskOutcome* out, int depth)
{
    out->value = co_await TaskTestChain(depth);
    out->done.FetchAdd(1, MemoryOrder::Release);
}

Job TaskTestRunMoveOnly(TaskOutcome* out)
{
    TaskMoveOnlyValue m = co_await TaskTestMakeMoveOnly(21);
    out->value = m.serial * 100 + *m.owned;
    out->done.FetchAdd(1, MemoryOrder::Release);
}

void TaskTestWaitFor(Scheduler& s, TaskOutcome& out)
{
    s.HelpUntil([](void* u) { return static_cast<TaskOutcome*>(u)->done.Load(MemoryOrder::Acquire) != 0; }, &out);
}

}  // namespace

TEST_CASE("A task returns its value to the awaiting job, nested tasks included", "[jobs][task]")
{
    for (usize n : {usize{1}, usize{4}})
    {
        auto s = TaskTestMakeScheduler(n);
        TaskOutcome out;
        s->Submit(TaskTestRunAdd(&out, 20, 22));
        TaskTestWaitFor(*s, out);
        CHECK(out.value == 42);
        CHECK(out.on_participant);
    }
}

TEST_CASE("A move-only value comes out of a task intact", "[jobs][task]")
{
    auto s = TaskTestMakeScheduler(2);
    TaskOutcome out;
    s->Submit(TaskTestRunMoveOnly(&out));
    TaskTestWaitFor(*s, out);
    CHECK(out.value == 21 * 100 + 42);
}

TEST_CASE("A deep chain of awaited tasks completes (the hand-back is a transfer, not a call)", "[jobs][task]")
{
    auto s = TaskTestMakeScheduler(2);
    TaskOutcome out;
    s->Submit(TaskTestRunChain(&out, 2000));
    TaskTestWaitFor(*s, out);
    CHECK(out.value == 2000);
}

TEST_CASE("A task that was never awaited frees its frame without running", "[jobs][task]")
{
    int ran = 0;
    {
        // The body would write through the pointer; a lazy task has not started, and destroying it must not run it.
        struct Local
        {
            static Task<int> Body(int* flag)
            {
                *flag = 1;
                co_return 1;
            }
        };
        Task<int> unstarted = Local::Body(&ran);
        [[maybe_unused]] Task<int> moved = static_cast<Task<int>&&>(unstarted);
    }
    CHECK(ran == 0);
}
