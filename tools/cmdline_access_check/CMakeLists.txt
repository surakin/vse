# Command-line access gate: see check.cmake. Registered as ctest cases (label: foundation, plus the phase label from tests/phases.txt).
set(CMDLINE_ACCESS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(CMDLINE_ACCESS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(cmdline_access_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${CMDLINE_ACCESS_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS foundation)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME cmdline_access_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMDLINE_ACCESS_SCRIPT})
set_tests_properties(cmdline_access_check PROPERTIES LABELS foundation)

# Planted violations (an index, an expression index; a declaration and a comment are not violations) are reported by line.
cmdline_access_test(cmdline_access_check_planted ${CMDLINE_ACCESS_FIXTURES}/bad ${CMDLINE_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(cmdline_access_check_planted PROPERTIES WILL_FAIL TRUE)
cmdline_access_test(cmdline_access_check_planted_count ${CMDLINE_ACCESS_FIXTURES}/bad ${CMDLINE_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(cmdline_access_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "tools/leak.cpp:4:.*tools/leak.cpp:5:.*tools/leak.cpp:6:.*stale entry engine/boot/.*4 problem")

# A clean tree passes: comments, a declaration, the allowed directory.
cmdline_access_test(cmdline_access_check_clean ${CMDLINE_ACCESS_FIXTURES}/good ${CMDLINE_ACCESS_FIXTURES}/good/allowlist.txt)
cmdline_access_test(cmdline_access_check_stale ${CMDLINE_ACCESS_FIXTURES}/good ${CMDLINE_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(cmdline_access_check_stale PROPERTIES WILL_FAIL TRUE)
cmdline_access_test(cmdline_access_check_no_reason ${CMDLINE_ACCESS_FIXTURES}/good ${CMDLINE_ACCESS_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(cmdline_access_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(cmdline_access_check cmdline_access_check_planted cmdline_access_check_planted_count cmdline_access_check_clean
                   cmdline_access_check_stale cmdline_access_check_no_reason)
