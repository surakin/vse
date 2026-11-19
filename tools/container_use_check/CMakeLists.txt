# Container use gate (container audit series, Day 13): see check.cmake. Registered as ctest cases.
set(CONTAINER_USE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(CONTAINER_USE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(container_use_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${CONTAINER_USE_SCRIPT})
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME container_use_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CONTAINER_USE_SCRIPT})

# Planted violations (a Remove(0), an FNV prime, an upper-case FNV basis) must each be reported by line.
container_use_test(container_use_check_planted ${CONTAINER_USE_FIXTURES}/bad ${CONTAINER_USE_FIXTURES}/good/allowlist.txt)
set_tests_properties(container_use_check_planted PROPERTIES WILL_FAIL TRUE)
container_use_test(container_use_check_planted_count ${CONTAINER_USE_FIXTURES}/bad ${CONTAINER_USE_FIXTURES}/good/allowlist.txt)
set_tests_properties(container_use_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:1:.*engine/leak.cpp:2:.*engine/leak.cpp:3:.*3 problem")
# Comments, string literals and the foundation hash are not violations.
container_use_test(container_use_check_clean ${CONTAINER_USE_FIXTURES}/good ${CONTAINER_USE_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails.
container_use_test(container_use_check_stale ${CONTAINER_USE_FIXTURES}/good ${CONTAINER_USE_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(container_use_check_stale PROPERTIES WILL_FAIL TRUE)

engine_label_tests(container_use_check container_use_check_planted container_use_check_planted_count container_use_check_clean
                   container_use_check_stale)
