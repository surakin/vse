add_test(a "true")
set_tests_properties(a PROPERTIES LABELS "phase0")
add_test(forgot_a_label "true")
set_tests_properties(forgot_a_label PROPERTIES LABELS "gpu")
