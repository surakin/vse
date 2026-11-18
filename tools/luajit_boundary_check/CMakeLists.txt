# LuaJIT boundary gate (Phase 27 Day 445): see check.cmake. Registered as ctest cases (label: script).
set(LUAJIT_BOUNDARY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(LUAJIT_BOUNDARY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(luajit_boundary_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${LUAJIT_BOUNDARY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS script)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME luajit_boundary_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${LUAJIT_BOUNDARY_SCRIPT})
set_tests_properties(luajit_boundary_check PROPERTIES LABELS script)

# Planted violations (an include, a function, a type, a macro, a link, in engine/, tests/) must be reported, each by line.
luajit_boundary_test(luajit_boundary_check_planted ${LUAJIT_BOUNDARY_FIXTURES}/bad ${LUAJIT_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(luajit_boundary_check_planted PROPERTIES WILL_FAIL TRUE)
luajit_boundary_test(luajit_boundary_check_planted_count ${LUAJIT_BOUNDARY_FIXTURES}/bad ${LUAJIT_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(luajit_boundary_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/CMakeLists.txt:2:.*engine/CMakeLists.txt:6:.*engine/leak.cpp:1:.*engine/leak.cpp:2:.*engine/leak.cpp:3:.*engine/other/macro.h:1:.*tests/leak_test.cpp:1:.*7 problem")

# A clean tree passes: the LuaJIT backend directory may name LuaJIT; comments, strings and a similarly named public header are not violations.
luajit_boundary_test(luajit_boundary_check_clean ${LUAJIT_BOUNDARY_FIXTURES}/good ${LUAJIT_BOUNDARY_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
luajit_boundary_test(luajit_boundary_check_stale ${LUAJIT_BOUNDARY_FIXTURES}/good ${LUAJIT_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(luajit_boundary_check_stale PROPERTIES WILL_FAIL TRUE)
luajit_boundary_test(luajit_boundary_check_stale_message ${LUAJIT_BOUNDARY_FIXTURES}/good ${LUAJIT_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(luajit_boundary_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/app/")
luajit_boundary_test(luajit_boundary_check_no_reason ${LUAJIT_BOUNDARY_FIXTURES}/good ${LUAJIT_BOUNDARY_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(luajit_boundary_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(luajit_boundary_check luajit_boundary_check_planted luajit_boundary_check_planted_count luajit_boundary_check_clean
                   luajit_boundary_check_stale luajit_boundary_check_stale_message luajit_boundary_check_no_reason)
