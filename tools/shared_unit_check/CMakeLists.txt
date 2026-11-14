# Shared-representation gate: see check.cmake. Registered as ctest cases (label: arrays).
set(SHARED_UNIT_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(SHARED_UNIT_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(shared_unit_test name root units allowlist)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DUNITS=${units} -DALLOWLIST=${allowlist} -P ${SHARED_UNIT_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS arrays)
endfunction()

# The real tree: its units, its allow list.
shared_unit_test(shared_unit_check ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/units.txt ${CMAKE_CURRENT_SOURCE_DIR}/allowlist.txt)

# Planted violations are reported, each on its line, and a header without its aliases too.
shared_unit_test(shared_unit_check_planted ${SHARED_UNIT_FIXTURES}/bad ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/allowlist.txt)
set_tests_properties(shared_unit_check_planted PROPERTIES WILL_FAIL TRUE)
shared_unit_test(shared_unit_check_planted_count ${SHARED_UNIT_FIXTURES}/bad ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/allowlist.txt)
set_tests_properties(shared_unit_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/leak.h:6:.*engine/leak.h:7:.*engine/leak.h:8:.*engine/leak.h:9:.*engine/leak.h:10:.*engine/leak.h:11:.*7 violation")
# A clean tree passes, with a debt in the allow list passes, and a stale entry fails.
shared_unit_test(shared_unit_check_clean ${SHARED_UNIT_FIXTURES}/good ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/allowlist.txt)
shared_unit_test(shared_unit_check_debt ${SHARED_UNIT_FIXTURES}/debt ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/allowlist_used.txt)
shared_unit_test(shared_unit_check_stale ${SHARED_UNIT_FIXTURES}/good ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(shared_unit_check_stale PROPERTIES WILL_FAIL TRUE)
shared_unit_test(shared_unit_check_stale_message ${SHARED_UNIT_FIXTURES}/good ${SHARED_UNIT_FIXTURES}/units.txt ${SHARED_UNIT_FIXTURES}/good/stale_allowlist.txt)
set_tests_properties(shared_unit_check_stale_message PROPERTIES PASS_REGULAR_EXPRESSION "stale entry engine/ok.h")
# A header that does not define the aliases of a unit fails (the `bad` tree's units file names engine/widget.h, which does; a
# units file that names a header without them is planted here).
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/units_missing_alias.txt "Other|OtherPtr||engine/missing_alias.h\n")
shared_unit_test(shared_unit_check_alias_missing ${SHARED_UNIT_FIXTURES}/bad ${CMAKE_CURRENT_BINARY_DIR}/units_missing_alias.txt ${SHARED_UNIT_FIXTURES}/good/allowlist.txt)
set_tests_properties(shared_unit_check_alias_missing PROPERTIES PASS_REGULAR_EXPRESSION "defines no .using OtherPtr")
engine_label_tests(shared_unit_check shared_unit_check_planted shared_unit_check_planted_count shared_unit_check_clean shared_unit_check_debt shared_unit_check_stale
                   shared_unit_check_stale_message shared_unit_check_alias_missing)
