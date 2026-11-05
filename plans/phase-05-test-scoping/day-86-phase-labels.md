# Phase 5 / Day 86 — Phase Labels for Every Test

**Phase:** [Phase 5 — Test Scoping](../phase-05-test-scoping.md)
**Depends on:** Day 85.

Make the label mechanism real: one map file, one CMake function, every test labeled, and a test that fails when one is
not.

## Tasks

- [ ] Write `tests/phases.txt` from the Day 85 assignments: one line per source file or directory glob, `<glob>  <phase>`, with comments for the ambiguous ones. The file is the only place a test's phase is decided.
- [ ] Write `cmake/EngineTestPhases.cmake` with `engine_discover_tests(<target> [TEST_SUFFIX s] [TEST_SPEC spec] [PROPERTIES ...])`: it calls `catch_discover_tests` as before and adds a post-build step that runs the executable with `--list-tests --verbosity high`, maps each test's source file through `tests/phases.txt`, and writes a generated include file of `set_tests_properties(... LABELS phaseN)` lines (appending to any labels the caller passes); the include file is registered so CTest reads it. A test whose file matches no line, or two different phases, makes the post-build step fail with the file name.
- [ ] Switch `tests/foundation`, `tests/jobs`, `tests/docgen`, `tests/shader_mint` and the shader_cook test target to `engine_discover_tests`, keeping their existing labels, `TEST_SUFFIX`, `TEST_SPEC` and `RESOURCE_LOCK` settings (the two foundation calls for display tests included). The test counts and names must be identical before and after (compare `ctest -N` output).
- [ ] Label the non-Catch tests by hand in their CMake files from the map's non-Catch section: `docs_*` Phase 2.5, `rhi_boundary_check*` Phase 3, `raw_array_check*` and `raw_new_check*` Phase 4, `shaders_check` and `shaders_portability` Phase 4, `renderer_demo_*` Phase 4, `rhi_demo_*` Phase 3.
- [ ] Add the `test_phase_labels` ctest (a CMake script run through `ctest`): it reads `ctest --show-only=json-v1` for the build, and fails, naming them, if any test (other than itself) has no `phaseN` label or more than one. Test it with a planted unlabeled test (a fixture in the same style as `rhi_boundary_check_planted`) so the failure path is covered.
- [ ] Record the number of tests per phase (plain flavor) in the day file; check they add up to the total.
- [ ] Run the full suite once (plain, ASan, TSan) to prove nothing moved: same pass count as before the change.
- [ ] **Documentation:** the map file and the function carry header comments that say how to add a test; `docs/guide/building-and-testing.md` gets a short "Test phases" section (the full workflow is Day 89). `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** every discovered test in the foundation, jobs, docgen and shader_mint/cook executables and every non-Catch test carries exactly one phase label, `test_phase_labels` enforces it (with a planted-failure test), and the full suite passes unchanged.
