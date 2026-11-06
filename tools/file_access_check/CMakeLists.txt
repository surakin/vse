# File access gate: see check.cmake. Registered as ctest cases (label: io).
set(FILE_ACCESS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(FILE_ACCESS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(file_access_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${FILE_ACCESS_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS io)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME file_access_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${FILE_ACCESS_SCRIPT})
set_tests_properties(file_access_check PROPERTIES LABELS io)

# Planted violations (stdio, POSIX, directory, stream and filesystem calls, a violation in tools/) must be reported, each by line.
file_access_test(file_access_check_planted ${FILE_ACCESS_FIXTURES}/bad ${FILE_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(file_access_check_planted PROPERTIES WILL_FAIL TRUE)
file_access_test(file_access_check_planted_count ${FILE_ACCESS_FIXTURES}/bad ${FILE_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(file_access_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:1:.*engine/leak.cpp:3:.*engine/leak.cpp:4:.*engine/leak.cpp:5:.*engine/leak.cpp:6:.*engine/leak.cpp:7:.*engine/leak.cpp:8:.*engine/leak.cpp:9:.*engine/leak.cpp:11:.*engine/leak.cpp:12:.*tools/tool.cpp:1:.*stale entry engine/foundation/io/linux/.*12 problem")

# A clean tree passes: the backend directory is allowed, comments, strings, member calls, file-layer functions and SIGNAL-SAFE regions are not violations.
file_access_test(file_access_check_clean ${FILE_ACCESS_FIXTURES}/good ${FILE_ACCESS_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
file_access_test(file_access_check_stale ${FILE_ACCESS_FIXTURES}/good ${FILE_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(file_access_check_stale PROPERTIES WILL_FAIL TRUE)
file_access_test(file_access_check_stale_message ${FILE_ACCESS_FIXTURES}/good ${FILE_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(file_access_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/app/")
file_access_test(file_access_check_no_reason ${FILE_ACCESS_FIXTURES}/good ${FILE_ACCESS_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(file_access_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(file_access_check file_access_check_planted file_access_check_planted_count file_access_check_clean
                   file_access_check_stale file_access_check_stale_message file_access_check_no_reason)
