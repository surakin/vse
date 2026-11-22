# Documentation phase gate: see check.cmake. Registered as ctest cases (label: docs_phase).
set(DOCS_PHASE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(DOCS_PHASE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# The real tree must be clean.
add_test(NAME docs_phase_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${DOCS_PHASE_SCRIPT})
set_tests_properties(docs_phase_check PROPERTIES LABELS docs_phase)

# Planted references (a guide line, a wrapped one, a header doc comment) must be reported, each by line; a plain comment and the word
# "phase" without a number are not references.
add_test(NAME docs_phase_check_planted COMMAND ${CMAKE_COMMAND} -DROOT=${DOCS_PHASE_FIXTURES}/bad -P ${DOCS_PHASE_SCRIPT})
set_tests_properties(docs_phase_check_planted PROPERTIES WILL_FAIL TRUE LABELS docs_phase)
add_test(NAME docs_phase_check_planted_count COMMAND ${CMAKE_COMMAND} -DROOT=${DOCS_PHASE_FIXTURES}/bad -P ${DOCS_PHASE_SCRIPT})
set_tests_properties(docs_phase_check_planted_count PROPERTIES LABELS docs_phase
  PASS_REGULAR_EXPRESSION "docs/guide/page.md:1:.*docs/guide/page.md:3:.*engine/leak.h:1:.*engine/leak.h:3:.*4 problem")
add_test(NAME docs_phase_check_clean COMMAND ${CMAKE_COMMAND} -DROOT=${DOCS_PHASE_FIXTURES}/good -P ${DOCS_PHASE_SCRIPT})
set_tests_properties(docs_phase_check_clean PROPERTIES LABELS docs_phase)

engine_label_tests(docs_phase_check docs_phase_check_planted docs_phase_check_planted_count docs_phase_check_clean)
