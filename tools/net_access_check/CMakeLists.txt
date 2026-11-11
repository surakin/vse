# Network access gate: see check.cmake. Registered as ctest cases (label: net).
set(NET_ACCESS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(NET_ACCESS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(net_access_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${NET_ACCESS_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS net)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME net_access_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${NET_ACCESS_SCRIPT})
set_tests_properties(net_access_check PROPERTIES LABELS net)

# Planted violations (calls, headers of both operating systems, a violation in tools/) must be reported, each by line.
net_access_test(net_access_check_planted ${NET_ACCESS_FIXTURES}/bad ${NET_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(net_access_check_planted PROPERTIES WILL_FAIL TRUE)
net_access_test(net_access_check_planted_count ${NET_ACCESS_FIXTURES}/bad ${NET_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(net_access_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:1:.*engine/leak.cpp:2:.*engine/leak.cpp:4:.*engine/leak.cpp:6:.*engine/leak.cpp:7:.*engine/leak.cpp:8:.*engine/leak.cpp:9:.*engine/win_leak.cpp:1:.*engine/win_leak.cpp:2:.*engine/win_leak.cpp:3:.*tools/tool.cpp:1:.*tools/tool.cpp:2:.*tools/tool.cpp:3:.*stale entry engine/app/allowed.cpp.*14 problem")

# A clean tree passes: the backend directory is exempt; comments, strings, member calls and SIGNAL-SAFE regions are not violations; an allow-listed file is covered.
net_access_test(net_access_check_clean ${NET_ACCESS_FIXTURES}/good ${NET_ACCESS_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
net_access_test(net_access_check_stale ${NET_ACCESS_FIXTURES}/good ${NET_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(net_access_check_stale PROPERTIES WILL_FAIL TRUE)
net_access_test(net_access_check_stale_message ${NET_ACCESS_FIXTURES}/good ${NET_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(net_access_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/app/gone.cpp")
net_access_test(net_access_check_no_reason ${NET_ACCESS_FIXTURES}/good ${NET_ACCESS_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(net_access_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(net_access_check net_access_check_planted net_access_check_planted_count net_access_check_clean
                   net_access_check_stale net_access_check_stale_message net_access_check_no_reason)
