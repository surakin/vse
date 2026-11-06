# The Job System

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 2

## Source of truth

- `engine/jobs/job.h`
- `scheduler.h`
- `counter.h`
- `parallel_for.h`
- `async_io.h`
- `deque.h`
- `worker_pool.h`
- `topology.h`
- `frame_pool.h`
- `plans/phase-02-job-system.md (design decisions D1-D9)`

## API reference

Full reference: [The Job System](../reference/jobs.md).

- [[jobs::Job]] — the coroutine job type
- [[jobs::Scheduler]] — submission, workers and helping
- [[jobs::Counter]] — the join primitive
- [[jobs::ParallelFor]] — chunked fan-out
- [[jobs::ReadFileAsync]] — asynchronous file reads
- [[jobs::FramePool]] — coroutine frame allocation
- [[jobs::WorkerPool]] — pinned worker threads
- [[jobs::WorkStealingDeque]] — the per-participant queue

## Already known — seed material (verify against the code when writing)

- Jobs are C++20 coroutines returning `Job`; they start suspended; submitting transfers ownership; jobs return void
- Parameter-lifetime rule: job bodies take state by value or pointer to data that outlives the join; never capture-by-reference lambdas
- Participants = N-1 pinned workers + the main thread; main only runs jobs inside `HelpUntil`; N defaults to allowed logical CPUs, override with `ENGINE_JOB_THREADS`
- `Counter` is the join primitive: increment before awaiting, single awaiter, must outlive all decrements; waiters are requeued, never resumed inline
- `parallel_for`: function pointer + user pointer, chunk count capped at 2048
- Queue capacity: 4096-slot deque per participant + 4096 injection queue; both full is an assert (main gets backpressure)
- Join all outstanding work before destroying the Scheduler
- Async file reads run on a dedicated non-participant I/O thread; continuations resume on a participant
- Coroutine frames come from FramePool (size classes 128-2048 B, counted malloc fallback)

## Outline

### Mental model

_TODO_

### Writing a job

_TODO_

### Submitting and joining (Spawn, Counter, HelpUntil)

_TODO_

### parallel_for

_TODO_

### Waiting inside a job (co_await) vs on main (HelpUntil)

_TODO_

### Async file I/O

_TODO_

### Configuring the pool

_TODO_

### Rules and contracts (lifetime, capacity, shutdown)

_TODO_

### Common mistakes

_TODO_

### Debugging: sanitizers, watchdog pattern, replaying seeds

_TODO_

### Performance notes and measured scaling

_TODO_

### Internals (for contributors): deque, parking protocol, Counter state word

_TODO_

## When a job crashes

Every worker thread and the I/O thread register themselves with the crash handler at start (`worker-0`, `worker-1`, ..., `io`), so
a crash report names the thread that faulted and a stack overflow on a worker can still be handled. A failed `ENGINE_ASSERT` inside
a job prints its own trace, which includes the job's function, and then the crash report repeats the thread name and trace. To
show something of your own in every report (a job count, a queue depth), add a crash state entry with
[[debug::CrashStateCounter]] or [[debug::SetCrashStateText]]; see "Adding your subsystem's state" in
[Debugging](foundation-debugging.md). A thread you create yourself should call [[debug::RegisterThreadForCrashHandling]] with a name.
