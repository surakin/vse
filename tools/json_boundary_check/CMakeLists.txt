# yyjson boundary gate: see check.cmake. Registered as ctest cases (label: asset).
set(YYJSON_BOUNDARY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(YYJSON_BOUNDARY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(json_boundary_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${YYJSON_BOUNDARY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS asset)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME json_boundary_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${YYJSON_BOUNDARY_SCRIPT})
set_tests_properties(json_boundary_check PROPERTIES LABELS asset)

# Planted violations (an include, a function, a type, a macro, a link, in engine/, tests/) must be reported, each by line.
json_boundary_test(json_boundary_check_planted ${YYJSON_BOUNDARY_FIXTURES}/bad ${YYJSON_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(json_boundary_check_planted PROPERTIES WILL_FAIL TRUE)
json_boundary_test(json_boundary_check_planted_count ${YYJSON_BOUNDARY_FIXTURES}/bad ${YYJSON_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(json_boundary_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/CMakeLists.txt:2:.*engine/CMakeLists.txt:6:.*engine/leak.cpp:1:.*engine/leak.cpp:2:.*engine/leak.cpp:3:.*engine/other/macro.h:1:.*tests/leak_test.cpp:1:.*7 problem")

# A clean tree passes: the yyjson directory may name yyjson; comments, strings and a similarly named public header are not violations.
json_boundary_test(json_boundary_check_clean ${YYJSON_BOUNDARY_FIXTURES}/good ${YYJSON_BOUNDARY_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
json_boundary_test(json_boundary_check_stale ${YYJSON_BOUNDARY_FIXTURES}/good ${YYJSON_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(json_boundary_check_stale PROPERTIES WILL_FAIL TRUE)
json_boundary_test(json_boundary_check_stale_message ${YYJSON_BOUNDARY_FIXTURES}/good ${YYJSON_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(json_boundary_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/app/")
json_boundary_test(json_boundary_check_no_reason ${YYJSON_BOUNDARY_FIXTURES}/good ${YYJSON_BOUNDARY_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(json_boundary_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(json_boundary_check json_boundary_check_planted json_boundary_check_planted_count json_boundary_check_clean
                   json_boundary_check_stale json_boundary_check_stale_message json_boundary_check_no_reason)
