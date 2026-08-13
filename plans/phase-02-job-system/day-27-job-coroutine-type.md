# Phase 2 / Day 27 — Job/Task Coroutine Type & promise_type

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Phase 1 complete (Day 5's pool allocator in particular).

## Tasks

- [ ] Define a `Job` return type usable as a C++20 coroutine return
  type in `/engine/jobs/job.h`, with a custom `promise_type`:
  `initial_suspend`, `final_suspend`, `get_return_object`.
- [ ] Implement `promise_type::unhandled_exception()` routing to Day
  9's assert/log path (no C++ exceptions per `PLAN.md` scope — this
  should never actually be reached in practice, but must be defined).
- [ ] Override `promise_type::operator new`/`operator delete` to
  allocate coroutine frames from a dedicated Foundation pool allocator
  (Phase 0 Day 5) sized for typical job-frame sizes, instead of the
  global `new`.
- [ ] Write a minimal coroutine `Job Example() { co_return; }`; confirm
  it compiles and runs.
- [ ] Instrument the pool allocator (e.g. a debug counter/tag) and
  confirm `Example()`'s frame allocation came from the pool, not the
  global default.
- [ ] Commit.

**Done:** a coroutine written as `Job Example() { co_return; }`
compiles and runs, and instrumenting the allocator confirms its frame
came from the pool, not the global default.
