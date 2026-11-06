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
  resource_guard_linux.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_debug_tests target foundation_lib suffix label)
  add_executable(${target} ${DEBUG_TEST_SOURCES})
  engine_enable_death_tests(${target})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # The family tests exercise the checking paths, so they need debug and slow checks on whatever the configuration is.
  target_compile_definitions(${target} PRIVATE ENGINE_DEBUG_CHECKS=1 ENGINE_SLOW_CHECKS=1)
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_debug_tests(debug_tests foundation "" "debug")
engine_add_debug_tests(debug_tests_asan foundation_asan " [asan]" "asan")
engine_add_debug_tests(debug_tests_tsan foundation_tsan " [tsan]" "tsan")

# What the assert macros do in a release configuration: a separate executable with the checks defined off. Not built when
# the checks are forced on for every configuration (ENGINE_FORCE_DEBUG_CHECKS / ENGINE_FORCE_SLOW_CHECKS), since then no
# translation unit can have them off.
if(NOT ENGINE_FORCE_DEBUG_CHECKS AND NOT ENGINE_FORCE_SLOW_CHECKS)
  add_executable(debug_checks_off_tests assert_checks_off_test.cpp)
  target_link_libraries(debug_checks_off_tests PRIVATE foundation Catch2::Catch2WithMain Threads::Threads)
  target_compile_definitions(debug_checks_off_tests PRIVATE ENGINE_DEBUG_CHECKS=0 ENGINE_SLOW_CHECKS=0)
  engine_discover_tests(debug_checks_off_tests TEST_SUFFIX " [checks off]" PROPERTIES LABELS "debug")
endif()
