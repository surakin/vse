# Configuration access gate: see check.cmake. Registered as ctest cases (label: settings).
set(CONFIG_ACCESS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(CONFIG_ACCESS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(config_access_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${CONFIG_ACCESS_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS settings)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME config_access_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CONFIG_ACCESS_SCRIPT})
set_tests_properties(config_access_check PROPERTIES LABELS settings)

# Planted violations (getenv, std::getenv, a --flag string; comments are not violations) must be reported by line.
config_access_test(config_access_check_planted ${CONFIG_ACCESS_FIXTURES}/bad ${CONFIG_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(config_access_check_planted PROPERTIES WILL_FAIL TRUE)
config_access_test(config_access_check_planted_count ${CONFIG_ACCESS_FIXTURES}/bad ${CONFIG_ACCESS_FIXTURES}/good/allowlist.txt)
set_tests_properties(config_access_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:4:.*engine/leak.cpp:5:.*engine/leak.cpp:7:.*engine/leak.cpp:10:.*engine/win_leak.cpp:1:.*engine/win_leak.cpp:2:.*engine/win_leak.cpp:3:.*engine/win_leak.cpp:4:.*stale entry engine/boot/.*9 problem")

# A clean tree passes: comments, the allowed directory.
config_access_test(config_access_check_clean ${CONFIG_ACCESS_FIXTURES}/good ${CONFIG_ACCESS_FIXTURES}/good/allowlist.txt)
config_access_test(config_access_check_stale ${CONFIG_ACCESS_FIXTURES}/good ${CONFIG_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(config_access_check_stale PROPERTIES WILL_FAIL TRUE)
config_access_test(config_access_check_stale_message ${CONFIG_ACCESS_FIXTURES}/good ${CONFIG_ACCESS_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(config_access_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/clean.cpp")
config_access_test(config_access_check_no_reason ${CONFIG_ACCESS_FIXTURES}/good ${CONFIG_ACCESS_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(config_access_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(config_access_check config_access_check_planted config_access_check_planted_count config_access_check_clean
                   config_access_check_stale config_access_check_stale_message config_access_check_no_reason)
