# Signal-safety gate: see check.cmake. Registered as ctest cases (label: debug).
set(SIGNAL_SAFETY_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(SIGNAL_SAFETY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(signal_safety_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -P ${SIGNAL_SAFETY_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS debug)
endfunction()

# The real tree must be clean.
signal_safety_test(signal_safety_check ${CMAKE_SOURCE_DIR})

# Planted violations of every kind must each be reported; a clean region with allowed calls and an unmarked file passes.
signal_safety_test(signal_safety_check_planted ${SIGNAL_SAFETY_FIXTURES}/bad)
set_tests_properties(signal_safety_check_planted PROPERTIES WILL_FAIL TRUE)
signal_safety_test(signal_safety_check_planted_count ${SIGNAL_SAFETY_FIXTURES}/bad)
set_tests_properties(signal_safety_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "'malloc' in signal-safe.*'snprintf' in signal-safe.*'new' in signal-safe.*'Log' in signal-safe.*'std::string' in signal-safe.*'mutex' in signal-safe.*never closed.*8 violation")
signal_safety_test(signal_safety_check_clean ${SIGNAL_SAFETY_FIXTURES}/good)

engine_label_tests(signal_safety_check signal_safety_check_planted signal_safety_check_planted_count signal_safety_check_clean)
