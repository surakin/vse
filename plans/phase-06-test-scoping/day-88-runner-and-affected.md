# Phase 6 / Day 88 — The Runner & Touched-Module Selection

**Phase:** [Phase 6 — Test Scoping](../phase-06-test-scoping.md)
**Depends on:** Day 87.

The command developers type: run the current phase, run what a diff touched, or run everything, and always say how much
was run.

## Tasks

- [x] Create the one-line `CURRENT_PHASE` file at the repo root (`5`), with a comment line explaining it, and have `STATUS.md` say it is changed in the commit that starts a phase.
- [x] Write `tests/module_map.txt`: source directory to the phases whose tests exercise it (`engine/foundation` to 0,1; `engine/jobs` to 2; `engine/rhi` to 3 and 4's RHI-facing tests; `engine/renderer`, `shaders`, `game/renderer_demo`, `tools/shader_mint` to 4; `tools/docgen` and `docs` to 2.5; `tests/<dir>` to the phase of the changed file; anything not listed forces the full suite, so a new directory cannot be silently skipped).
- [x] Write `scripts/test-phase.sh`: `scripts/test-phase.sh [N | --current | --affected | --all] [--flavor plain|asan|tsan|all] [--jobs N] [--dry-run]`. Default is `--current` in the plain flavor. `--affected` is the current phase plus the phases of every module in `git diff --name-only` (uncommitted changes, and the commits since the last `phase complete` tag or the merge base, whichever the Day 85 spec chose); a changed `CMakeLists.txt`, anything under `cmake/`, `CMakePresets.json` or a header under `engine/foundation` selects `--all`. It builds first, runs ctest with the label expression from the Day 85 spike, and prints `ran X of Y tests (phases: ...; flavors: ...)` as its last line, with a loud note if X is less than Y. `--dry-run` prints the selection without running.
- [x] Refuse to run `--current` when `CURRENT_PHASE` is missing or not a known phase; `--all` ignores it.
- [x] Tests for the script (a ctest `test_phase_script`, label Phase 6): a fixture git repository in a temp directory with a tiny CMake project is too heavy, so run the script with `--dry-run` against the real build using a `--diff-from-file` option that reads the changed-file list from a file instead of git; cover: only a Phase 5 file changed (selects phase 5 plus current), a foundation header changed (selects all), an unknown directory changed (selects all), `--current` with a bad `CURRENT_PHASE`, and `--flavor` combinations. Each case checks the selected phases and the count line.
- [x] Check the selection numbers against `ctest -N -L` for each phase, so the script's `X of Y` is the real count.
- [x] Print timing: the script reports its own wall time and, for `--current`/`--affected`, the time a full run took last time (recorded in `build/.last-full-run` by `--all`), so the saving is visible on every run.
- [x] **Documentation:** a usage comment at the top of the script and `scripts/test-phase.sh --help`; the guide section is written on Day 89. `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** `scripts/test-phase.sh` runs the current phase by default, `--affected` adds the phases of touched modules and escalates to `--all` for build-file and shared-header changes, every run ends with an `X of Y` line, and the script's own selection logic is covered by `test_phase_script`.

## Result

- `CURRENT_PHASE` (`5`), `tests/module_map.txt`, `scripts/test-phase.sh` (`--help` is the usage comment at its top).
  Options as planned plus `--display/--no-display` (window tests are left out of `--current`/`--affected` unless phase 1 is selected or
  `engine/foundation` changed), `--since REF` (default: the last commit that changed `CURRENT_PHASE`, so a phase's diff is
  "since the phase started") and `--current-phase-file` for tests. The counts come after the build (tests register at build
  time), a selection of zero tests is an error, and `.last-full-run` is written only for `--all --flavor all` with nothing left out.
- `test_phase_script` (29 ctest cases, label `phase6`, `tests/test_phase_script/`): each case runs the script with `--dry-run`
  against this build, checks the phases and the display decision, and checks that "X of Y" equals an independent `ctest -N`;
  cases cover explicit phases and flavors, the current phase (with 1 and `--display`), `--affected` for RHI, jobs, docgen+shaders, notes only,
  the runner, foundation source, and the escalations (foundation header, unknown directory, a `CMakeLists.txt`, the phase map), refusals (bad or
  missing `CURRENT_PHASE`, unknown phase, bad flavor, unknown option), the git path, `--no-display`, that every path in the module map exists,
  and that plain + ASan + TSan add up to all flavors for phases 2, 4 and 5. Planted regression: removing the foundation-header
  escalation fails `affected_foundation_header`.
- The module map was widened after review (see the spec, 6.3): a directory maps to everything that links it.
- Whole suite: 1874/1874 pass.

## Post-implementation code review

`/code-review high`, 10 findings:

1. Counts taken before the build. **Fixed**: build first, then count.
2. `shaders` not mapped to phase 4. **Fixed** (3, 4).
3. `engine/jobs` and `engine/foundation` sources missed the phases that link them. **Fixed**: widened; the test fixture changed with it.
4. Public headers did not select the docs checks (2.5). **Fixed**: every `engine/` line includes 2.5.
5. `scripts` mapped to nothing although `test_phase_script` runs the script. **Fixed**: phase 6.
6. `.last-full-run` written for an incomplete run. **Fixed**.
7. A zero-test selection exited 0. **Fixed** (also for `--dry-run`).
8. No test for the module-map paths, the git path, `--no-display`. **Fixed** except the git path's *content* (base selection, untracked files),
   which needs a fixture repository: **declined**, the smoke case proves it runs, `--diff-from-file` covers the selection logic.
9. Empty array under `set -u` on bash older than 4.4. **Fixed** (`${sel[@]+"${sel[@]}"}`).
10. Nested `ctest -N` against the live build, same oracle. **Partly fixed**: the partition cases check the label semantics against
    ctest's own counts; the concurrent `ctest -N` runs passed in the full `-j16` suite (1874/1874), nothing flaky seen.
