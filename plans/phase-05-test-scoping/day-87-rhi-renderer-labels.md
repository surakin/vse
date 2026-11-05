# Phase 5 / Day 87 — Phase Labels for the RHI and Renderer Tests

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 86.

The two executables that mix phases (`rhi_tests` holds Phase 3 RHI tests and Phase 4 renderer GPU tests) and the three
sanitizer flavors of each.

## Tasks

- [ ] Add the `tests/rhi` and `tests/renderer` assignments to `tests/phases.txt`: the Phase 4 files in `tests/rhi` (`vulkan_gbuffer_test.cpp`, `vulkan_lighting_test.cpp`, `vulkan_render_graph_test.cpp`, `vulkan_strip_test.cpp`, `vulkan_renderer_demo_test.cpp`, `shader_interface_test.cpp` for the Phase 4 shaders, and so on, each decided from the file's content) and the rest Phase 3; every `tests/renderer` file Phase 4.
- [ ] Switch `tests/rhi` and `tests/renderer` to `engine_discover_tests` for all three flavors (plain, ASan, TSan), keeping `TEST_SPEC`, `TEST_SUFFIX`, `PROPERTIES`, `RESOURCE_LOCK display` and the pipeline-cache `ENVIRONMENT` exactly; compare `ctest -N` before and after.
- [ ] A test file that contains both a Phase 3 and a Phase 4 test (check each `TEST_CASE`'s tags and name) is split only if the split is small; otherwise record the file under the later phase with a comment, since a scoped run of the later phase must include it.
- [ ] Generated label files must not make a build without a GPU fail: discovery runs `--list-tests`, which needs no device; confirm by running the discovery step with `VK_ICD_FILENAMES` pointing at nothing.
- [ ] Make `test_phase_labels` also cover the flavors: each phase has tests in each flavor that exists for it (foundation has only a plain flavor; record that as expected in the map file, not as a failure).
- [ ] Per-phase, per-flavor test counts and wall times (scoped to one phase each, plain/ASan/TSan) recorded in the day file as the "before runner" baseline.
- [ ] Run the full suite in all flavors: same pass count as before the day.
- [ ] **Documentation:** no public API; the building-and-testing guide's "Test phases" section lists the per-phase counts. `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** all 1839 tests carry exactly one phase label in every flavor, `ctest -L phase4` and `ctest -L phase3` select what a reader expects, a build without a GPU still configures and builds, and the full suite passes unchanged.
