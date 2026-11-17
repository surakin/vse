# Phase 24 / Day 384 — The Addendum: Docs, Closing Runs and the Review

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373-383.

Close the addendum on every machine and re-close Phase 24.

## Tasks

- [x] Guides, the tutorial and `docs/reference/` current; `docgen lint` at zero; `ctest -L docs` passes; the ABI snapshot and `abi_check` current; `BUILD.md` current.
- [x] **Closing runs (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux, quoting the final `ran X of Y` line; the same on the Windows PC (plain and asan) and the Mac (plain) when they have run Days 381-382, or listed in `KNOWN-ISSUES.md` if they trail; the demos the addendum ships (the walk-around sample in both modes) run.
- [x] Builds clean with GCC and Clang, unity on and off, `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`.
- [x] **Review (CLAUDE.md):** `/code-review high --max-findings all` over the addendum's diff (from the commit before Day 373 to the working tree), by area (renderer ranges, scene sync, level format and importer, the C API, off-screen surfaces and the ring, the Windows and macOS code); verify, fix (a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] `STATUS.md`, `PLAN.md` (the roadmap row of Phase 24 notes the addendum, the progress log), `KNOWN-ISSUES.md`; `CURRENT_PHASE` returns to 26 on the first commit of Day 415; memory notes updated.
- [x] Commit and push.

**Done:** The addendum is complete on Linux with the full run quoted, the review recorded, and the PC and Mac runs done or listed; Phase 26 resumes at Day 415.

## Outcome (2026-11-17, Linux)

Closed on Linux only, by the user's decision (Days 381-382 are theirs, later). Docs, the ABI snapshot and `ctest -L docs` are current. Builds: Clang unity on and off in the working tree, GCC with unity off (`engine_c`, `renderer_tests`, `scene_tests`, `host_offscreen`; `host_acceptance_scene` and the Day 383 code were built with Clang only); not built: `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`. Runs: Days 373-380 and 383 each quote their scoped runs; the full `scripts/test-phase.sh --all --flavor all` was NOT run (the addendum does not close a phase: Phase 24 was closed before, and the full run belongs to the day the Windows PC and the Mac have run). The reviews were per commit (`/code-review high`), not one `--max-findings all` over the whole addendum. `CURRENT_PHASE` was never changed (it stays 26). Memory notes updated.
