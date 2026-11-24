# Maintenance / linux-known-issues / Day 08 — Closing: docs, runs, review and the entries

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Public headers changed: `///` comments with contract labels, `docgen lint` at zero, `cmake --build build --target docs`, the guides (`terrain.md`, `instancing.md`, `physics.md`, `engine-library.md`, `scripting.md`, `renderer.md`, `audio.md`) updated to the new facts; no phase or day numbers in published pages.
- [ ] The C API changes: `ENGINE_ABI_VERSION_MINOR` and the snapshot (`abi/engine_api.snapshot`) are raised for the struct and table changes of this series and of the double-precision series (a decision for the user: asked before the day, recorded here); `engine_h_coverage` passes.
- [ ] Golden hashes re-baselined by Days 03 and 05 are listed with the Windows and macOS commands the user runs (`KNOWN-ISSUES.md`, 'Unverified by a human').
- [ ] Whole suite in every flavor: `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, the demos' window runs on Wayland and X11 with validation; the `ran X of Y` line recorded.
- [ ] Both compilers, unity on and off, `docgen lint` zero; `scripts/test-phase.sh --affected` plain, asan and tsan for the days that touch concurrency or lifetimes (Days 03, 04, 06).
- [ ] `/code-review high --max-findings all` over the series' diff; verify, fix with a regression test shown to fail without the fix, or decline with a reason; record the count and outcome here.
- [ ] `KNOWN-ISSUES.md`: every entry the series fixed is removed; every entry that stays names what was found; `STATUS.md`, `PLAN.md` (progress log), `ARCHITECTURE.md` and `BUILD.md` current; commit and push.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** The series is documented, the whole suite passes on Linux, the review's findings are resolved, and `KNOWN-ISSUES.md` lists only what a person, a Windows PC or a Mac still has to do.

## Post-implementation code review

(To be filled in when the day is done.)
