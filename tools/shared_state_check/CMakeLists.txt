# Shared-state gate: see check.cmake (Phase 23 Day 345).
set(SHARED_STATE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(SHARED_STATE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(shared_state_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${SHARED_STATE_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS engine_library)
endfunction()

shared_state_test(shared_state_check ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/allowlist.txt)
shared_state_test(shared_state_check_planted ${SHARED_STATE_FIXTURES}/bad ${SHARED_STATE_FIXTURES}/good/allowlist.txt)
set_tests_properties(shared_state_check_planted PROPERTIES WILL_FAIL TRUE)
shared_state_test(shared_state_check_planted_count ${SHARED_STATE_FIXTURES}/bad ${SHARED_STATE_FIXTURES}/good/allowlist.txt)
set_tests_properties(shared_state_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.h:7:.*engine/leak.h:10:.*engine/leak.h:14:.*engine/leak.h:15:.*engine/leak.h:18:.*5 violation")
shared_state_test(shared_state_check_clean ${SHARED_STATE_FIXTURES}/good ${SHARED_STATE_FIXTURES}/good/allowlist.txt)
shared_state_test(shared_state_check_stale ${SHARED_STATE_FIXTURES}/good ${SHARED_STATE_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(shared_state_check_stale PROPERTIES WILL_FAIL TRUE)
shared_state_test(shared_state_check_stale_message ${SHARED_STATE_FIXTURES}/good ${SHARED_STATE_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(shared_state_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/clean.h")
engine_label_tests(shared_state_check shared_state_check_planted shared_state_check_planted_count shared_state_check_clean shared_state_check_stale
                   shared_state_check_stale_message)
