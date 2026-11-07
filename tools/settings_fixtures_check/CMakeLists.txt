# Settings schema fixture check: see check.cmake. Registered as ctest cases (label: settings).
set(SETTINGS_FIXTURES_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(SETTINGS_FIXTURES_TREES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

add_test(NAME settings_fixtures COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${SETTINGS_FIXTURES_SCRIPT})
# A schema at version 3 with fixtures for versions 1 and 2, listed and tested, passes.
add_test(NAME settings_fixtures_clean COMMAND ${CMAKE_COMMAND} -DROOT=${SETTINGS_FIXTURES_TREES}/good -P ${SETTINGS_FIXTURES_SCRIPT})
# A schema at version 3 without the fixture of version 2, unlisted, untested, fails and says what is missing.
add_test(NAME settings_fixtures_planted COMMAND ${CMAKE_COMMAND} -DROOT=${SETTINGS_FIXTURES_TREES}/bad -P ${SETTINGS_FIXTURES_SCRIPT})
set_tests_properties(settings_fixtures_planted PROPERTIES WILL_FAIL TRUE)
add_test(NAME settings_fixtures_planted_message COMMAND ${CMAKE_COMMAND} -DROOT=${SETTINGS_FIXTURES_TREES}/bad -P ${SETTINGS_FIXTURES_SCRIPT})
set_tests_properties(settings_fixtures_planted_message PROPERTIES
  PASS_REGULAR_EXPRESSION "the fixture planted_v1.cfg .* is missing.*planted_v1.cfg is not listed.*does not load the fixture planted_v1.cfg.*3 problem")
engine_label_tests(settings_fixtures settings_fixtures_clean settings_fixtures_planted settings_fixtures_planted_message)
