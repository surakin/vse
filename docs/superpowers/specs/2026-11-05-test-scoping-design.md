# Test scoping design (Phase 5)

Status: decided on Phase 5 Day 85 (2026-11-06). Implemented by Days 86-90.

## 1. Problem

The suite has 1839 ctest tests and takes minutes in each of three flavors (plain, ASan, TSan). Development on the current
phase re-runs tests of finished phases that cannot have changed. The goal: day-to-day runs select the current phase's
tests plus the tests of any earlier module the diff touches; the full suite in all flavors runs when a phase is
completed (and for the always-full triggers in section 6).

## 2. Inventory (build/, plain flavor, 2026-11-06)

Catch2 tests per executable, and the time of one executable run alone (single process, no ctest parallelism):

| Executable | Test cases | Alone | Notes |
|---|---|---|---|
| `foundation_tests` | 150 | 22.7 s | includes the window tests (Wayland/X11 backends) |
| `jobs_tests` | 77 | 4.6 s | |
| `docgen_tests` | 168 | 4.2 s | |
| `shader_mint_tests` | 17 | 0.005 s | |
| `renderer_tests` | 151 | 5.0 s | |
| `rhi_tests` | 241 | 49.0 s | most of it GPU tests |

The 1839 ctest entries are these cases once per flavor that builds them (plain, ASan, TSan), plus the header-check and other non-Catch tests. Non-Catch tests: `docs_check`, `docs_lint`, `docs_links`,
`docs_samples`, `rhi_boundary_check*` (6), `raw_array_check*`, `raw_new_check*` (4), `shaders_check`,
`shaders_portability`, `rhi_demo_*`, `renderer_demo_*` (9), `tsan_canary_detects_race`. The non-Catch group measured
11.9 s under `ctest -j16`.

Flavors are separate executables of the same sources in the same build directory (`jobs_tests`, `jobs_tests_asan`,
`jobs_tests_tsan`, ...); their ctest names carry the suffix `_asan`/`_tsan` and the labels `asan`/`tsan` (plain
tests carry neither); the TSan flavor excludes `[gpu]`. The per-file counts are the source of truth for the map;
`ctest -N` lists the names.

## 3. Phase assignment (rule: the phase that owns the behavior, not the directory)

`tests/phases.txt` carries `glob phase` lines. Resulting assignment, by source file (test cases in parentheses):

**Phase 0 (foundation):** arena_allocator (2), array (3), command_line (9), container_vector (12), file_io (3),
grapheme (7), hash_map (2), log (3), matrix (3), normalize (5), os_timing (2), pool_allocator (1), quaternion (5),
shared_ptr (6), stack_allocator (2), string (4), transform (2), unique_ptr (7), utf8 (3), vector (3).
`concurrency_primitives_test.cpp` (3) is Phase 0 too: it tests the foundation's atomics and lock wrappers, not the
scheduler.

**Phase 1 (Linux platform):** wayland_connection (4), wayland_window (10), x11_window (17), keyboard_input (11),
window_backend (16), gpu_driver_loading (5).

**Phase 2 (jobs):** every file under `tests/jobs/` (async_io, counter, deque, frame_pool, job, parallel_for,
scheduler, smoke, stress, worker_pool) and `tsan_canary_detects_race`.

**Phase 2.5 (docs tooling):** every file under `tests/docgen/` and the `docs_*` tests.

**Phase 3 (RHI):** handle_pool (12), null_device (41), types (17), shader (8), smoke (3), vulkan_bindings (8),
vulkan_buffer (12), vulkan_commands (10), vulkan_compute (3), vulkan_cube (5), vulkan_device (10), vulkan_fault (5),
vulkan_frame (3), vulkan_golden (3), vulkan_instance (6), vulkan_loader (10), vulkan_memory (17), vulkan_pipeline (9),
vulkan_swapchain (10), vulkan_textured_cube (8), vulkan_texture (9), vulkan_lit_cube (5), `rhi_boundary_check*`,
`rhi_demo_*`.

**Phase 4 (renderer):** everything under `tests/renderer/`; the `tests/shader_mint/` files; and in `tests/rhi/`:
vulkan_gbuffer (5), vulkan_lighting (9), vulkan_render_graph (1), vulkan_renderer_demo (7), vulkan_strip (4),
shader_interface (1); `shaders_check`, `shaders_portability`, `raw_array_check*`, `raw_new_check*`,
`renderer_demo_*`.

Ambiguous files and the reason for each:

- `vulkan_lit_cube_test.cpp` is in Phase 3: the lit cube was the Phase 3 capstone demo; the deferred lighting
  pipeline is the Phase 4 file `vulkan_lighting_test.cpp`.
- `vulkan_strip_test.cpp` (Day 78) tests `primitive_restart`, an RHI field, but was added by the renderer's mesh
  work; kept in Phase 4 because that is the phase that would change it.
- `shader_interface_test.cpp` asserts the Slang shaders' reflected interface (Phase 4 Days 69-71), though it lives
  in `tests/rhi/`.
- `shader_test.cpp` (RHI shader module creation) is Phase 3.
- `unique_ptr_test.cpp`, `shared_ptr_test.cpp` were extended by Phase 4 Day 80b but test a foundation module: Phase 0.
- `gpu_driver_loading_test.cpp` tests the dlopen layer of the platform (Phase 1), not a Vulkan device.
- `concurrency_primitives_test.cpp` see above.
- `raw_array_check*`/`raw_new_check*` police `engine/` and `game/` (added Phase 4 Day 80a/80b): Phase 4.
- `rhi_demo_*` run `game/rhi_demo` (Phase 3); `renderer_demo_*` run `game/renderer_demo` (Phase 4).

## 4. Label mechanism (spike, kept in the scratchpad)

What was tried, and what was decided:

- Catch2 v3 has no automatic per-file tag (v2's `[#file]` is gone), so a file cannot be selected with a test spec.
- `ADD_TAGS_AS_LABELS` turns Catch2 tags into ctest labels but would need every test case tagged by hand.
- `catch_discover_tests` runs the executable with `--list-tests --reporter json`; the JSON carries a
  `source-location.filename` per test. `CatchAddTests.cmake` already parses each test object and emits
  `set_tests_properties(<prefix+name+suffix> PROPERTIES ...)` with exact name escaping. Redoing that name handling
  in a second script would duplicate the escaping and parse the (quadratic in CMake) JSON a second time.
- **Decision:** add one optional input to the vendored `CatchAddTests.cmake`/`Catch.cmake` (`PHASE_MAP`: a file of
  `glob phase` lines); for each test it matches the test's source file (path relative to the repo) against the globs and
  appends `phaseN` to the `LABELS` property. The patch is listed in `third_party/catch2/README` so a Catch2 upgrade
  re-applies it. `engine_discover_tests(target ...)` in `cmake/EngineTestPhases.cmake` is a thin wrapper that always
  passes the map, so no test directory calls `catch_discover_tests` directly.
- Names with the `TEST_SUFFIX` and the two `catch_discover_tests` calls per target (window tests in a second call
  with `RESOURCE_LOCK display`) need nothing special: both calls go through the same wrapper and the labeling is done
  inside the same generation step as `add_test`, so names cannot disagree.
- A test whose file matches no glob, or more than one, gets no label from the map; `test_phase_labels` (a ctest
  test) fails on any test with zero or more than one `phaseN` label, with a planted fixture proving that it can fail.
  Non-Catch tests are labeled by hand in their CMake files (`set_tests_properties(... LABELS "phaseN;...")`).

Label names: `phase0`, `phase1`, `phase2`, `phase2_5`, `phase3`, `phase4`, then `phase5`... as phases add tests.
`phase2_5` rather than `phase2.5` because ctest label filters are regular expressions and `.` matches anything
(`phase2.5` would also match `phase2x5`; harmless here but wrong in principle). Existing labels (`gpu`, `demo`,
`shaders`, `docs`) stay.

## 5. `ctest -L` combination (CMake 4.4.5, tried on a 4-test project)

- OR of phases: `ctest -L 'phase1|phase2'` selects tests carrying either label (regex alternation).
- AND: `ctest -L phase2 -L gpu` selects tests that carry both (repeated `-L` is AND).
- Exclude: `ctest -L phase2 -LE gpu`.
- Exact label: `ctest -L '^phase2$'` (anchors keep `phase2` from matching `phase2_5`; the runner always anchors).
- Display tests: `-LE display` is not available as a label, they are marked with `RESOURCE_LOCK display` only. The
  runner selects them by adding a `display` label to those tests (Day 86) so a scoped run can include or exclude them.

## 6. Decisions

1. **Labels** as in section 4, one per test, assigned from `tests/phases.txt` (one file; a new test file needs one
   line, and `test_phase_labels` fails until it has it).
2. **Current phase** is a one-line `CURRENT_PHASE` file at the repo root (`5` today). It changes in the commit that
   starts a phase, nowhere else.
3. **A scoped run** (`scripts/test-phase.sh --current`, the default for development) is: the current phase's tests,
   plus `--affected`: the tests of every phase that owns a module the diff touches. The module-to-phase map
   (`tests/module_map.txt`, one `dir phases` line each): `engine/foundation` 0 and 1, `engine/jobs` 2, `engine/rhi`
   3, `engine/renderer` and `shaders` 4, `tools/docgen` 2.5, `game/rhi_demo` 3, `game/renderer_demo` 4, `tools/shader_mint` 4.
   A touched directory missing from the map forces the full suite (fail safe).
4. **Always full** (all phases, `--all`): the closing day of every phase, any change to a `CMakeLists.txt`, `cmake/`,
   `CMakePresets.json` or `tests/phases.txt`, a change to any header under `engine/foundation` (every module includes
   them), and a release candidate.
5. **Flavors:** a scoped run uses the plain flavor (`-LE 'asan|tsan'`); `--flavor asan|tsan` runs the same phase
   selection restricted to that label (`-L asan`); `--flavor all` (used at a phase's close) runs all three. TSan keeps
   excluding `[gpu]`.
6. **Display tests** (resource lock `display`) are included when the current phase is 1 or the diff touches platform
   code (`engine/foundation/os`, window or input files); otherwise the scoped run excludes them, since they are the
   known source of load flakes (KNOWN-ISSUES) and cannot be affected.
7. The runner prints `ran X of Y tests (phases ...; flavors ...)` so a partial run can never be mistaken for the
   whole.

## 7. What a scoped run can miss, and the mitigation

| Risk | Mitigation |
|---|---|
| A behavior change reaches an earlier phase through a dependency nobody edited (e.g. the renderer changes how it calls an RHI function that nothing in `engine/rhi` changed) | Every phase's closing run is the full suite in three flavors; the day's `/code-review` also looks for cross-module effects |
| A changed build flag, compiler option or CMake variable | Any `CMakeLists.txt`/`cmake/`/preset change forces `--all` |
| A shared header (`engine/foundation`) change | Forces `--all` |
| A shared test helper (`tests/*/test_helpers.h`, fixtures) | The module map treats a change under `tests/<module>` as touching that module's phases |
| A new test file with no map line, silently unscoped | `test_phase_labels` fails on a test with no phase label |
| A mapped directory moved or renamed | `test_phase_script` checks every directory in `module_map.txt` exists; unknown touched dir forces `--all` |
| People stop running the full suite | The closing-run item is added to the end of every later phase plan; `STATUS.md` records the date and result of the last full run |

## 8. Out of scope

Test sharding across machines, caching results by input hash, and per-test timing history. If the full suite grows
past what is tolerable at a phase close, revisit then.
