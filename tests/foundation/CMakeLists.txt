# Deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS
# (-fno-exceptions etc, set in the root CMakeLists.txt): Catch2's
# REQUIRE/CHECK macros are exception-based, and this target's own test
# files use exceptions too (log_test.cpp). The `foundation` library
# under test still builds exception/RTTI-free — this target only
# consumes it.
set(FOUNDATION_TEST_SOURCES
  arena_allocator_test.cpp
  pool_allocator_test.cpp
  stack_allocator_test.cpp
  array_test.cpp
  hash_map_test.cpp
  utf8_test.cpp
  grapheme_test.cpp
  normalize_test.cpp
  string_test.cpp
  vector_test.cpp
  matrix_test.cpp
  quaternion_test.cpp
  transform_test.cpp
  log_test.cpp
  command_line_test.cpp
  os_timing_test.cpp
  concurrency_primitives_test.cpp
  file_io_test.cpp
  gpu_driver_loading_test.cpp
  wayland_connection_test.cpp
  wayland_window_test.cpp
  keyboard_input_test.cpp
  x11_window_test.cpp
  window_backend_test.cpp
)

find_package(Threads REQUIRED)

add_executable(foundation_tests ${FOUNDATION_TEST_SOURCES})
target_link_libraries(foundation_tests PRIVATE foundation Catch2::Catch2WithMain Threads::Threads)
target_include_directories(foundation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(Catch)
catch_discover_tests(foundation_tests)
