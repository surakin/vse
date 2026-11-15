# Phase 23 / Day 357 — Docs, Closing Runs on Three Machines & the Phase Review

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 342-356.

Close the phase.

## Tasks

- [ ] `docs/guide/engine-library.md` finished (building the library, the C API conventions, versioning rules, writing a host, embedding in a native window, the the host path, platform notes, limits); `docs/tutorials/loading-the-engine.md` (a C host that opens a window and clears it, compiled and run by a test), linked from the tutorial index; `docs/reference/` regenerated; `ctest -L docs` passes.
- [ ] **Closing full run on Linux:** `scripts/test-phase.sh --all --flavor all` with the engine built static and with `ENGINE_BUILD_SHARED=ON`, plus `ctest -L docs` and the demos; quote the final `ran X of Y tests (phases ...)` lines here; `CURRENT_PHASE` stays 23 until Phase 25's first day.
- [ ] **Windows PC and Mac:** the closing runs there as well (`windows-msvc` plain and asan, `windows-clang-cl`; `macos-clang` plain, asan, tsan and the arm64 compile), the shared and the static builds; or recorded as open in `KNOWN-ISSUES.md`.
- [ ] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 342-357); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-23-engine-as-library.md` status, `PLAN.md` (roadmap row, log, assumption #27 updated to what was built), `CLAUDE.md` (the loadable-library bullet changes from "planned" to the rules that hold), `STATUS.md` (next: Phase 25 needs its day files before it starts), the memory notes, the `KNOWN-ISSUES.md` sweep (no stable C++ ABI, no language bindings beyond the C table, no plugin hot reload, shared textures per platform, and every limit the phase filed).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the whole suite passes in every flavor with the engine built both ways on Linux (and on the other machines, or their runs are recorded as open), the phase review is recorded, and Phase 25 is next.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
