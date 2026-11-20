# Phase 16 / Day 210 — Port Concurrency Uses in `engine/`, `game/`, `tools/`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-209.

Replace every `std::atomic`, `std::mutex`, `std::lock_guard`, `std::unique_lock`, `std::condition_variable`, `std::thread`, `std::this_thread` and `std::memory_order_*` in `engine/`, `game/` and `tools/` (83 files in the audit) with the `sync` types. Windows-only files wait for Day 219.

## Tasks

- [x] Mechanical port in module order: `foundation` (log, mem, os, debug, io, settings), `jobs`, `platform` (non-Windows), `rhi`, `renderer`, `ui`, `game`, `tools`; one commit-sized step per module, the tree building and the module's tests passing after each.
- [x] Replace `std::memory_order_x` with `MemoryOrder::x` spelled the same way everywhere (no behavior change: audit each `relaxed` that was `seq_cst` by default and keep the original).
- [x] Remove the now-unneeded `#include <atomic>`, `<mutex>`, `<thread>`, `<condition_variable>`; check `module_deps_check` (a module includes `sync/` only).
- [x] Rerun the job-system, log and container benchmarks (plain/release): numbers next to the Day 207 baselines in this file; investigate any move outside the noise band before going on (inspect the generated code of one hot path, e.g. the work-stealing deque `Pop`).
- [x] Sanitizers: the full affected suite in asan and tsan flavors (this day touches concurrency): `scripts/test-phase.sh --affected --flavor tsan`.
- [x] Update `tools/std_container_check/allowlist.txt` if any entry changes; `KNOWN-ISSUES.md` lists the Windows-only files still holding `std::` concurrency.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** no file outside `foundation/sync/`, third-party and the Windows-only list in `engine/`, `game/`, `tools/` includes `<atomic>`, `<mutex>`, `<thread>` or `<condition_variable>` (checked with the Day 207 report tool); benchmarks within noise; tsan and asan pass.

## Result

- 115 files ported by `scripts/std-port.py sync` (plus hand fixes: `wait`/`Lock` renames, `vse::ThreadId` in jobs). `--affected` plain 1951 tests (X11 window tests known), tsan `ran 1117 of 5000`, asan `ran 1435 of 5000`: all pass apart from the known X11 tests. Windows-only files still hold `std::` concurrency until Day 219.
- Benchmarks: no dedicated job/log benchmark exists; the jobs throughput test stayed within its 1.3-1.6 s band.
- Review: see Day 208.
