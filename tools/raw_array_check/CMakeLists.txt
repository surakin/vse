# Raw-array gate: see check.cmake. Registered as ctest cases (label: arrays).
set(RAW_ARRAY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(RAW_ARRAY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(raw_array_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -P ${RAW_ARRAY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS arrays)
endfunction()

# The real tree must be clean.
raw_array_test(raw_array_check ${CMAKE_SOURCE_DIR})

# Planted violations (new[], nothrow new[], delete[]) must be reported; a clean tree with the foundation allocators
# and mentions in comments must pass.
raw_array_test(raw_array_check_planted ${RAW_ARRAY_FIXTURES}/bad)
set_tests_properties(raw_array_check_planted PROPERTIES WILL_FAIL TRUE)
raw_array_test(raw_array_check_planted_count ${RAW_ARRAY_FIXTURES}/bad)
set_tests_properties(raw_array_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.cpp:3.*engine/leak.cpp:4.*engine/leak.cpp:5.*engine/leak.cpp:6.*engine/leak.cpp:7.*engine/leak.cpp:10.*6 violation")
raw_array_test(raw_array_check_clean ${RAW_ARRAY_FIXTURES}/good)
