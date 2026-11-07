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

phase_labels_test(test_phase_labels ${CMAKE_BINARY_DIR} -DCHECK_FLAVORS=1 -DPLATFORM=${ENGINE_PLATFORM} -DCHECK_UNUSED=1)
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
# Platform-qualified `flavors` lines (Phase 13 Day 148): the real map says phase 13 has plain tests on Windows only. One fixture with an
# asan test of phase 13, checked as both platforms, so either machine tests both rules: on Linux the phase may have no tests at all, on
# Windows it must have the plain tests its line lists.
phase_labels_test(test_phase_labels_planted_platform_linux ${CMAKE_CURRENT_BINARY_DIR}/fixtures/platform_only -DCHECK_FLAVORS=1 -DPLATFORM=linux)
phase_labels_test(test_phase_labels_planted_platform_windows ${CMAKE_CURRENT_BINARY_DIR}/fixtures/platform_only -DCHECK_FLAVORS=1 -DPLATFORM=windows)
set_tests_properties(test_phase_labels_planted_platform_linux PROPERTIES
  PASS_REGULAR_EXPRESSION "phase13 has 1 asan tests, but the map does not list that flavor")
set_tests_properties(test_phase_labels_planted_platform_windows PROPERTIES
  PASS_REGULAR_EXPRESSION "phase13 has no plain tests, but the map lists that flavor")
engine_label_tests(test_phase_labels_planted_platform_linux test_phase_labels_planted_platform_windows)
engine_label_tests(test_phase_labels test_phase_labels_clean test_phase_labels_planted_unlabeled
  test_phase_labels_planted_doubled)
