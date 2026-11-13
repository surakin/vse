# Phase 22 / Day 330 — Closing Runs on Three Machines & the Phase Review

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 313-329.

Close the phase.

## Tasks

- [ ] **Closing full run on Linux (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --all --flavor all`, plus `ctest -L docs` and the demos; quote the final `ran X of Y tests (phases ...)` line here; `CURRENT_PHASE` stays 22 until Phase 23's first day.
- [ ] **Windows PC and Mac:** the closing runs there as well (`windows-msvc` plain and asan, `windows-clang-cl`; `macos-clang` plain, asan and tsan, plus the arm64 compile check); the texture and mint determinism tests run on every machine and the byte-identical golden hashes must match across them (a mismatch is a bug in the minter, not in the test); or recorded as open in `KNOWN-ISSUES.md`.
- [ ] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 313-330, from the commit before Day 313 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-22-asset-pipeline.md` status, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 23 needs its day files before it starts), `KNOWN-ISSUES.md` sweep.
- [ ] **Runs and records (CLAUDE.md):** quote each run's final `ran X of Y tests (phases ...)` line in this file; `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the whole suite passes in every flavor on Linux (and on the other machines, or their runs are recorded as open), the phase review is recorded, and Phase 23 is next.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
