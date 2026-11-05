# Phase 5 / Day 88 — The Runner & Touched-Module Selection

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 87.

The command developers type: run the current phase, run what a diff touched, or run everything, and always say how much
was run.

## Tasks

- [ ] Create the one-line `CURRENT_PHASE` file at the repo root (`5`), with a comment line explaining it, and have `STATUS.md` say it is changed in the commit that starts a phase.
- [ ] Write `tests/module_map.txt`: source directory to the phases whose tests exercise it (`engine/foundation` to 0,1; `engine/jobs` to 2; `engine/rhi` to 3 and 4's RHI-facing tests; `engine/renderer`, `shaders`, `game/renderer_demo`, `tools/shader_mint` to 4; `tools/docgen` and `docs` to 2.5; `tests/<dir>` to the phase of the changed file; anything not listed forces the full suite, so a new directory cannot be silently skipped).
- [ ] Write `scripts/test-phase.sh`: `scripts/test-phase.sh [N | --current | --affected | --all] [--flavor plain|asan|tsan|all] [--jobs N] [--dry-run]`. Default is `--current` in the plain flavor. `--affected` is the current phase plus the phases of every module in `git diff --name-only` (uncommitted changes, and the commits since the last `phase complete` tag or the merge base, whichever the Day 85 spec chose); a changed `CMakeLists.txt`, anything under `cmake/`, `CMakePresets.json` or a header under `engine/foundation` selects `--all`. It builds first, runs ctest with the label expression from the Day 85 spike, and prints `ran X of Y tests (phases: ...; flavors: ...)` as its last line, with a loud note if X is less than Y. `--dry-run` prints the selection without running.
- [ ] Refuse to run `--current` when `CURRENT_PHASE` is missing or not a known phase; `--all` ignores it.
- [ ] Tests for the script (a ctest `test_phase_script`, label Phase 5): a fixture git repository in a temp directory with a tiny CMake project is too heavy, so run the script with `--dry-run` against the real build using a `--diff-from-file` option that reads the changed-file list from a file instead of git; cover: only a Phase 4 file changed (selects phase 4 plus current), a foundation header changed (selects all), an unknown directory changed (selects all), `--current` with a bad `CURRENT_PHASE`, and `--flavor` combinations. Each case checks the selected phases and the count line.
- [ ] Check the selection numbers against `ctest -N -L` for each phase, so the script's `X of Y` is the real count.
- [ ] Print timing: the script reports its own wall time and, for `--current`/`--affected`, the time a full run took last time (recorded in `build/.last-full-run` by `--all`), so the saving is visible on every run.
- [ ] **Documentation:** a usage comment at the top of the script and `scripts/test-phase.sh --help`; the guide section is written on Day 89. `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** `scripts/test-phase.sh` runs the current phase by default, `--affected` adds the phases of touched modules and escalates to `--all` for build-file and shared-header changes, every run ends with an `X of Y` line, and the script's own selection logic is covered by `test_phase_script`.
