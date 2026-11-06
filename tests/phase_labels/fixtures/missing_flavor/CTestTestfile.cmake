add_test(a "true")
set_tests_properties(a PROPERTIES LABELS "phase2")
add_test(b "true")
set_tests_properties(b PROPERTIES LABELS "phase2;asan")
