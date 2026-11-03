# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The docgen library under test is still built
# exception/RTTI-free.
set(DOCGEN_TEST_SOURCES
  scanner_test.cpp
  extractor_test.cpp
  doc_parser_test.cpp
  reference_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

add_executable(docgen_tests ${DOCGEN_TEST_SOURCES})
target_link_libraries(docgen_tests PRIVATE docgen_lib Catch2::Catch2WithMain Threads::Threads)
target_include_directories(docgen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(docgen_tests PRIVATE
  DOCGEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DOCGEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/engine")
catch_discover_tests(docgen_tests PROPERTIES LABELS "docgen")

add_executable(docgen_tests_asan ${DOCGEN_TEST_SOURCES})
target_link_libraries(docgen_tests_asan PRIVATE docgen_lib_asan Catch2::Catch2WithMain Threads::Threads)
target_include_directories(docgen_tests_asan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(docgen_tests_asan PRIVATE
  DOCGEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DOCGEN_ENGINE_DIR="${CMAKE_SOURCE_DIR}/engine")
catch_discover_tests(docgen_tests_asan TEST_SUFFIX " [asan]" PROPERTIES LABELS "docgen;asan")
