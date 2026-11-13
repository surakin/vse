# Every test must carry exactly one phase label (Phase 6, tests/phases.txt). test_phase_labels checks this build, that
# no line of the map is unused and that each phase has the sanitizer flavors the map lists. The planted fixtures prove
# each check can fail, and for the right reason: a test with no phase label, one with two, a flavor the map does not
# list and a listed flavor with no tests. ctest writes a Testing/ directory next to the build it inspects, so the
# fixtures are copied into the build tree first.
set(PHASE_LABELS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

function(phase_labels_test name dir)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND} -DBUILD_DIR=${dir} -DMAP=${ENGINE_PHASE_MAP}
      ${ARGN} -P ${PHASE_LABELS_SCRIPT})
endfunction()

# The unused-line check runs on Linux only: on Windows, which builds one layer more each day (Phase 13), a `test:` line for a test of a layer
# not ported yet matches nothing without being stale.
if(ENGINE_PLATFORM STREQUAL "linux")
  set(_check_unused -DCHECK_UNUSED=1)
else()
  set(_check_unused "")
endif()
# The flavors this configuration builds: the map's per-platform lines say what the platform has, and a configuration without a sanitizer
# (clang-cl, which has no ASan flavor yet) is not asked for its tests.
set(_built_flavors plain)
if(ENGINE_HAVE_ASAN)
  string(APPEND _built_flavors ",asan")
endif()
if(ENGINE_HAVE_TSAN)
  string(APPEND _built_flavors ",tsan")
endif()
phase_labels_test(test_phase_labels ${CMAKE_BINARY_DIR} -DCHECK_FLAVORS=1 -DPLATFORM=${ENGINE_PLATFORM} -DBUILT_FLAVORS=${_built_flavors}
  ${_check_unused} -DCURRENT_PHASE_FILE=${CMAKE_SOURCE_DIR}/CURRENT_PHASE -DSIDE_PHASES_FILE=${CMAKE_SOURCE_DIR}/SIDE_PHASES)
# Side phases (Phase 21 Day 286): a map phase above the current one fails unless SIDE_PHASES lists it. Planted: the real map with the
# current phase at 3 (phase 4 and up are above it), and with no side phases (phase 21 is above the current phase).
phase_labels_test(test_phase_labels_planted_above_current ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good
  -DCURRENT_PHASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/current_phase_3.txt -DSIDE_PHASES_FILE=${CMAKE_SOURCE_DIR}/SIDE_PHASES)
phase_labels_test(test_phase_labels_planted_no_side_phase ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good
  -DCURRENT_PHASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/current_phase_3.txt -DSIDE_PHASES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/no_side_phases.txt)
set_tests_properties(test_phase_labels_planted_above_current PROPERTIES
  PASS_REGULAR_EXPRESSION "phase4 is above the current phase 3 and is not a side phase")
set_tests_properties(test_phase_labels_planted_no_side_phase PROPERTIES
  PASS_REGULAR_EXPRESSION "phase21 is above the current phase [0-9]+ and is not a side phase")
# A stale or mistyped SIDE_PHASES entry (one at or below the current phase, one with no map lines) fails; so does an empty CURRENT_PHASE, with
# the file named (Day 286 review).
phase_labels_test(test_phase_labels_planted_stale_side_phase ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good
  -DCURRENT_PHASE_FILE=${CMAKE_SOURCE_DIR}/CURRENT_PHASE -DSIDE_PHASES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/stale_side_phases.txt)
set_tests_properties(test_phase_labels_planted_stale_side_phase PROPERTIES
  PASS_REGULAR_EXPRESSION "side phase 3 is not above the current phase [0-9]+.*side phase 62 has no lines in the map")
phase_labels_test(test_phase_labels_planted_empty_current ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good
  -DCURRENT_PHASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/no_side_phases.txt -DSIDE_PHASES_FILE=${CMAKE_SOURCE_DIR}/SIDE_PHASES)
set_tests_properties(test_phase_labels_planted_empty_current PROPERTIES
  PASS_REGULAR_EXPRESSION "no_side_phases.txt: '' is not a phase number")
engine_label_tests(test_phase_labels_planted_above_current test_phase_labels_planted_no_side_phase test_phase_labels_planted_stale_side_phase
  test_phase_labels_planted_empty_current)
phase_labels_test(test_phase_labels_clean ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good)
phase_labels_test(test_phase_labels_planted_unlabeled ${CMAKE_CURRENT_BINARY_DIR}/fixtures/unlabeled)
phase_labels_test(test_phase_labels_planted_doubled ${CMAKE_CURRENT_BINARY_DIR}/fixtures/doubled)
# The planted failures must name the offender, so a failure for another reason (no ctest, a bad map) does not pass.
set_tests_properties(test_phase_labels_planted_unlabeled PROPERTIES
  PASS_REGULAR_EXPRESSION "no phase label: [0-9]+: forgot_a_label")
set_tests_properties(test_phase_labels_planted_doubled PROPERTIES
  PASS_REGULAR_EXPRESSION "more than one phase label: [0-9]+: in_two_phases")
phase_labels_test(test_phase_labels_planted_extra_flavor ${CMAKE_CURRENT_BINARY_DIR}/fixtures/extra_flavor -DCHECK_FLAVORS=1 -DPLATFORM=linux)
phase_labels_test(test_phase_labels_planted_missing_flavor ${CMAKE_CURRENT_BINARY_DIR}/fixtures/missing_flavor -DCHECK_FLAVORS=1 -DPLATFORM=linux)
set_tests_properties(test_phase_labels_planted_extra_flavor PROPERTIES
  PASS_REGULAR_EXPRESSION "phase1 has 1 asan tests, but the map does not list that flavor")
set_tests_properties(test_phase_labels_planted_missing_flavor PROPERTIES
  PASS_REGULAR_EXPRESSION "phase2 has no tsan tests, but the map lists that flavor")
engine_label_tests(test_phase_labels_planted_extra_flavor test_phase_labels_planted_missing_flavor)
# The same fixture, for a configuration that does not build tsan (Day 150): a flavor it cannot build is not demanded. The fixture lacks
# the other phases' tests, so the check still fails; it must report those (it ran) and not phase 2's tsan.
phase_labels_test(test_phase_labels_built_flavors ${CMAKE_CURRENT_BINARY_DIR}/fixtures/missing_flavor -DCHECK_FLAVORS=1 -DPLATFORM=linux
  -DBUILT_FLAVORS=plain,asan)
set_tests_properties(test_phase_labels_built_flavors PROPERTIES
  PASS_REGULAR_EXPRESSION "phase0 has no plain tests, but the map lists that flavor"
  FAIL_REGULAR_EXPRESSION "phase2 has no tsan tests")
engine_label_tests(test_phase_labels_built_flavors)
# Platform-qualified `flavors` lines (Phase 13 Day 148): the real map gives phase 13 its own flavors per platform (Windows plain,asan;
# Linux plain,asan,tsan since Day 162). One fixture with an asan test of phase 13, checked as both platforms, so either machine tests
# both rules: each platform must have the plain tests its own line lists.
phase_labels_test(test_phase_labels_planted_platform_linux ${CMAKE_CURRENT_BINARY_DIR}/fixtures/platform_only -DCHECK_FLAVORS=1 -DPLATFORM=linux)
phase_labels_test(test_phase_labels_planted_platform_windows ${CMAKE_CURRENT_BINARY_DIR}/fixtures/platform_only -DCHECK_FLAVORS=1 -DPLATFORM=windows)
set_tests_properties(test_phase_labels_planted_platform_linux PROPERTIES
  PASS_REGULAR_EXPRESSION "phase13 has no plain tests, but the map lists that flavor")
set_tests_properties(test_phase_labels_planted_platform_windows PROPERTIES
  PASS_REGULAR_EXPRESSION "phase13 has no plain tests, but the map lists that flavor")
engine_label_tests(test_phase_labels_planted_platform_linux test_phase_labels_planted_platform_windows)
engine_label_tests(test_phase_labels test_phase_labels_clean test_phase_labels_planted_unlabeled
  test_phase_labels_planted_doubled)
