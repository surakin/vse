add_test(a "true")
set_tests_properties(a PROPERTIES LABELS "phase0;gpu")
add_test(b "true")
set_tests_properties(b PROPERTIES LABELS "phase2_5")
# A name with ";" goes through the check (it used to be split into list elements, which only inflated counts).
add_test([==[semi; colon]==] "true")
set_tests_properties([==[semi; colon]==] PROPERTIES LABELS "phase3")
