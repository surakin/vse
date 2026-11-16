# Phase 26 / Day 414 — The Linux Backend II: The Render Thread, Underruns and Latency

**Phase:** [Phase 26 — Audio](../phase-26-audio.md)
**Depends on:** Day 413.

Keep the device fed: a real-time thread, timing, recovery and the measured latency.

## Tasks

- [ ] The render thread (`os::` thread wrappers, never `std`): a high priority where the OS grants it (a best-effort real-time request, the failure not an error), a loop that fills the period and calls the render callback; no allocation, no locks, no I/O on that thread (a debug check counts allocations during the callback and fails a test that makes one).
- [ ] Underruns and xrun recovery: detect a missed period, restart the stream, count it in the stats and the log (rate-limited); a deliberate stall test (the callback sleeps) proves recovery and the count.
- [ ] Latency: the device's reported latency plus the buffer, measured with a loopback where the machine has one and otherwise computed; `DeviceInfo::latency_frames` documented; the command queue's block stamp (Day 409) uses it so a command posted now is applied at the next block that can still be heard on time.
- [ ] Start, stop and close from any thread, idempotent; destroy while the callback runs waits for it (no use after free); a callback that takes the full period gets its own stat.
- [ ] Clock: a monotonic time from frames rendered (the audio clock) that the game can read to schedule events against the audio, with the drift against the system clock measured over a long run.
- [ ] Tests: the callback is called with the requested sizes at the device's pace within tolerance (hardware test, skipped without a device); the stall test; the destroy-while-running test under tsan with a fake backend.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 428-429, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/audio.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 26); only wrapped types, no `std`, no raw platform macros; the audio thread never allocates, locks or does I/O.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Linux backend plays the mixer's output on a real device with measured latency and recovers from underruns.
