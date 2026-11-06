# Phase 5 / Day 87 — Phase Labels for the RHI and Renderer Tests

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 86.

The two executables that mix phases (`rhi_tests` holds Phase 3 RHI tests and Phase 4 renderer GPU tests) and the three
sanitizer flavors of each.

## Tasks

- [x] (done on Day 86, see its Result) Add the `tests/rhi` and `tests/renderer` assignments to `tests/phases.txt`: the Phase 4 files in `tests/rhi` (`vulkan_gbuffer_test.cpp`, `vulkan_lighting_test.cpp`, `vulkan_render_graph_test.cpp`, `vulkan_strip_test.cpp`, `vulkan_renderer_demo_test.cpp`, `shader_interface_test.cpp` for the Phase 4 shaders, and so on, each decided from the file's content) and the rest Phase 3; every `tests/renderer` file Phase 4.
- [x] (done on Day 86) Switch `tests/rhi` and `tests/renderer` to `engine_discover_tests` for all three flavors (plain, ASan, TSan), keeping `TEST_SPEC`, `TEST_SUFFIX`, `PROPERTIES`, `RESOURCE_LOCK display` and the pipeline-cache `ENVIRONMENT` exactly; compare `ctest -N` before and after.
- [x] A test file that contains both a Phase 3 and a Phase 4 test (check each `TEST_CASE`'s tags and name) is split only if the split is small; otherwise record the file under the later phase with a comment, since a scoped run of the later phase must include it.
- [x] Generated label files must not make a build without a GPU fail: discovery runs `--list-tests`, which needs no device; confirm by running the discovery step with `VK_ICD_FILENAMES` pointing at nothing.
- [x] Make `test_phase_labels` also cover the flavors: each phase has tests in each flavor that exists for it (foundation has only a plain flavor; record that as expected in the map file, not as a failure).
- [x] Per-phase, per-flavor test counts and wall times (scoped to one phase each, plain/ASan/TSan) recorded in the day file as the "before runner" baseline.
- [x] Run the full suite in all flavors: same pass count as before the day.
- [x] **Documentation:** no public API; the building-and-testing guide's "Test phases" section lists the per-phase counts. `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** all 1839 tests carry exactly one phase label in every flavor, `ctest -L phase4` and `ctest -L phase3` select what a reader expects, a build without a GPU still configures and builds, and the full suite passes unchanged.

## Result

Most of the day's mechanics landed on Day 86 (the check needs every test labeled). What this day added:

- **No GPU:** with `VK_ICD_FILENAMES=/nonexistent ENGINE_VULKAN_LIBRARY=/nonexistent`, touching two sources and running `ninja`
  rediscovered the rhi and renderer tests (discovery is `--list-tests`, no device); `ctest -N` still lists 1843 tests.
- **Flavor coverage:** `flavors <phase> plain,asan,tsan` lines in `tests/phases.txt` state which flavors each phase has
  (foundation 0 and 1: plain only; 2.5: plain, ASan; 2, 3, 4: all three; 5: plain). `test_phase_labels` fails when a phase
  gains or loses a flavor (verified by editing the map), plus two planted fixtures (a flavor the map does not list, a listed flavor
  with no tests). `selected_tests` also had a bug: a test name containing `;` was split into several list elements, which
  inflated counts (found when the flavor check reported 182 ASan tests against ctest's 168); fixed.
- **Baseline for the runner** (`ctest -j16 -L ^phaseN$` per flavor on the development machine, wall time, 2026-11-06; plain is
  neither `asan` nor `tsan`):

| Phase | plain | ASan | TSan |
|---|---|---|---|
| 0 foundation | 87 tests, 0.06 s | - | - |
| 1 Linux platform | 63, 8.7 s | - | - |
| 2 jobs | 77, 1.7 s | 77, 2.3 s | 78, 8.2 s |
| 2.5 docs tooling | 172, 1.5 s | 168, 1.6 s | - |
| 3 RHI | 226, 17.1 s | 211, 39.4 s | 143, 2.8 s |
| 4 renderer | 211, 13.0 s | 178, 37.6 s | 148, 0.7 s |

  Whole suite for comparison: 1845 tests in about 65 s. The scoped run of phase 5 alone is the 6 `test_phase_labels*` tests (about 1.3 s).
- Guide: the per-phase table is in "Test phases". Full suite: 1845/1845 pass (6 planted/label tests are new).

## Post-implementation code review

`/code-review high`, 9 findings:

1. No planted fixture for the flavor check. **Fixed**: two fixtures.
2. Malformed, duplicate or stale `flavors` lines accepted silently. **Fixed**: each is an error.
3. The `;` fix has no regression test. **Declined with a note**: the splitting only inflated counts, which no other check compares; the `good` fixture now carries such a name so the path is exercised.
4. 21 extra `ctest -N` launches. **Declined**: the test takes 1.2 s.
5. Hard-coded counts in the guide go stale. **Fixed**: dated, with the recount command.
6. Usage header of `check.cmake` stale. **Fixed** (options `CHECK_FLAVORS`, `CHECK_UNUSED` documented).
7. `tests/phases.txt` header stale (labels range, `flavors` syntax). **Fixed**.
8. An over-long comment line. **Fixed**.
9. Process items. **Done in this commit**.
