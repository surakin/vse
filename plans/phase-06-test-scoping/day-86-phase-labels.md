# Phase 6 / Day 86 — Phase Labels for Every Test

**Phase:** [Phase 6 — Test Scoping](../phase-06-test-scoping.md)
**Depends on:** Day 85.

Make the label mechanism real: one map file, one CMake function, every test labeled, and a test that fails when one is
not.

## Tasks

- [x] Write `tests/phases.txt` from the Day 85 assignments: one line per source file or directory glob, `<glob>  <phase>`, with comments for the ambiguous ones. The file is the only place a test's phase is decided.
- [x] Write `cmake/EngineTestPhases.cmake` with `engine_discover_tests(<target> [TEST_SUFFIX s] [TEST_SPEC spec] [PROPERTIES ...])`: it calls `catch_discover_tests` as before and adds a post-build step that runs the executable with `--list-tests --verbosity high`, maps each test's source file through `tests/phases.txt`, and writes a generated include file of `set_tests_properties(... LABELS phaseN)` lines (appending to any labels the caller passes); the include file is registered so CTest reads it. A test whose file matches no line, or two different phases, makes the post-build step fail with the file name.
- [x] Switch `tests/foundation`, `tests/jobs`, `tests/docgen`, `tests/shader_mint` and the shader_cook test target to `engine_discover_tests`, keeping their existing labels, `TEST_SUFFIX`, `TEST_SPEC` and `RESOURCE_LOCK` settings (the two foundation calls for display tests included). The test counts and names must be identical before and after (compare `ctest -N` output).
- [x] Label the non-Catch tests by hand in their CMake files from the map's non-Catch section: `docs_*` Phase 3, `rhi_boundary_check*` Phase 4, `raw_array_check*` and `raw_new_check*` Phase 5, `shaders_check` and `shaders_portability` Phase 5, `renderer_demo_*` Phase 5, `rhi_demo_*` Phase 4.
- [x] Add the `test_phase_labels` ctest (a CMake script run through `ctest`): it reads `ctest --show-only=json-v1` for the build, and fails, naming them, if any test (other than itself) has no `phaseN` label or more than one. Test it with a planted unlabeled test (a fixture in the same style as `rhi_boundary_check_planted`) so the failure path is covered.
- [x] Record the number of tests per phase (plain flavor) in the day file; check they add up to the total.
- [x] Run the full suite once (plain, ASan, TSan) to prove nothing moved: same pass count as before the change.
- [x] **Documentation:** the map file and the function carry header comments that say how to add a test; `docs/guide/building-and-testing.md` gets a short "Test phases" section (the full workflow is Day 89). `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** every discovered test in the foundation, jobs, docgen and shader_mint/cook executables and every non-Catch test carries exactly one phase label, `test_phase_labels` enforces it (with a planted-failure test), and the full suite passes unchanged.

## Result

Deviations from the plan above, decided when the spike (Day 85) showed Catch2 v3 has no per-file tag and that a second
script would have to repeat `CatchAddTests.cmake`'s name escaping:

- The label comes from a small marked patch to the vendored `CatchAddTests.cmake`/`Catch.cmake` (`PHASE_MAP`, `PHASE_ROOT`,
  `[engine patch]`; listed in `third_party/catch2/VENDORED.md`), not from a post-build generated include. `engine_discover_tests`
  passes the map. A Catch2 test whose file has no line in `tests/phases.txt` **stops the build**, naming the file (checked by
  removing the `tests/renderer/` line).
- The patch also fixes `LABELS a b` lists: before, `PROPERTIES LABELS "docgen;asan"` made `asan` a bogus property name, so
  the ASan docgen tests carried only `docgen` and no `SKIP_RETURN_CODE`; they now carry `asan` and `docgen` (+168 tests
  selected by `ctest -L asan`).
- The `tests/rhi` and `tests/renderer` call sites and their file assignments were done today too (the check needs every
  test labeled); Day 87 keeps the no-GPU check, the flavor coverage and the baseline times. There is no `shader_cook_tests`
  target any more (it is `shader_mint_tests`).
- Non-Catch tests are labeled by `engine_label_tests(<names>)` from `test:<glob>` lines in the same map, so the map stays
  the only place a phase is decided. `test_phase_labels` reads three-plus `ctest -N -L` selections instead of
  `--show-only=json-v1` (`string(JSON)` over 1.8k tests is quadratic in CMake) and also fails on a map line that matches nothing.
- Window tests gained the label `display` (foundation and rhi), for the runner of Day 88.

Tests per phase (plain + ASan + TSan, all 1843): phase0 87, phase1 63, phase2 232, phase3 340, phase4 580, phase5 537,
phase6 4 (`test_phase_labels*`); sum 1843. Test names are identical before and after (`ctest -N` diff, 1839 names). Full
suite `ctest -j16`: 1843/1843 pass (4 skipped as before), 65 s. Docs: `ctest -L docs` 4/4. Guide: "Test phases" section added to
`docs/guide/building-and-testing.md`.

## Post-implementation code review

`/code-review high` on the day's diff, 9 findings:

1. Fixture `Testing/Temporary/LastTest.log` files staged (ctest writes next to the build it inspects). **Fixed**: the
   fixtures are copied into the build tree at configure time and the logs deleted.
2. `ADD_TAGS_AS_LABELS` would overwrite the phase label. **Fixed**: the tag block merges with `test_labels` (nothing uses
   the option today).
3. An all-upper-case label would be read as a property name. **Declined**, documented in `cmake/EngineTestPhases.cmake`: the
   flat `PROPERTIES` list cannot carry the structure, and every label here is lower case.
4. `WILL_FAIL` planted tests passed for any failure. **Fixed**: both planted tests now require the offender's name in the
   output (`no phase label: <n>: forgot_a_label`, `more than one phase label: <n>: in_two_phases`).
5. A map line matching nothing was never reported (`raw_array_check*` named a test that does not exist). **Fixed**:
   `test_phase_labels` fails on an unused line (verified with a planted `test:nothing_here*`); the dead line was removed.
   A test carrying the same phase label twice is **declined**: ctest's `-L` cannot tell, and nothing can produce it.
6. `engine_label_tests` produced a leading empty label (`;phaseN`). **Fixed**.
7. `engine_phase_lookup` is linear per test. **Declined**: a full reconfigure+rebuild of every test target took 5 s of
   wall time; `test_phase_labels` runs in 0.6 s.
8. A comment named `scripts/test-phase.sh`, which does not exist until Day 88. **Fixed**.
9. Process items (STATUS, day-file ticks, KNOWN-ISSUES, plan deviations). **Done in this commit**.
