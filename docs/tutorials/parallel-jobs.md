# Tutorial: Run Work in Parallel with Jobs

**Goal:** Fan work out over all cores, join it, and check the answer against the serial result.

**You need:** a built tree. Background: [Jobs](../guide/jobs.md).

## Steps

1. **Create a scheduler.** `jobs::Scheduler scheduler(jobs::Scheduler::Config{4, false});` makes a pool of four participants: the calling thread and three workers.
   Leave out the config to get one participant per CPU.
2. **Write a job as a free function** that returns `jobs::Job` and finishes with `co_return`. Pass what it needs as arguments: pointers to output slots that outlive the job, never references to locals that
   may disappear.
3. **Spawn and join with a counter.** `jobs::Counter counter(scheduler);` counts the jobs still running. `jobs::Spawn(counter, MyJob(...))` starts one, and
   `scheduler.HelpUntil(counter)` makes the calling thread run jobs until the count is zero. Join *before* the data the jobs use goes away.
4. **Sum an array with `ParallelForBlocking`.** It splits `[0, n)` into chunks and calls your lambda once per chunk on any participant. Add up inside the chunk and
   touch the shared total once per chunk, with an `Atomic`.
5. **Use `HelpUntil` on the main thread, `co_await` inside jobs.** A job that waits for other jobs says `co_await counter;`, which frees its worker to run something
   else. `HelpUntil` is for code that is not a job.
6. **Check against the serial answer.** The parallel result must equal a plain loop, every run.

## Complete code

```cpp compile run standalone
#include "containers/vector.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/parallel_for.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "sync/atomic.h"

using namespace vse;

// Step 2: a job. It writes its answer into a slot the caller keeps alive.
static jobs::Job Square(u64* out, u64 value) {
    *out = value * value;
    co_return;
}

int main() {
    jobs::Scheduler scheduler(jobs::Scheduler::Config{4, false});

    // Step 3: one job per number, joined with a counter.
    Vector<u64> squares;
    squares.Resize(100);
    {
        jobs::Counter counter(scheduler);
        for (u64 i = 0; i < 100; ++i) {
            jobs::Spawn(counter, Square(&squares[i], i));
        }
        scheduler.HelpUntil(counter);
    }
    u64 sum_of_squares = 0;
    for (usize i = 0; i < squares.Size(); ++i) {
        sum_of_squares += squares[i];
    }

    // Step 4: add up a big array in chunks.
    Vector<u32> data;
    data.Resize(1'000'000);
    for (usize i = 0; i < data.Size(); ++i) {
        data[i] = static_cast<u32>(i % 10);
    }
    Atomic<u64> parallel_total{0};
    auto add_chunk = [&](usize begin, usize end) {
        u64 partial = 0;
        for (usize i = begin; i < end; ++i) {
            partial += data[i];
        }
        parallel_total.FetchAdd(partial);
    };
    jobs::ParallelForBlocking(scheduler, data.Size(), 0, add_chunk);   // takes the body by lvalue: name it

    // Step 6: the serial answer.
    u64 serial_total = 0;
    for (usize i = 0; i < data.Size(); ++i) {
        serial_total += data[i];
    }

    os::Print("sum of squares 0..99 = %llu\n", static_cast<unsigned long long>(sum_of_squares));
    os::Print("parallel total = %llu\n", static_cast<unsigned long long>(parallel_total.Load()));
    os::Print("matches the serial loop: %s\n", parallel_total.Load() == serial_total ? "yes" : "NO");
    return 0;
}
```

## Expected output

```output
sum of squares 0..99 = 328350
parallel total = 4500000
matches the serial loop: yes
```

## Where next

- [Jobs](../guide/jobs.md): the lifetime rule, `co_await` between jobs, configuring the pool, debugging.
- [Read a File Asynchronously from a Job](async-file-io.md): wait for the disk without blocking a worker.
