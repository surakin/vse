# Phase 2 / Day 29 — Participants, Topology & Worker Thread Pool

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 28 (Job coroutine type).

Implements worker model **A** (phase design decision **D1**): `N`
participants = `N-1` spawned workers + the main thread. Main is **not** a
permanent worker — it only runs jobs while waiting (Day 31's
`RunUntil`), so it stays free for the window/frame loop.

## Tasks

- [x] Query the logical CPU count the process may use via
  `sched_getaffinity` (count of the allowed mask — *logical* CPUs, by
  decision; no physical-core/SMT topology parsing). Expose the allowed
  CPU ids in order. Not `std::thread::hardware_concurrency`, which
  ignores the affinity mask.
- [x] Decide the participant count `N`: default = allowed logical CPUs;
  override with the `ENGINE_JOB_THREADS` env var (integer ≥ 1, clamped to
  a documented maximum such as 256; garbage/0 falls back to the default
  and logs a warning). The scheduler/pool constructor also takes an
  explicit `N` so tests never depend on the environment.
- [x] Participant 0 is the calling (main) thread, **not pinned and not
  looping**; participants 1..N-1 are spawned `std::thread`s (Phase 1 Day
  15, `PLAN.md` assumption #2). `N == 1` spawns no threads and must be a
  valid configuration (main does all work inside `RunUntil`).
- [x] Pin each spawned worker to a distinct allowed CPU (the allowed
  list excluding the first entry, which is left to main) using
  `pthread_setaffinity_np` on `std::thread::native_handle()` (or
  `sched_setaffinity` from inside the worker with its own tid). If there
  are more workers than allowed CPUs (an explicit oversubscribed `N`),
  wrap around and log that pinning is shared.
- [x] Give every participant a thread-local identity (index + a pointer
  to its participant record) and `IsParticipantThread()` /
  `CurrentParticipantIndex()` queries; non-participant threads (e.g.
  Day 34's I/O thread) report "not a participant".
- [x] Pool lifecycle only (no job execution yet): start threads, each
  registers itself and parks until told to exit; a shutdown request
  wakes and joins all of them with no leaks (ASan/TSan clean).
- [x] Verify pinning per thread via `/proc/self/task/<tid>/status`
  `Cpus_allowed_list` (the per-thread file; `/proc/self/status` is
  process-wide), in a test that starts a pool and checks each worker's
  list is exactly one CPU and the CPUs are pairwise distinct.
- [x] Commit.

**Done:** starting a pool of `N` participants logs `N-1` spawned workers
plus main; each spawned worker's `/proc/self/task/<tid>/status` shows a
single, distinct allowed CPU; `ENGINE_JOB_THREADS` overrides the default
(including `1`, with zero spawned threads); shutdown joins everything
clean under ASan and TSan.

## Post-implementation code review (2026-11-03, `/code-review high`)

Fixed: TLS identity restore now asserts destruction on the constructing
thread and in reverse order (+ test); `Join()` made idempotent/thread-safe
via atomic exchange and its contract (workers must exit first) documented;
`GetParticipant` bounds-asserted; `JobsLog` now has a printf format
attribute; pinning test SKIPs (with reason) if affinity is rejected;
tests fixed (oversubscribe clamp, `<string>`, 32-bit mask, default parked
body + stop test added).
Declined: `GetAllowedCpus` truncating at 256 — participants are capped at
256 anyway so CPUs beyond the first 256 are never needed; documented.
Pin failure already logs at Warn (the channel default).
