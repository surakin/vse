# Phase 6 tests: the debugging facilities. Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are exception-based. The libraries under test are still
# built exception/RTTI-free. Built plain and under ASan/UBSan and TSan, each linked against the matching foundation.
#
# Files named *_linux_* hold the Linux-only details (signals, fork, /proc); everything else speaks the neutral API of
# engine/foundation/debug/ so the Windows and macOS ports (Phases 8 and 9) build the same sources.
set(DEBUG_TEST_SOURCES
  death_test_registry_test.cpp
  death_test_run_test.cpp
  death_test_linux_test.cpp
  assert_family_test.cpp
  assert_linux_test.cpp
  stack_trace_test.cpp
  resource_guard_linux.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_debug_tests target foundation_lib jobs_lib suffix label)
  add_executable(${target} ${DEBUG_TEST_SOURCES})
  engine_enable_death_tests(${target})
  target_link_libraries(${target} PRIVATE ${foundation_lib} ${jobs_lib} Catch2::Catch2WithMain Threads::Threads)
  add_dependencies(${target} debug_test_plugin)
  target_compile_definitions(${target} PRIVATE DEBUG_TEST_PLUGIN_PATH="$<TARGET_FILE:debug_test_plugin>")
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # The family tests exercise the checking paths, so they need debug and slow checks on whatever the configuration is.
  target_compile_definitions(${target} PRIVATE ENGINE_DEBUG_CHECKS=1 ENGINE_SLOW_CHECKS=1)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
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
  engine_discover_tests(debug_checks_off_tests TEST_SUFFIX " [checks off]" PROPERTIES LABELS "debug")
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
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
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
