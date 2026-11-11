# SQL text gate: see check.cmake. Registered as ctest cases (label: database).
set(DATABASE_SQL_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(DATABASE_SQL_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(database_sql_test name root allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DALLOWLIST=${allowlist} -P ${DATABASE_SQL_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS database)
endfunction()

add_test(NAME database_sql_check COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${DATABASE_SQL_SCRIPT})
set_tests_properties(database_sql_check PROPERTIES LABELS database)

database_sql_test(database_sql_check_planted ${DATABASE_SQL_FIXTURES}/bad ${DATABASE_SQL_FIXTURES}/good/allowlist.txt)
set_tests_properties(database_sql_check_planted PROPERTIES WILL_FAIL TRUE)
database_sql_test(database_sql_check_planted_message ${DATABASE_SQL_FIXTURES}/bad ${DATABASE_SQL_FIXTURES}/good/allowlist.txt)
set_tests_properties(database_sql_check_planted_message PROPERTIES PASS_REGULAR_EXPRESSION "engine/leak.cpp:2:.*engine/leak.cpp:4:.*3 problem")

database_sql_test(database_sql_check_clean ${DATABASE_SQL_FIXTURES}/good ${DATABASE_SQL_FIXTURES}/good/allowlist.txt)
database_sql_test(database_sql_check_stale ${DATABASE_SQL_FIXTURES}/good ${DATABASE_SQL_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(database_sql_check_stale PROPERTIES WILL_FAIL TRUE)
database_sql_test(database_sql_check_stale_message ${DATABASE_SQL_FIXTURES}/good ${DATABASE_SQL_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(database_sql_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/gone/")
database_sql_test(database_sql_check_no_reason ${DATABASE_SQL_FIXTURES}/good ${DATABASE_SQL_FIXTURES}/good/no_reason_allowlist.txt)
set_tests_properties(database_sql_check_no_reason PROPERTIES WILL_FAIL TRUE)

engine_label_tests(database_sql_check database_sql_check_planted database_sql_check_planted_message database_sql_check_clean
                   database_sql_check_stale database_sql_check_stale_message database_sql_check_no_reason)
