# Every test must carry exactly one phase label (Phase 5, tests/phases.txt). test_phase_labels checks this build and that
# no line of the map is unused; the planted fixtures prove the check can fail, and for the right reason: a test with no
# phase label, and one with two. ctest writes a Testing/ directory next to the build it inspects, so the fixtures are
# copied into the build tree first.
set(PHASE_LABELS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

function(phase_labels_test name dir)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND} -DBUILD_DIR=${dir} -DMAP=${ENGINE_PHASE_MAP}
      ${ARGN} -P ${PHASE_LABELS_SCRIPT})
endfunction()

phase_labels_test(test_phase_labels ${CMAKE_BINARY_DIR} -DCHECK_UNUSED_PATTERNS=1)
phase_labels_test(test_phase_labels_clean ${CMAKE_CURRENT_BINARY_DIR}/fixtures/good)
phase_labels_test(test_phase_labels_planted_unlabeled ${CMAKE_CURRENT_BINARY_DIR}/fixtures/unlabeled)
phase_labels_test(test_phase_labels_planted_doubled ${CMAKE_CURRENT_BINARY_DIR}/fixtures/doubled)
# The planted failures must name the offender, so a failure for another reason (no ctest, a bad map) does not pass.
set_tests_properties(test_phase_labels_planted_unlabeled PROPERTIES
  PASS_REGULAR_EXPRESSION "no phase label: [0-9]+: forgot_a_label")
set_tests_properties(test_phase_labels_planted_doubled PROPERTIES
  PASS_REGULAR_EXPRESSION "more than one phase label: [0-9]+: in_two_phases")
engine_label_tests(test_phase_labels test_phase_labels_clean test_phase_labels_planted_unlabeled
  test_phase_labels_planted_doubled)
