# Phase 24 / Day 372 — Docs, Closing Runs on Three Machines & the Phase Review

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-371.

Documents the phase, runs everything everywhere, and reviews the whole phase.

## Tasks

- [ ] Guide `docs/guide/engine-library.md`: "Scenes, cameras and lights", "Animated models", "Live textures", "Labels" and the host's duties (ordering of the frame's systems, threads, DPI); the spec updated to what was built; the coordinate conventions in one table.
- [ ] **Linux:** `scripts/test-phase.sh --all --flavor all` (plain, asan, tsan), `ctest -L docs`, the demos the phase ships, the shared and the static builds, the importer-off build; the final `ran X of Y` lines recorded here.
- [ ] **Windows PC and Mac:** the closing runs there as well (`windows-msvc` plain and asan, `windows-clang-cl`; `macos-clang` plain, asan, tsan and the arm64 compile), shared and static; or recorded as open in `KNOWN-ISSUES.md`.
- [ ] ABI: the snapshot and `abi_check` record the phase's additions; the version follows the spec's rule; the C header compiles as C11 with `-pedantic -Werror`.
- [ ] **Uncapped end-of-phase review (CLAUDE.md):** `/code-review high --max-findings all` over the whole phase's diff (Days 358-371); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **Records:** `plans/phase-24-c-api-scenes-for-hosts.md` status, `PLAN.md` (roadmap row, log, a new assumption on scenes for hosts), `CLAUDE.md` (the C API bullet gains the scene rules), `STATUS.md` (next: Phase 25 needs its day files before it starts), the memory notes, the `KNOWN-ISSUES.md` sweep (no shadows, no orthographic cameras unless built, no zero-copy host GPU textures, no per-vertex morph targets, limits filed by the days).

**Done:** the phase is closed on all machines with every run quoted, the review is recorded, and `STATUS.md` names Phase 25 as next.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
