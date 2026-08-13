# Phase 2 / Day 33 — Async I/O Integration

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 31 (Counter/await mechanism).

## Tasks

- [ ] Implement a `co_await`-able `ReadFileAsync(path)` in
  `/engine/jobs/async_io.h`, built on Phase 1 Day 21's file I/O.
- [ ] Dispatch the actual (synchronous) read onto a dedicated I/O job
  rather than running it on a calling compute worker, so a slow read
  doesn't block other jobs on that worker.
- [ ] Deliver the result back to the awaiting coroutine via Day 31's
  Counter/continuation mechanism (not a raw OS callback thread).
- [ ] Write a test: `co_await ReadFileAsync(path)` inside a job returns
  the correct file contents for a known test file.
- [ ] Log the resuming thread's ID when the awaiting coroutine resumes;
  confirm it's always one of the registered pool workers (Day 28),
  never an ad hoc OS thread.
- [ ] Commit.

**Done:** `co_await ReadFileAsync(path)` inside a job returns the
correct file contents, and the awaiting coroutine resumes on a
job-system worker.
