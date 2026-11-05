# Phase 5 / Day 85 — Test Inventory & Scoping Design

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Phase 4 complete.

Decide how a test gets a phase, what "current phase" means, and what a scoped run includes, from measurements of the
suite as it is. Planning and a throwaway spike only: no production change.

## Tasks

- [ ] Inventory: list every ctest test (`ctest -N`) with its executable, source file and line (Catch2's `--list-tests --verbosity high` prints `file:line`) and its flavor (plain/ASan/TSan), into a table in the design spec; count tests per executable and per flavor, and the wall time of each executable alone (`foundation_tests`, `jobs_tests*`, `docgen_tests*`, `rhi_tests*`, `renderer_tests*`, `shader_mint_tests`, `shader_cook_tests`) and of the non-Catch tests (`docs_*`, `shaders_*`, `*_check*`, demos).
- [ ] Assign every source file and every non-Catch test to a phase (0, 1, 2, 2.5, 3, 4) by the rule "the phase that owns the behavior, not the directory": for example `tests/foundation/wayland_window_test.cpp` is Phase 1, `arena_allocator_test.cpp` Phase 0, `unique_ptr_test.cpp` Phase 0 (the type's tests stay with its module even though Day 80b added it), `tests/rhi/vulkan_gbuffer_test.cpp` Phase 4, `docgen_tests` Phase 2.5. Record each ambiguous file with the reason.
- [ ] Spike (kept in the scratchpad, not committed): confirm that a CMake function can generate, after a target is built, a file of `set_tests_properties(<test name> PROPERTIES LABELS ...)` lines from the `--list-tests --verbosity high` output, that CTest reads it (`TEST_INCLUDE_FILES` directory property or `catch_discover_tests`'s generated include), and that test names including the `TEST_SUFFIX` and the two `catch_discover_tests` calls per target (display-locked tests) match. Record what works and what does not.
- [ ] Confirm how `ctest -L` combines several labels with the installed CMake (4.4.5): a phase label AND a flavor label, and an OR of several phases. Record the exact command lines.
- [ ] Decide and write down: (1) the label names (`phase0` ... `phase4`, `phase2.5` spelled as a valid label), (2) where the map lives (one file, `tests/phases.txt`, lines `glob  phase`), (3) where the current phase is recorded (a one-line `CURRENT_PHASE` file at the repo root, changed in the commit that starts a phase), (4) what a scoped run is: the current phase's tests plus the tests of every module the diff touches (module-to-phase map: `engine/foundation` to 0 and 1, `engine/jobs` to 2, `engine/rhi` to 3, `engine/renderer` and `shaders` to 4, `tools/docgen` to 2.5, and so on), (5) what always forces the full suite: a phase's closing day, a change to any `CMakeLists.txt`, `cmake/` or `CMakePresets` file, a change to a header under `engine/foundation`, and a release candidate.
- [ ] Decide the flavor policy: a scoped run uses the plain flavor; `--flavor asan|tsan` adds that flavor of the same selection; the phase-closing run uses all three. Decide whether display tests (the `display` resource lock, flaky under load) are included in a scoped run (proposed: only when the diff touches platform code).
- [ ] Write the design spec `docs/superpowers/specs/2026-11-05-test-scoping-design.md`: the inventory, the phase assignments, the decisions above with their reasons, and the risk analysis (what a scoped run can miss: a behavior change that reaches an earlier phase through a dependency nobody edited, a changed build flag, a shared test helper) with the mitigation for each.
- [ ] **Documentation:** no public API. The spec is linked from `docs/README.md`.
- [ ] Update `STATUS.md`; commit (planning and spec only; no code review is required for a planning-only commit).

**Done:** the spec exists with the full inventory (every test assigned to one phase, ambiguities explained), the label mechanism is proven in a spike, the label-combination command lines are recorded, and every decision listed above is written down with its reason.
