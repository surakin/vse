# Standard-library gate: see check.cmake. Registered as ctest cases (label: types).
set(STD_USAGE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(STD_USAGE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(std_usage_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} "-DDIRS=engine;tools" -P ${STD_USAGE_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS types)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME std_usage_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${STD_USAGE_SCRIPT})
set_tests_properties(std_usage_check PROPERTIES LABELS types)

# Planted violations (an include, std::, an unqualified C call, a raw integer spelling, a header outside its module's list, a violation in
# tools/, and in an Objective-C++ file a standard header by #include and by #import, Phase 21 Day 286) are reported by line; comments, literals and member calls are not.
std_usage_test(std_usage_check_planted ${STD_USAGE_FIXTURES}/bad ${STD_USAGE_FIXTURES}/good/allowlist.txt)
set_tests_properties(std_usage_check_planted PROPERTIES WILL_FAIL TRUE)
std_usage_test(std_usage_check_planted_lines ${STD_USAGE_FIXTURES}/bad ${STD_USAGE_FIXTURES}/good/allowlist.txt)
set_tests_properties(std_usage_check_planted_lines PROPERTIES
  PASS_REGULAR_EXPRESSION "lang/wrong.h:3:.*not in the header list.*os/macos/uses_vector.mm:2:.*<vector>.*os/macos/uses_vector.mm:3:.*<string>.*engine/leak.cpp:1:.*engine/leak.cpp:6:.*engine/leak.cpp:7:.*engine/leak.cpp:8:.*engine/leak.cpp:9:.*engine/leak.cpp:10:.*tools/tool.cpp:1:.*10 problem")

# A clean tree passes.
std_usage_test(std_usage_check_clean ${STD_USAGE_FIXTURES}/good ${STD_USAGE_FIXTURES}/good/allowlist.txt)
# A stale entry, a stale header, a header the file does not list and an entry without a reason all fail.
std_usage_test(std_usage_check_stale ${STD_USAGE_FIXTURES}/good ${STD_USAGE_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(std_usage_check_stale PROPERTIES WILL_FAIL TRUE)
std_usage_test(std_usage_check_stale_message ${STD_USAGE_FIXTURES}/good ${STD_USAGE_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(std_usage_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale header <type_traits>.*stale entry engine/app/clean.cpp")
std_usage_test(std_usage_check_wrong_header ${STD_USAGE_FIXTURES}/good ${STD_USAGE_FIXTURES}/good/wrong_header_allowlist.txt)
set_tests_properties(std_usage_check_wrong_header PROPERTIES WILL_FAIL TRUE)
std_usage_test(std_usage_check_no_reason ${STD_USAGE_FIXTURES}/good ${STD_USAGE_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(std_usage_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(std_usage_check std_usage_check_planted std_usage_check_planted_lines std_usage_check_clean std_usage_check_stale
                   std_usage_check_stale_message std_usage_check_wrong_header std_usage_check_no_reason)
