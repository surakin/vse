# Phase 6 tests: the debugging facilities. Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are exception-based. The libraries under test are still
# built exception/RTTI-free. Built plain and under ASan/UBSan and TSan, each linked against the matching foundation.
#
# Files named *_linux_* hold the Linux-only details (signals, fork, /proc); everything else speaks the neutral API of
# engine/foundation/debug/ so the Windows and macOS ports (Phases 10 and 11) build the same sources.
set(DEBUG_TEST_SOURCES
  death_test_registry_test.cpp
  death_test_run_test.cpp
  death_test_linux_test.cpp
  assert_family_test.cpp
  assert_linux_test.cpp
  stack_trace_test.cpp
  crash_extract_test.cpp
  crash_handler_test.cpp
  crash_handler_linux_test.cpp
  crash_report_test.cpp
  crash_report_linux_test.cpp
  crash_end_to_end_linux_test.cpp
  core_dump_test.cpp
  core_dump_linux_test.cpp
  crash_report_script_linux_test.cpp
  program_crash_test_linux_test.cpp
  job_crash_linux_test.cpp
  crash_state_test.cpp
  resource_guard_linux.cpp
)

find_package(Threads REQUIRED)
include(Catch)

# Every Catch2 test of this directory carries the label `crash`: the suite crashes processes on purpose (death tests, the demos'
# --crash-test), which makes systemd-coredump and desktop crash reporters notify the user. scripts/test-phase.sh leaves the label
# out unless it runs --all or is given --crashes; plain ctest runs it.
function(engine_add_debug_tests target foundation_lib jobs_lib suffix label)
  add_executable(${target} ${DEBUG_TEST_SOURCES})
  engine_enable_death_tests(${target})
  target_link_libraries(${target} PRIVATE ${foundation_lib} ${jobs_lib} Catch2::Catch2WithMain Threads::Threads)
  add_dependencies(${target} debug_test_plugin crash_extract)
  # The envelope parser of the crash_extract tool, compiled into each flavor so the sweeps run under the sanitizers.
  target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tools/crash_extract/envelope.cpp)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tools/crash_extract)
  target_compile_definitions(${target} PRIVATE DEBUG_TEST_PLUGIN_PATH="$<TARGET_FILE:debug_test_plugin>" ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ENGINE_CRASH_EXTRACT_PATH="$<TARGET_FILE:crash_extract>")
  # The tests that run the shipped programs are in the plain flavor only: the programs are not sanitized, so the other flavors
  # would run the same binaries again for no extra coverage (and would need every program built to build a sanitizer test).
  if(suffix STREQUAL "")
    target_compile_definitions(${target} PRIVATE
      ENGINE_PROGRAM_RHI_DEMO="$<TARGET_FILE:rhi_demo>" ENGINE_PROGRAM_RHI_BENCH="$<TARGET_FILE:rhi_bench>"
      ENGINE_PROGRAM_RENDERER_DEMO="$<TARGET_FILE:renderer_demo>" ENGINE_PROGRAM_RENDERER_BENCH="$<TARGET_FILE:renderer_bench>")
    add_dependencies(${target} rhi_demo rhi_bench renderer_demo renderer_bench)
  endif()
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # The family tests exercise the checking paths, so they need debug and slow checks on whatever the configuration is.
  target_compile_definitions(${target} PRIVATE ENGINE_DEBUG_CHECKS=1 ENGINE_SLOW_CHECKS=1)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label};crash")
endfunction()

# A shared library the stack-trace tests load at run time. Plain build, no sanitizer runtime of its own.
add_library(debug_test_plugin MODULE plugin.cpp)
target_include_directories(debug_test_plugin PRIVATE ${CMAKE_SOURCE_DIR}/engine/foundation)
set_target_properties(debug_test_plugin PROPERTIES UNITY_BUILD OFF)

engine_add_debug_tests(debug_tests foundation jobs "" "debug")
engine_add_debug_tests(debug_tests_asan foundation_asan jobs_asan " [asan]" "asan")
engine_add_debug_tests(debug_tests_tsan foundation_tsan jobs_tsan " [tsan]" "tsan")

# What the assert macros do in a release configuration: a separate executable with the checks defined off. Not built when
# the checks are forced on for every configuration (ENGINE_FORCE_DEBUG_CHECKS / ENGINE_FORCE_SLOW_CHECKS), since then no
# translation unit can have them off.
if(NOT ENGINE_FORCE_DEBUG_CHECKS AND NOT ENGINE_FORCE_SLOW_CHECKS)
  add_executable(debug_checks_off_tests assert_checks_off_test.cpp)
  target_link_libraries(debug_checks_off_tests PRIVATE foundation Catch2::Catch2WithMain Threads::Threads)
  target_compile_definitions(debug_checks_off_tests PRIVATE ENGINE_DEBUG_CHECKS=0 ENGINE_SLOW_CHECKS=0)
  engine_discover_tests(debug_checks_off_tests TEST_SUFFIX " [checks off]" PROPERTIES LABELS "debug;crash")
endif()

# The trace-in-a-signal-handler test counts allocations, so these executables wrap malloc, calloc and realloc (GNU ld
# --wrap); the sanitizer flavors check the same path for races and memory errors (their runtimes intercept the real malloc
# the wrappers call).
function(engine_add_alloc_tests target foundation_lib suffix label)
  add_executable(${target} stack_trace_alloc_linux_test.cpp)
  engine_enable_death_tests(${target})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options(${target} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label};crash")
endfunction()

engine_add_alloc_tests(debug_alloc_tests foundation " [alloc count]" "debug")
engine_add_alloc_tests(debug_alloc_tests_asan foundation_asan " [alloc count] [asan]" "asan")
engine_add_alloc_tests(debug_alloc_tests_tsan foundation_tsan " [alloc count] [tsan]" "tsan")

# A stripped copy of the test executable, run by its own ctest entry with ENGINE_EXPECT_STRIPPED_BINARY=1: it checks that
# frames of a binary with no symbols fall back to module and offset. (The test case skips in the normal executable.)
find_program(ENGINE_STRIP_TOOL strip)
if(ENGINE_STRIP_TOOL)
  add_custom_command(TARGET debug_tests POST_BUILD
    COMMAND ${ENGINE_STRIP_TOOL} -o $<TARGET_FILE:debug_tests>.stripped $<TARGET_FILE:debug_tests>
    COMMENT "Stripping a copy of debug_tests for the stripped-binary trace test")
  add_test(NAME debug_stack_trace_stripped
    COMMAND $<TARGET_FILE:debug_tests>.stripped "In a stripped binary the frames fall back to module and offset")
  set_tests_properties(debug_stack_trace_stripped PROPERTIES
    ENVIRONMENT "ENGINE_EXPECT_STRIPPED_BINARY=1"
    LABELS "debug")
  engine_label_tests(debug_stack_trace_stripped)
endif()

# Nothing can upload (PLAN.md assumption #20): no built test binary may list a network or TLS library or import their
# functions. The check itself is tested on two canned readelf outputs.
function(engine_no_network_test name binary_target)
  add_test(NAME debug_no_network_${name}
    COMMAND ${CMAKE_COMMAND} -DBINARY=$<TARGET_FILE:${binary_target}> -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_network.cmake)
  set_tests_properties(debug_no_network_${name} PROPERTIES LABELS "debug" SKIP_REGULAR_EXPRESSION "readelf not found")
  engine_label_tests(debug_no_network_${name})
endfunction()
engine_no_network_test(debug_tests debug_tests)
engine_no_network_test(debug_tests_asan debug_tests_asan)
engine_no_network_test(debug_tests_tsan debug_tests_tsan)
engine_no_network_test(debug_alloc_tests debug_alloc_tests)
engine_no_network_test(crash_extract crash_extract)
# The programs that ship install the crash handler too, so the guarantee covers them (the targets are defined in game/, later).
engine_no_network_test(rhi_demo rhi_demo)
engine_no_network_test(rhi_bench rhi_bench)
engine_no_network_test(renderer_demo renderer_demo)
engine_no_network_test(renderer_bench renderer_bench)

add_test(NAME debug_no_network_check_clean
  COMMAND ${CMAKE_COMMAND} -DREADELF_TEXT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/readelf_clean.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_network.cmake)
add_test(NAME debug_no_network_check_planted
  COMMAND ${CMAKE_COMMAND} -DREADELF_TEXT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/readelf_with_curl.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_network.cmake)
set_tests_properties(debug_no_network_check_clean PROPERTIES LABELS "debug")
set_tests_properties(debug_no_network_check_planted PROPERTIES LABELS "debug"
  PASS_REGULAR_EXPRESSION "links a network library: Shared library: \\[libcurl.so.4\\]")
engine_label_tests(debug_no_network_check_clean debug_no_network_check_planted)

# The symbol half of the check: an imported network function fails, a defined one (a sanitizer runtime's interceptor) does not.
add_test(NAME debug_no_network_check_imports_planted
  COMMAND ${CMAKE_COMMAND} -DREADELF_TEXT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/readelf_imports_only.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_network.cmake)
add_test(NAME debug_no_network_check_defines_clean
  COMMAND ${CMAKE_COMMAND} -DREADELF_TEXT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/readelf_defines_only.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_network.cmake)
set_tests_properties(debug_no_network_check_imports_planted PROPERTIES LABELS "debug"
  PASS_REGULAR_EXPRESSION "imports a network function: getaddrinfo")
set_tests_properties(debug_no_network_check_defines_clean PROPERTIES LABELS "debug")
engine_label_tests(debug_no_network_check_imports_planted debug_no_network_check_defines_clean)

# The neutral functions a platform backend implements carry a `Backend:` doc label; the Windows and macOS checklists must list
# every one of them (and name no function without the label). Tested on a good and a bad fixture tree.
function(engine_backend_lists_test name root)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DROOT=${root} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_backend_lists.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "debug")
endfunction()
engine_backend_lists_test(debug_backend_lists ${CMAKE_SOURCE_DIR})
engine_backend_lists_test(debug_backend_lists_clean ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/backend_lists/good)
engine_backend_lists_test(debug_backend_lists_planted ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/backend_lists/bad)
set_tests_properties(debug_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_backend_lists_test(debug_backend_lists_planted_count ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/backend_lists/bad)
set_tests_properties(debug_backend_lists_planted_count PROPERTIES
  PASS_REGULAR_EXPRESSION "windows: BACKEND.md lists `Stale`.*windows: BACKEND.md has no section heading for `Forgotten`.*macos: BACKEND.md lists `Stale`.*macos: BACKEND.md has no section heading for `Forgotten`.*4 problem")
engine_label_tests(debug_backend_lists debug_backend_lists_clean debug_backend_lists_planted debug_backend_lists_planted_count)

# The neutral headers and the platform-free facade sources must compile for the Windows and macOS targets (clang -fsyntax-only with
# shim standard headers). Skipped where clang++ is missing. Not covered: stack_trace_backtrace.cpp, crash_handler_sentry.cpp and
# module_list.cpp (they include libbacktrace's and sentry's headers, which need the target's operating system headers; see
# KNOWN-ISSUES.md).
function(engine_cross_syntax_test name triple)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DTRIPLE=${triple} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/check_cross_syntax.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "debug" SKIP_REGULAR_EXPRESSION "clang\\+\\+ not found")
endfunction()
engine_cross_syntax_test(debug_cross_syntax_windows x86_64-pc-windows-msvc)
engine_cross_syntax_test(debug_cross_syntax_macos arm64-apple-macos13)
engine_label_tests(debug_cross_syntax_windows debug_cross_syntax_macos)
