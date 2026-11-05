# Raw new/delete gate: see check.cmake. Registered as ctest cases (label: arrays).
set(RAW_NEW_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(RAW_NEW_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(raw_new_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -P ${RAW_NEW_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS arrays)
endfunction()

# The real tree must be clean.
raw_new_test(raw_new_check ${CMAKE_SOURCE_DIR})

# Planted violations (new, new[], nothrow new, delete, delete[]) must be reported; a clean tree with the foundation allocators
# and mentions in comments must pass.
raw_new_test(raw_new_check_planted ${RAW_NEW_FIXTURES}/bad)
set_tests_properties(raw_new_check_planted PROPERTIES WILL_FAIL TRUE)
raw_new_test(raw_new_check_planted_count ${RAW_NEW_FIXTURES}/bad)
set_tests_properties(raw_new_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:3.*engine/leak.cpp:4.*engine/leak.cpp:5.*engine/leak.cpp:6.*engine/leak.cpp:7.*engine/leak.cpp:10.*engine/leak.cpp:13.*engine/leak.cpp:14.*engine/leak.cpp:15.*engine/leak.cpp:18.*engine/leak.cpp:19.*engine/leak.cpp:20.*12 violation")
raw_new_test(raw_new_check_clean ${RAW_NEW_FIXTURES}/good)
