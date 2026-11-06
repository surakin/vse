# Platform-neutral gate: see check.cmake. Registered as ctest cases (phase 7).
set(PLATFORM_NEUTRAL_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
set(PLATFORM_NEUTRAL_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
set(PLATFORM_NEUTRAL_BOUNDARY ${CMAKE_CURRENT_SOURCE_DIR}/library_boundary.txt)

function(platform_neutral_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -DBOUNDARY=${PLATFORM_NEUTRAL_BOUNDARY} -P ${PLATFORM_NEUTRAL_SCRIPT})
  set_tests_properties(${name} PROPERTIES LABELS debug)
endfunction()

# The real tree must be clean.
platform_neutral_test(platform_neutral_check ${CMAKE_SOURCE_DIR})

# Planted violations must be reported (each by file and line); a tree that only mentions platform names in comments and
# in the exempt *_linux files must pass.
platform_neutral_test(platform_neutral_check_planted ${PLATFORM_NEUTRAL_FIXTURES}/bad)
set_tests_properties(platform_neutral_check_planted PROPERTIES WILL_FAIL TRUE)
platform_neutral_test(platform_neutral_check_planted_count ${PLATFORM_NEUTRAL_FIXTURES}/bad)
set_tests_properties(platform_neutral_check_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/foundation/debug/leaky.h:4.*engine/foundation/debug/leaky.h:5.*engine/foundation/debug/leaky.h:6.*engine/foundation/debug/leaky.h:7.*engine/foundation/debug/leaky.h:8.*engine/foundation/debug/leaky.h:9.*engine/foundation/debug/leaky.h:10.*engine/foundation/debug/leaky.h:11.*engine/foundation/debug/leaky.h:12.*tests/debug/bad_test.cpp:2.*tests/debug/bad_test.cpp:3.*engine/other/uses_library.cpp:2.*engine/other/uses_library.cpp:3.*engine/other/inline_check.h:3.*engine/other/inline_check.h:4.*engine/other/inline_check.h:5.*16 violation")
platform_neutral_test(platform_neutral_check_clean ${PLATFORM_NEUTRAL_FIXTURES}/good)

engine_label_tests(platform_neutral_check platform_neutral_check_planted platform_neutral_check_planted_count platform_neutral_check_clean)
