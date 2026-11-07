# Standard-container gate: see check.cmake. Registered as ctest cases (label: types).
set(STD_CONTAINER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(STD_CONTAINER_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(std_container_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} "-DDIRS=engine;tools" -P ${STD_CONTAINER_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS types)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME std_container_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${STD_CONTAINER_SCRIPT})
set_tests_properties(std_container_check PROPERTIES LABELS types)

# Planted violations (an include, containers, smart pointers, algorithms, raw integer spellings, a violation in tools/) must be
# reported by line; comments, string literals and allowed std:: names are not.
std_container_test(std_container_check_planted ${STD_CONTAINER_FIXTURES}/bad ${STD_CONTAINER_FIXTURES}/good/allowlist.txt)
set_tests_properties(std_container_check_planted PROPERTIES WILL_FAIL TRUE)
std_container_test(std_container_check_planted_count ${STD_CONTAINER_FIXTURES}/bad ${STD_CONTAINER_FIXTURES}/good/allowlist.txt)
set_tests_properties(std_container_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:1:.*engine/leak.cpp:5:.*engine/leak.cpp:6:.*engine/leak.cpp:7:.*engine/leak.cpp:8:.*engine/leak.cpp:9:.*engine/leak.cpp:10:.*engine/leak.cpp:11:.*engine/leak.cpp:13:.*tools/tool.cpp:1:.*stale entry engine/old/.*11 problem")

# A clean tree passes: comments, literals, concurrency primitives and C library calls are not violations, and an allowed directory is skipped.
std_container_test(std_container_check_clean ${STD_CONTAINER_FIXTURES}/good ${STD_CONTAINER_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
std_container_test(std_container_check_stale ${STD_CONTAINER_FIXTURES}/good ${STD_CONTAINER_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(std_container_check_stale PROPERTIES WILL_FAIL TRUE)
std_container_test(std_container_check_stale_message ${STD_CONTAINER_FIXTURES}/good ${STD_CONTAINER_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(std_container_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/clean.cpp")
std_container_test(std_container_check_no_reason ${STD_CONTAINER_FIXTURES}/good ${STD_CONTAINER_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(std_container_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(std_container_check std_container_check_planted std_container_check_planted_count std_container_check_clean
                   std_container_check_stale std_container_check_stale_message std_container_check_no_reason)
