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
  PASS_REGULAR_EXPRESSION "engine/foundation/debug/leaky.h:4.*engine/foundation/debug/leaky.h:5.*engine/foundation/debug/leaky.h:6.*engine/foundation/debug/leaky.h:7.*engine/foundation/debug/leaky.h:8.*engine/foundation/debug/leaky.h:9.*engine/foundation/debug/leaky.h:10.*engine/foundation/debug/leaky.h:11.*engine/foundation/debug/leaky.h:12.*tests/debug/bad_test.cpp:2.*tests/debug/bad_test.cpp:3.*engine/other/uses_library.cpp:2.*engine/other/uses_library.cpp:3.*engine/other/uses_library.cpp:4.*engine/other/uses_library.cpp:5.*engine/other/inline_check.h:3.*engine/other/inline_check.h:4.*engine/other/inline_check.h:5.*engine/foundation/os/linux/win_in_linux.cpp:1: a Windows SDK header outside.*engine/foundation/os/linux/win_in_linux.cpp:2: a Windows SDK header outside.*engine/foundation/os/windows/direct.cpp:1: <windows.h> outside.*engine/foundation/os/windows/direct.cpp:2: a Windows SDK header before.*engine/foundation/os/windows/direct.cpp:4: a D3D12 or DXGI header outside.*game/windowed/main.cpp:3: a windowed program defines its own entry point.*game/windowed/main.cpp:4: a windowed program defines its own entry point.*25 violation")
platform_neutral_test(platform_neutral_check_clean ${PLATFORM_NEUTRAL_FIXTURES}/good)

engine_label_tests(platform_neutral_check platform_neutral_check_planted platform_neutral_check_planted_count platform_neutral_check_clean)

# Rule 6: a raw compiler or system macro outside engine/foundation/build_defs.h is reported by file and line (a comment is not).
platform_neutral_test(platform_neutral_check_raw_defines ${PLATFORM_NEUTRAL_FIXTURES}/bad_build_defs)
set_tests_properties(platform_neutral_check_raw_defines PROPERTIES
  PASS_REGULAR_EXPRESSION "engine/raw.cpp:1: a raw compiler or system macro \\(_WIN32\\).*engine/raw.cpp:3: a raw compiler or system macro \\(__linux__\\).*engine/raw.cpp:6: a raw compiler or system macro \\(__has_feature\\).*3 violation"
  FAIL_REGULAR_EXPRESSION "engine/raw.cpp:2:")
engine_label_tests(platform_neutral_check_raw_defines)
