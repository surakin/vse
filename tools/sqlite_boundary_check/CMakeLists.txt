# SQLite boundary gate: see check.cmake. Registered as ctest cases (label: database).
set(SQLITE_BOUNDARY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(SQLITE_BOUNDARY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(sqlite_boundary_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${SQLITE_BOUNDARY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS database)
endfunction()

# The real tree must be clean, with its own allow list.
add_test(NAME sqlite_boundary_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${SQLITE_BOUNDARY_SCRIPT})
set_tests_properties(sqlite_boundary_check PROPERTIES LABELS database)

# Planted violations (an include, a function, a type, a macro, a link, in engine/, tests/) must be reported, each by line.
sqlite_boundary_test(sqlite_boundary_check_planted ${SQLITE_BOUNDARY_FIXTURES}/bad ${SQLITE_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(sqlite_boundary_check_planted PROPERTIES WILL_FAIL TRUE)
sqlite_boundary_test(sqlite_boundary_check_planted_count ${SQLITE_BOUNDARY_FIXTURES}/bad ${SQLITE_BOUNDARY_FIXTURES}/good/allowlist.txt)
set_tests_properties(sqlite_boundary_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/CMakeLists.txt:2:.*engine/CMakeLists.txt:6:.*engine/leak.cpp:1:.*engine/leak.cpp:2:.*engine/leak.cpp:3:.*engine/other/macro.h:1:.*tests/leak_test.cpp:1:.*7 problem")

# A clean tree passes: the SQLite directory may name SQLite; comments, strings and a similarly named public header are not violations.
sqlite_boundary_test(sqlite_boundary_check_clean ${SQLITE_BOUNDARY_FIXTURES}/good ${SQLITE_BOUNDARY_FIXTURES}/good/allowlist.txt)
# A stale allow-list entry fails, and so does an entry without a reason.
sqlite_boundary_test(sqlite_boundary_check_stale ${SQLITE_BOUNDARY_FIXTURES}/good ${SQLITE_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(sqlite_boundary_check_stale PROPERTIES WILL_FAIL TRUE)
sqlite_boundary_test(sqlite_boundary_check_stale_message ${SQLITE_BOUNDARY_FIXTURES}/good ${SQLITE_BOUNDARY_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(sqlite_boundary_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/app/")
sqlite_boundary_test(sqlite_boundary_check_no_reason ${SQLITE_BOUNDARY_FIXTURES}/good ${SQLITE_BOUNDARY_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(sqlite_boundary_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(sqlite_boundary_check sqlite_boundary_check_planted sqlite_boundary_check_planted_count sqlite_boundary_check_clean
                   sqlite_boundary_check_stale sqlite_boundary_check_stale_message sqlite_boundary_check_no_reason)
