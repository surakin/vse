# Phase 16 / Day 210 — Port Concurrency Uses in `engine/`, `game/`, `tools/`

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208-209.

Replace every `std::atomic`, `std::mutex`, `std::lock_guard`, `std::unique_lock`, `std::condition_variable`, `std::thread`, `std::this_thread` and `std::memory_order_*` in `engine/`, `game/` and `tools/` (83 files in the audit) with the `sync` types. Windows-only files wait for Day 219.

## Tasks

- [ ] Mechanical port in module order: `foundation` (log, mem, os, debug, io, settings), `jobs`, `platform` (non-Windows), `rhi`, `renderer`, `ui`, `game`, `tools`; one commit-sized step per module, the tree building and the module's tests passing after each.
- [ ] Replace `std::memory_order_x` with `MemoryOrder::x` spelled the same way everywhere (no behavior change: audit each `relaxed` that was `seq_cst` by default and keep the original).
- [ ] Remove the now-unneeded `#include <atomic>`, `<mutex>`, `<thread>`, `<condition_variable>`; check `module_deps_check` (a module includes `sync/` only).
- [ ] Rerun the job-system, log and container benchmarks (plain/release): numbers next to the Day 207 baselines in this file; investigate any move outside the noise band before going on (inspect the generated code of one hot path, e.g. the work-stealing deque `Pop`).
- [ ] Sanitizers: the full affected suite in asan and tsan flavors (this day touches concurrency): `scripts/test-phase.sh --affected --flavor tsan`.
- [ ] Update `tools/std_container_check/allowlist.txt` if any entry changes; `KNOWN-ISSUES.md` lists the Windows-only files still holding `std::` concurrency.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** no file outside `foundation/sync/`, third-party and the Windows-only list in `engine/`, `game/`, `tools/` includes `<atomic>`, `<mutex>`, `<thread>` or `<condition_variable>` (checked with the Day 207 report tool); benchmarks within noise; tsan and asan pass.
