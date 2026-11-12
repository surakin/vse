# Like /tests/foundation, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The foundation library under test is still built exception/RTTI-free. One executable per sanitizer
# flavor, as in tests/io, plus one that forces the portable 128-bit implementation.
set(TYPES_TEST_SOURCES
  int128_test.cpp
  span_test.cpp
  algorithm_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_types_tests target foundation_lib suffix label)
  add_executable(${target} ${TYPES_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_types_tests(types_tests foundation_base "" "types")
if(ENGINE_HAVE_ASAN)
  engine_add_types_tests(types_tests_asan foundation_base_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_types_tests(types_tests_tsan foundation_base_tsan " [tsan]" "tsan")
endif()

# The same cases with the portable 128-bit implementation forced on (ENGINE_FORCE_PORTABLE_INT128): int128.cpp is compiled into this
# executable with the macro, so no translation unit sees the other definition of the inline operators. Only where the compiler has a
# native __int128 (GCC, Clang, clang-cl): with MSVC the library itself is the portable path, so types_tests already covers it and this
# executable would define every portable symbol twice (found on Phase 13 Day 149).
if(NOT ENGINE_MSVC_LIKE OR ENGINE_CLANG_CL)
  add_executable(types_tests_portable ${TYPES_TEST_SOURCES} ${CMAKE_SOURCE_DIR}/engine/foundation/int128.cpp)
  target_compile_definitions(types_tests_portable PRIVATE ENGINE_FORCE_PORTABLE_INT128=1)
  target_link_libraries(types_tests_portable PRIVATE foundation_base Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(types_tests_portable PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  engine_discover_tests(types_tests_portable TEST_SUFFIX " [portable]" PROPERTIES LABELS "types")
endif()

# The type vocabulary headers compile for the Windows and macOS targets (needs clang++; skips without it). Windows has no __int128, so this is the
# portable 128-bit path compiled for real.
function(engine_types_cross_syntax_test name triple)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DTRIPLE=${triple} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/check_types_cross_syntax.cmake)
endfunction()
engine_types_cross_syntax_test(types_cross_syntax_windows x86_64-pc-windows-msvc)
# Kept now that macOS builds for real (Phase 26 Day 302): the Intel development Mac builds x86-64 only, so this is what compiles these
# headers for arm64 (Apple silicon) until Phase 26 Day 327's arm64 build.
engine_types_cross_syntax_test(types_cross_syntax_macos arm64-apple-macos13)
engine_label_tests(types_cross_syntax_windows types_cross_syntax_macos)
