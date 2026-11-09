# UI widget style gate: see check.cmake. Registered as ctest cases (label: ui).
set(UI_STYLE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(UI_STYLE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(ui_style_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -P ${UI_STYLE_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS ui)
endfunction()

ui_style_test(ui_widget_style_check ${CMAKE_SOURCE_DIR})
ui_style_test(ui_widget_style_check_planted ${UI_STYLE_FIXTURES}/bad)
set_tests_properties(ui_widget_style_check_planted PROPERTIES WILL_FAIL TRUE)
ui_style_test(ui_widget_style_check_planted_count ${UI_STYLE_FIXTURES}/bad)
set_tests_properties(ui_widget_style_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "bad.cpp:2: a color literal.*bad.cpp:3: a number of 3 or more.*bad.cpp:4: a number of 3 or more.*3 violation")
ui_style_test(ui_widget_style_check_clean ${UI_STYLE_FIXTURES}/good)

engine_label_tests(ui_widget_style_check ui_widget_style_check_planted ui_widget_style_check_planted_count ui_widget_style_check_clean)
