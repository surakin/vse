# Phase 2 / Day 28 — Job Coroutine Type, Ownership & Completion

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 27 (`jobs` library, `FramePool`).

Settles the job's lifetime and completion semantics up front (phase
design decisions **D2**, **D3**) because Days 29-34 all build on them:
the original plan gave the promise no way to notify a `Counter` and never
said who destroys the frame.

## Tasks

- [ ] Define `Job` in `/engine/jobs/job.h`: the C++20 coroutine return
  type, with `promise_type` whose `initial_suspend` is `suspend_always`
  (a job does not start until scheduled), `get_return_object`, and
  `return_void`. `Job` is a move-only handle to a not-yet-started frame.
- [ ] Ownership rule (D2): submitting a `Job` to the scheduler (Day 31)
  transfers the frame to the scheduler, which destroys it when the job
  completes; a `Job` destroyed **without** being submitted destroys its
  never-started frame. No other ownership mode exists in this phase
  (`co_await`-ing a `Job` directly is out of scope — joining is done with
  `Counter`, Day 32).
- [ ] Completion hook: the promise holds an optional `Counter*`
  (`SetCompletion(Counter&)`, defined as a forward declaration here and
  implemented in Day 32). `final_suspend` returns a custom awaiter that,
  on completion, decrements that counter (which may reschedule a waiter)
  and then destroys the frame itself — never leaving a suspended frame
  for anyone to leak. Until Day 32 lands, the hook is exercised with a
  test-only callback.
- [ ] `promise_type::unhandled_exception()` → `ENGINE_ASSERT(false, …)`
  (exceptions are disabled project-wide; it must exist and must be
  unreachable). Confirm the toolchain actually accepts coroutines under
  `-fno-exceptions -fno-rtti` and record the compiler/version in the day
  file.
- [ ] `promise_type::operator new(size_t)`/`operator delete(void*,
  size_t)` allocate frames from Day 27's `FramePool` (never returns
  `nullptr`, so no `get_return_object_on_allocation_failure` is needed —
  state this in a comment).
- [ ] Document the **parameter-lifetime rule** in `job.h`: coroutine
  parameters are copied into the frame, references are not extended, and
  a lambda coroutine's captures live in the lambda object, not the frame.
  Job bodies therefore take everything by value or by pointer to data
  that outlives the join, and are free functions or take their state as
  arguments — never capture-by-reference lambdas.
- [ ] Tests: `Job Example() { co_return; }` runs when resumed by hand;
  `FramePool::Stats()` shows exactly one pooled allocation and one free
  for it (not the global default); a never-started `Job` destroyed
  without submission frees its frame (stats balance); a job that
  completes triggers the completion hook exactly once and frees its
  frame (stats balance); a frame larger than the top size class takes the
  fallback path and still balances. Run clean under ASan.
- [ ] Commit.

**Done:** `Job Example() { co_return; }` compiles and runs under
`-fno-exceptions -fno-rtti`; `FramePool` stats prove its frame was
pooled and that every frame (started, never-started, oversize) is freed
exactly once; the completion hook fires exactly once per completed job.
