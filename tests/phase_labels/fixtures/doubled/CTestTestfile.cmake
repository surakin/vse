add_test(a "true")
set_tests_properties(a PROPERTIES LABELS "phase0")
add_test(in_two_phases "true")
set_tests_properties(in_two_phases PROPERTIES LABELS "phase1;phase4")
