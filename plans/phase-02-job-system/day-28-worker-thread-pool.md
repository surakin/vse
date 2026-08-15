# Phase 2 / Day 28 — Worker Thread Pool

**Phase:** [Phase 2 — Job System & Coroutines](../phase-02-job-system.md)
**Depends on:** Day 27 (Job coroutine type).

## Tasks

- [ ] Query physical core count via `sched_getaffinity` (or
  `/proc/cpuinfo` parsing as a fallback) — not libc
  `std::thread::hardware_concurrency`.
- [ ] Spawn one worker thread per physical core using `std::thread`
  directly (Phase 1 Day 15, `PLAN.md` assumption #2).
- [ ] Pin each worker to a distinct core via `sched_setaffinity` (Linux-
  specific; no portable `std::thread` API for affinity) using the
  native handle (`std::thread::native_handle()`, a `pthread_t` on
  Linux) where available.
- [ ] Register the calling (main) thread as a worker too, rather than
  leaving it idle while worker threads run jobs.
- [ ] Verify each worker's affinity via `/proc/self/status`'s
  `Cpus_allowed_list` (or the per-thread equivalent) after startup.
- [ ] Commit.

**Done:** launching the pool on an N-core machine logs N+1 active
workers (N spawned + main), each pinned to a distinct core, verified
via `/proc/self/status`'s `Cpus_allowed_list` per thread.
