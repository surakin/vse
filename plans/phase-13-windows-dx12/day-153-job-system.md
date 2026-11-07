# Phase 13 / Day 153 — The Job System on Windows

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 152 (asynchronous I/O), Phase 2's scheduler.

The scheduler and coroutines are portable C++20; what is Linux-specific is the CPU topology, thread affinity and naming in `jobs/topology.cpp` and
`jobs/worker_pool.cpp`. Port those, then run the jobs suite and its stress tests on Windows.

## Tasks

- [ ] **Topology:** `jobs/topology.cpp` reads the process's usable CPUs with `GetLogicalProcessorInformationEx(RelationAll)` (cores, SMT siblings, caches,
      processor groups) and the process affinity; the Linux `sched_getaffinity` code moves behind a per-platform function so the shared part has no
      `#include <sched.h>`; machines with more than 64 logical processors (several processor groups) are handled by design (the PC has 16; a test feeds the
      parser a recorded multi-group buffer).
- [ ] **Worker threads:** affinity with `SetThreadGroupAffinity`, names with `SetThreadDescription` (UTF-16), priorities only if Linux sets them; the
      thread id for logs from `os::`/`debug::CurrentThreadId`; `pthread`/`syscall` includes removed from shared code.
- [ ] **Timer resolution and sleeping workers:** idle workers on Windows wait on the same primitive as Linux (`std::condition_variable`/`atomic::wait`);
      measure wake latency (median and 99th percentile of 10 000 wake-ups) on both machines and record it; no `timeBeginPeriod` unless the measurement asks for it.
- [ ] **The jobs suite** (`tests/jobs`): everything passes on Windows, including the stress tests (dependency chains, work stealing, frame pool, async I/O
      through the Day 152 completion port); the tsan canary is not built on Windows (Linux-only, reported by the flavor summary).
- [ ] **Throughput:** run the Phase 2 benchmarks on Windows and record them beside the Linux numbers (different CPUs: the comparison is scaling with core
      count, not absolute speed).
- [ ] **Documentation:** `docs/guide/jobs.md` notes Windows topology and naming; reference regenerated if headers changed; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan (jobs is concurrency code: run the asan flavor of the jobs selection 20 times and record failures);
      Linux `--affected` plus asan and tsan (shared jobs sources changed); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: processor-group arithmetic (masks are per group), a worker pinned to a CPU the process may
      not use, and anything that changed in the shared scheduler while removing the POSIX includes.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the job system runs on Windows with correct topology, affinity and thread names, the jobs suite and stress tests pass in plain and ASan on Windows,
and Linux still passes its sanitizer flavors.
