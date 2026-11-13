# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The docgen library under test is still built
# exception/RTTI-free.
set(DOCGEN_TEST_SOURCES
  scanner_test.cpp
  extractor_test.cpp
  doc_parser_test.cpp
  reference_test.cpp
  links_test.cpp
  samples_test.cpp
  lint_test.cpp
)

# macOS (Phase 21 Day 293): the Command Line Tools' compiler, run by its path as the sample runner does, finds no SDK of its own, and CMake
# keeps the one it passes to itself (CMAKE_OSX_SYSROOT stays empty), so the tests ask xcrun for it. Empty elsewhere.
set(DOCGEN_TEST_SYSROOT "")
if(APPLE)
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE DOCGEN_TEST_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()

find_package(Threads REQUIRED)
include(Catch)

add_executable(docgen_tests ${DOCGEN_TEST_SOURCES})
target_link_libraries(docgen_tests PRIVATE docgen_lib Catch2::Catch2WithMain Threads::Threads)
target_include_directories(docgen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(docgen_tests PRIVATE
  DOCGEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DOCGEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/engine"
  DOCGEN_CXX="${CMAKE_CXX_COMPILER}" DOCGEN_SYSROOT="${DOCGEN_TEST_SYSROOT}")
engine_discover_tests(docgen_tests PROPERTIES LABELS "docgen")

add_executable(docgen_tests_asan ${DOCGEN_TEST_SOURCES})
target_link_libraries(docgen_tests_asan PRIVATE docgen_lib_asan Catch2::Catch2WithMain Threads::Threads)
target_include_directories(docgen_tests_asan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(docgen_tests_asan PRIVATE
  DOCGEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DOCGEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/engine"
  DOCGEN_CXX="${CMAKE_CXX_COMPILER}" DOCGEN_SYSROOT="${DOCGEN_TEST_SYSROOT}")
engine_discover_tests(docgen_tests_asan TEST_SUFFIX " [asan]" PROPERTIES LABELS "docgen;asan")
