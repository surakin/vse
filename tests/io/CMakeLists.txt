# Like /tests/foundation, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The foundation library under test is still built exception/RTTI-free. One executable per sanitizer
# flavor, as in tests/jobs.
set(IO_TEST_SOURCES
  path_test.cpp
  file_system_test.cpp
  file_test.cpp
  directory_test.cpp
  async_file_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_io_tests target foundation_lib jobs_lib suffix label)
  add_executable(${target} ${IO_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${jobs_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_io_tests(io_tests foundation jobs "" "io")
engine_add_io_tests(io_tests_asan foundation_asan jobs_asan " [asan]" "asan")
engine_add_io_tests(io_tests_tsan foundation_tsan jobs_tsan " [tsan]" "tsan")
