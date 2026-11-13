# The Job System

Status: written (Phase 2; Windows pinning from Phase 13; macOS from Phase 26 Day 305).

**Audience:** every engine and game programmer. Read [Memory](foundation-memory.md) and [Containers](foundation-containers.md) first. You do not need to know
C++ coroutines in depth: this page shows the few patterns you write.

Modern CPUs have many cores, and a game that uses one of them leaves most of the machine idle. The engine runs everything that can be parallel (the ECS systems,
rendering command recording, asset loading, file I/O) through **one job system**, not a thread pool per feature. Learn it once and you can use it everywhere.

## Mental model

- A **job** is a small function that runs once, on any core. It is a C++20 coroutine that returns `jobs::Job`.
- The **scheduler** (`jobs::Scheduler`) owns a set of worker threads, one per CPU core, each pinned to its core where the system allows it (macOS has no affinity API, so its workers are unpinned). Together with your main thread they are the
  *participants*.
- You **submit** jobs; idle workers **steal** them from each other, so the load balances itself.
- To know when work is finished you use a **counter** (`jobs::Counter`): it counts the jobs still running, and you wait until it reaches zero.
- Waiting does not waste a core. The **main thread helps**: while it waits it runs queued jobs itself (`HelpUntil`). **Inside a job** you wait with `co_await`,
  which suspends the job and lets the worker run something else.

```
 main thread:  Spawn(counter, jobA) Spawn(counter, jobB) ...  HelpUntil(counter)   <- runs jobs while it waits
                      \                    \
 workers:         steal jobA          steal jobB     ... counter reaches 0 -> main continues
```

## Writing a job

A job is an ordinary function whose return type is `jobs::Job` and whose body ends in `co_return;`. Nothing runs when you call it: it only creates the
suspended job, which you hand to the scheduler.

```cpp
static jobs::Job Square(u64* out, u64 value) {
    *out = value * value;
    co_return;
}
```

Jobs return nothing. **Results travel through pointers to data that outlives the job**, such as an array the spawner keeps alive until it has waited.

### The lifetime rule (the one that bites)

A job usually runs *after* the function that created it has returned. So everything a job uses must still exist then:

- **Take everything by value, or as a pointer to data that outlives the job.** `out` above points into an array the caller keeps until after the join.
- **Never take a reference parameter**, and **never write a job as a lambda that captures by reference**. A reference parameter stays a reference to the caller's
  object, and a lambda coroutine's captures live in the lambda object, not in the job; both dangle as soon as the creating function returns.
- Write jobs as free functions (or static member functions).

## Submitting and joining

`jobs::Spawn(counter, job)` increments the counter, submits the job, and arranges for the counter to be decremented when the job finishes. `scheduler.HelpUntil(counter)`
runs jobs on the calling thread until the counter is zero.

```cpp compile run standalone
#include "containers/vector.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "os/console.h"

using namespace engine_placeholder;

static jobs::Job Square(u64* out, u64 value) {
    *out = value * value;
    co_return;
}

int main() {
    jobs::Scheduler scheduler(jobs::Scheduler::Config{4, false});   // 4 participants: this thread + 3 workers
    jobs::Counter counter(scheduler);

    Vector<u64> results;
    results.Resize(100);                          // alive until after the join

    for (u64 i = 0; i < 100; ++i) {
        jobs::Spawn(counter, Square(&results[i], i));
    }
    scheduler.HelpUntil(counter);                 // the main thread runs jobs too, until all are done

    u64 sum = 0;
    for (usize i = 0; i < results.Size(); ++i) {
        sum += results[i];
    }
    os::Print("sum of squares 0..99 = %llu\n", static_cast<unsigned long long>(sum));
    return 0;                                     // all work was joined before the scheduler is destroyed
}
```

```output
sum of squares 0..99 = 328350
```

Each job wrote only its own slot, so there is no data race and no lock. **Design jobs so they write to different memory** (their own slot, their own range)
and combine the results after the join. If jobs must share a variable, make it an `Atomic<T>` ([Synchronization](foundation-sync.md)).

## parallel_for: splitting a loop

Most parallel work is "do this to every element". `jobs::ParallelForBlocking` splits the range `[0, count)` into chunks (1024 iterations by default, at most 2048
chunks), runs each chunk as a job and returns when all are done. Your callable receives the half-open range of its chunk.

```cpp compile run standalone
#include "containers/vector.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "sync/atomic.h"

using namespace engine_placeholder;

int main() {
    jobs::Scheduler scheduler(jobs::Scheduler::Config{4, false});

    Vector<u32> data;
    data.Resize(1'000'000);
    for (usize i = 0; i < data.Size(); ++i) {
        data[i] = static_cast<u32>(i % 10);
    }

    Atomic<u64> total{0};
    auto body = [&](usize begin, usize end) {          // runs once per chunk, on any participant
        u64 partial = 0;
        for (usize i = begin; i < end; ++i) {
            partial += data[i];                        // the chunks only read `data`
        }
        total.FetchAdd(partial);                       // one atomic add per chunk, not per element
    };
    jobs::ParallelForBlocking(scheduler, data.Size(), 0, body);   // chunk size 0 = default

    os::Print("total = %llu\n", static_cast<unsigned long long>(total.Load()));
    return 0;
}
```

```output
total = 4500000
```

Here a lambda capturing by reference is **fine**, because the call blocks: the captured variables certainly outlive it. Accumulate into a local first (`partial`) and
touch the shared atomic once per chunk. Pick a chunk size so each chunk takes at least a few microseconds; tiny chunks cost more in scheduling than they save.

## Waiting inside a job: co_await

A job must never block a worker thread (`HelpUntil` is only for the main thread, outside any job). To wait for child jobs, create a `Counter` **inside the job**,
spawn the children, and `co_await` it: the job is suspended, the worker runs other jobs, and the job resumes when the counter reaches zero.

```cpp compile run standalone
#include "containers/vector.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "os/console.h"

using namespace engine_placeholder;

static jobs::Job Leaf(u64* out, u64 value) {
    *out = value + 1;
    co_return;
}

// Runs ten leaves, waits for them without blocking its worker, then adds them up.
static jobs::Job Parent(u64* result) {
    u64 leaves[10];                       // lives in this job's frame, so it survives the co_await
    jobs::Counter counter;                // bound to the scheduler running this job
    for (u64 i = 0; i < 10; ++i) {
        jobs::Spawn(counter, Leaf(&leaves[i], i));
    }
    co_await counter;                     // suspend until all ten are done
    u64 sum = 0;
    for (u64 v : leaves) {
        sum += v;
    }
    *result = sum;
}

int main() {
    jobs::Scheduler scheduler(jobs::Scheduler::Config{3, false});
    jobs::Counter done(scheduler);
    u64 result = 0;
    jobs::Spawn(done, Parent(&result));
    scheduler.HelpUntil(done);
    os::Print("parent result = %llu\n", static_cast<unsigned long long>(result));
    return 0;
}
```

```output
parent result = 55
```

The counter rules: increment *before* you wait (`Spawn` does it), at most one coroutine awaits a counter, and the counter must outlive the jobs that decrement it
(a local in the waiting job's frame does). A job that was waiting is never resumed in the middle of another job's stack; it is put back on a queue.

## Async file I/O

Reading a file blocks the thread for as long as the disk takes, which would stall a worker. The scheduler therefore has a dedicated **I/O thread**, and files can be
read, written and flushed with `co_await` from a job; the job is suspended meanwhile and resumes on a worker. `jobs::ReadFileAsync(scheduler, path)` reads a whole file;
`jobs::ReadAsync`, `WriteAsync`, `FlushAsync` and `ReadAllAsync` work on an open `io::File`:

```cpp
static jobs::Job LoadLevel(jobs::Scheduler* scheduler, Atomic<int>* done) {
    jobs::FileReadResult file = co_await jobs::ReadFileAsync(*scheduler, "level1.dat");
    if (file.ok) {
        // use file.data[0 .. file.size); the bytes are freed when `file` goes out of scope
    }
    done->Store(1);
}
```

A complete, runnable example is in [Files](io.md#asynchronous-io). There is one I/O thread and one queue, so a very slow read delays the ones behind it.

### Configuring the pool

The pool has one participant per logical CPU the process may use (`jobs::GetAllowedCpus`), or `ENGINE_JOB_THREADS` participants; workers are
pinned to one CPU each and the main thread is not. The CPU list, thread ids, pinning and names are the per-platform part
(`jobs/detail/thread_platform.h`, implemented in `jobs/linux/`, `jobs/windows/` and `jobs/macos/`):

- **Linux:** the affinity mask (`sched_getaffinity`), kernel thread ids, `pthread_setaffinity_np`.
- **Windows** (Phase 13 Day 153): CPU ids are `group * 64 + bit` over the processor groups the process may use
  (`GetLogicalProcessorInformationEx(RelationGroup)`, `GetProcessGroupAffinity`), restricted by the process affinity mask when the process has one
  group, so `start /affinity` is honoured; a machine with more than 64 logical processors has several groups, and the parser is tested with
  recorded multi-group data. Pinning is `SetThreadGroupAffinity`; workers are named `worker-N` with `SetThreadDescription`, which debuggers,
  profilers and crash dumps show.
- **macOS** (Phase 26 Day 305): every active logical CPU (`hw.activecpu`; there is no per-process mask). **Workers are not pinned**: macOS has no
  affinity API (`THREAD_AFFINITY_POLICY` is only a grouping hint the scheduler may ignore), so `CanPinThreads()` is false and the pool does not
  try, and a participant's `cpu` is `-1`. Thread ids are `pthread_threadid_np`'s low 31 bits. Workers name themselves `worker-N` with
  `pthread_setname_np`, which on macOS names only the calling thread (so it is called from inside each worker; at most 63 bytes, cut on a UTF-8
  boundary); Instruments, `lldb` and crash reports show it. Workers keep the default QoS class and the default 512 KiB stack of a secondary
  thread (coroutine frames live in the frame pool, not on the stack; the stress suite passes with it); neither was changed without a measured need.

### Rules and contracts

- **Join everything before the scheduler is destroyed.** A job suspended on a counter is in no queue, so shutdown cannot free it; it leaks. Destroy the
  scheduler on the thread that created it.
- **`HelpUntil` is for the main thread only**, and never from inside a job. In a job, use `co_await`.
- **Only participants can submit freely.** Each participant has a queue of 4096 jobs, plus a shared injection queue of 4096. Fan out wide trees carefully: if both are
  full the program aborts rather than losing work (the main thread gets backpressure and runs jobs to make room). `ParallelFor` caps itself at 2048 chunks for this reason.
- **A job must not block.** No sleeping, no waiting on a mutex another job holds, no blocking file calls. Waiting for children is `co_await`; waiting for I/O is an async call.
- **Jobs return nothing** and cannot be awaited directly; join with a `Counter`.
- **Exceptions are off**, so a job that fails reports through its result slot or a log line.

### Common mistakes

- **A reference parameter or a by-reference lambda as a job.** The data is gone when the job runs. Use free functions with value/pointer parameters.
- **Putting the result on the spawner's stack and returning before the join.** The job writes into a dead frame. Join first.
- **Calling `HelpUntil` inside a job.** It aborts. Use `co_await counter`.
- **Awaiting the same counter from two jobs**, or incrementing it after you started waiting.
- **Two jobs writing the same variable.** A data race; give each job its own slot or use `Atomic`.
- **Spawning one job per tiny item.** A million jobs of ten nanoseconds each is slower than the loop. Use `ParallelFor` and a sensible chunk size.
- **Forgetting to join** before the scheduler goes out of scope.

### Debugging: sanitizers, watchdog pattern, replaying seeds

- Run the job tests, and any test of your parallel code, under **ThreadSanitizer**: `scripts/test-phase.sh --affected --flavor tsan`. It finds a race even when the
  result happened to be right ([Building and testing](building-and-testing.md)).
- A hang is almost always a counter that never reaches zero (a job was never spawned or never finished). The tests wait with a **watchdog**: instead of waiting
  forever they help for a bounded time and abort with a message naming what they were waiting for; copy that pattern in your own tests.
- The stress test prints the random seed of a failing task graph; replay it with `ENGINE_STRESS_SEED=<seed>`.
- `ENGINE_JOB_THREADS=1` runs with a single participant (everything on the main thread inside `HelpUntil`), which makes a bug deterministic; a bug that disappears
  with one participant is a race.

### Performance notes and measured scaling

Windows PC (16 logical CPUs, MSVC, Debug build; Phase 13 Day 153): the `Throughput scales with participant count` table gave 1.00x, 2.00x, 3.18x and
11.37x for 1, 2, 4 and 16 participants. Waking an idle worker (`jobs_tests "[latency]"`, 10 000 wake-ups, a job submitted while the only worker
sleeps): median 72 us, 99th percentile 0.6 to 1.0 ms over two runs. Idle workers wait on a condition variable, which a notification wakes; timer
resolution (`timeBeginPeriod`) does not bound that, so it is not changed. The Linux numbers are taken at the Phase 13 close.

Development Mac (a 2019 MacBook Pro: 4-core i5-8257U, 8 logical CPUs, Apple clang, Debug build, workers unpinned; Phase 26 Day 305): 1.00x, 1.99x,
3.79x and 6.07x for 1, 2, 4 and 8 participants (the step from 4 to 8 is hyper-threading); waking an idle worker: median 26 to 27 us, 99th percentile
61 to 220 us over two runs. No target applies across machines.

### Internals (for contributors)

Each participant owns a fixed-size **work-stealing deque**: its owner pushes and pops at one end (newest first, which is cache-friendly), and thieves take from the
other end. The order of work acquisition is: own deque, the shared injection queue, then a bounded number of steals from random victims. A worker that finds nothing
yields a few times and then parks on a condition variable until a submission wakes it. A `Counter` is one atomic word that holds the count and a "waiter
present" state, so exactly one of the awaiter and the last decrementer resumes the waiting job. Coroutine frames come from `jobs::FramePool` (size classes 128 to 2048
bytes), not the heap. Source: `engine/jobs/` (`deque.h`, `scheduler.cpp`, `counter.h`, `frame_pool.h`) and the design notes in `plans/phase-02-job-system.md`.

## When a job crashes

Every worker thread and the I/O thread register themselves with the crash handler at start (`worker-0`, `worker-1`, ..., `io`), so
a crash report names the thread that faulted and a stack overflow on a worker can still be handled. A failed `ENGINE_ASSERT` inside
a job prints its own trace, which includes the job's function, and then the crash report repeats the thread name and trace. To
show something of your own in every report (a job count, a queue depth), add a crash state entry with
[[debug::CrashStateCounter]] or [[debug::SetCrashStateText]]; see "Adding your subsystem's state" in
[Debugging](foundation-debugging.md). A thread you create yourself should call [[debug::RegisterThreadForCrashHandling]] with a name.

Files can also be read, written and flushed with `co_await` through `jobs::ReadAsync`, `WriteAsync`, `FlushAsync` and `ReadAllAsync`; see [Asynchronous I/O](io.md#asynchronous-io).

`jobs::ReadFileAsync` keeps its signature but reads through `io::File` (opened and read on the I/O thread), not the removed `os::ReadFileToArena`.

The ECS runs its systems and the chunks of one system as jobs through this scheduler; see "Parallel systems" in [ECS](ecs.md).
