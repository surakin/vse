# Job System & Coroutines — Design Spec

**Date:** 2026-08-13
**Status:** Approved, not yet implemented (planning-only repo — see root `CLAUDE.md`)

## Problem

The engine is heavily multithreaded by design, but the plan currently only
has raw OS thread primitives (Phase 1 Day 20: `clone()`-based thread
creation, futex mutex/condvar). There is no portable job/task
abstraction, no work-stealing scheduler, and no coroutine support
anywhere in the plan. This gap was already flagged implicitly in
`plans/phase-16-ecs.md`'s note that "multithreaded system scheduling
depends on Phase 1's threading primitives; confirm they're sufficient or
need extending" — they are not sufficient on their own; every consumer
(ECS parallel system scheduling, renderer command recording, physics,
audio, asset streaming) needs a shared, portable way to fan out and join
work without hand-rolling its own thread pool.

## Decision: new Phase 2 — Job System & Coroutines

Inserted between Platform (Phase 1) and Vulkan RHI. This mirrors the
existing pattern in the plan: Platform owns the raw, genuinely
OS-specific primitives (Wayland/X11, raw syscalls, `clone()`/futex);
higher layers built on those primitives that are themselves portable
algorithms get their own layer, the same relationship RHI already has to
Platform's raw Vulkan driver-loading (Day 22).

**Renumbering:** every phase from the current Phase 2 onward shifts up by
one:

| Old | New | Phase |
|---|---|---|
| 2 | 3 | Vulkan RHI |
| 3 | 4 | Renderer core |
| 4 | 5 | ECS |
| 5 | 6 | Physics |
| 6 | 7 | Audio |
| 7 | 8 | Asset pipeline |
| 8 | 9 | Scripting (Lua/LuaJIT) |
| 9 | 10 | Windows port + DX12 |
| 10 | 11 | macOS/iOS + Metal |
| 11 | 12 | Android |
| 12 | 13 | Consoles |
| 13 | 14 | Unreal asset & level import |
| 14 | 15 | Unreal Blueprint import |
| 15 | 16 | Editor tooling |
| 16 | 17 | First playable demo |
| 17 | 18 | Feature-parity push |

Updated architecture-layers diagram (`PLAN.md`):

```
Game / Demo content
Editor tooling
Unreal Blueprint import        --> targets Scripting (Lua/LuaJIT)
Scripting (Lua/LuaJIT first, pluggable language backends)
Unreal asset & level import    --> targets Asset pipeline / ECS / Renderer
Physics | Audio | Asset Pipeline
ECS
Renderer core
RHI  (Vulkan | DX12 | Metal)
Job System (work-stealing scheduler, jobs-as-coroutines)
Platform layer (window, input, raw threads, files, time)
Foundation (allocators, containers, string, math, assert)
Raw syscalls / OS ABI
```

Because this phase is next-in-line for implementation (immediately after
Platform), it gets day-by-day tasks with concrete done-conditions, like
Phases 0 and 1, rather than the goal-level treatment given to phases
further out.

## Design

### Worker pool

One OS thread per physical core (created via Phase 1's `clone()`
primitive), pinned to a core where the OS exposes affinity control. The
main thread participates as a worker rather than idling while waiting on
a frame's jobs.

### Work-stealing deques

Each worker owns a lock-free double-ended queue (Chase-Lev style) holding
type-erased `coroutine_handle<>` values (see below). A worker
pushes/pops its own end (LIFO — cache-friendly for depth-first job
chains); idle workers steal from the opposite end of a random peer's
deque (FIFO — minimizes contention with the owner).

### Jobs as C++20 coroutines — the sole suspension mechanism

Jobs are authored as C++20 coroutines (a `Job`/`Task` return type with a
custom `promise_type`), not plain function pointers. This was chosen over
stackful fibers specifically to avoid any hand-written, per-ABI
context-switching code — stackful fiber switching requires manipulating
raw register/stack state at some level, which is either hand-rolled
assembly (rejected — the whole point of this phase is portability) or a
wrapped OS-native facility (`ucontext` on POSIX, Win32 Fibers on Windows,
with macOS/iOS support unresolved). Stackless coroutines are pure
standard C++20: the compiler generates the suspend/resume state machine,
so there is nothing OS- or ABI-specific anywhere in this phase.

- **Submitting a job** schedules its initial `coroutine_handle` onto a
  worker's deque.
- **Counters/dependencies:** waiting on N sub-jobs is `co_await` on a
  `Counter` awaitable. If the counter isn't zero, `await_suspend()`
  registers the coroutine's handle as the counter's continuation and
  returns control to the scheduler loop, which immediately pops the next
  ready handle from the worker's deque — no OS thread ever blocks on a
  job dependency. The sub-job that decrements the counter to zero
  reschedules the waiting continuation onto some worker's deque.
- **Async I/O** (first real consumer: Phase 13's asset streaming) uses the
  identical mechanism — `co_await LoadTextureAsync(path)` suspends until
  the I/O completes, then resumes as a scheduled job. One suspension
  primitive serves both job-graph waits and async I/O.
- **Allocation:** `promise_type::operator new/delete` route coroutine-frame
  allocation through a Foundation pool allocator sized for typical
  job-frame sizes, instead of the global default `new` — keeps the
  no-malloc-by-default rule intact.

### What this deliberately excludes

- No stackful fibers, no fiber stack pool, no per-ABI context-switch code.
- No general-purpose thread pool separate from the job system — everything
  routes through the same scheduler so there's one mechanism to reason
  about, not two.

## Testing

Matches the Done-condition style already used in Phases 0/1 — concrete
and checkable, not aspirational:

- **Stress/correctness:** thousands of jobs with randomized dependency
  chains, run repeatedly under thread/address sanitizers, with no
  races/deadlocks/lost wakeups across repeated runs.
- **Work-stealing throughput:** skew load onto one worker; verify other
  workers steal work and total throughput scales with core count.
- **Coroutine-frame correctness:** pool-allocator round-trip under churn
  (no leaks/corruption); a chain of nested `co_await`s resolves in the
  correct order.

## Plan-file changes required

1. Rename `plans/phase-02..17-*.md` → `plans/phase-03..18-*.md` (shift by
   one), and create new `plans/phase-02-job-system.md` with day-by-day
   tasks following the Phase 0/1 format.
2. `PLAN.md`: architecture-layers diagram, repo-layout section (add
   `/engine/jobs`), phase roadmap table, and a new assumption #15
   documenting this decision (job system as its own layer, work-stealing
   scheduler, jobs-as-coroutines as the sole suspension mechanism,
   fibers considered and rejected for portability, x86-64-first per
   existing ABI scoping via assumption #7).
3. `CLAUDE.md`: update the "phase-00…17" range reference to "phase-00…18".
4. Renumbered `plans/phase-16-ecs.md` (was phase-04): rewrite the note
   about depending on "Phase 1's threading primitives" to reference the
   new phase's `Job`/`Counter` API instead.
5. Sweep all renumbered phase files' "Depends on" lines and any
   cross-phase mentions (e.g. Unreal import phases, multiplatform phases)
   for old phase numbers.
