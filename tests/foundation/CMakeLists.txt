# Deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS
# (-fno-exceptions etc, set in the root CMakeLists.txt): Catch2's
# REQUIRE/CHECK macros are exception-based, and this target's own test
# files use exceptions too (log_test.cpp). The foundation libraries
# under test still build exception/RTTI-free — these targets only
# consume them.
# The tests of layers 0-5 link foundation_base alone, so a base file that reaches up to io, debug or the platform fails to link here.
set(FOUNDATION_BASE_TEST_SOURCES
  arena_allocator_test.cpp
  pool_allocator_test.cpp
  stack_allocator_test.cpp
  container_vector_test.cpp
  array_test.cpp
  unique_ptr_test.cpp
  shared_ptr_test.cpp
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
  diagnostics_hooks_test.cpp
  command_line_test.cpp
  os_timing_test.cpp
  concurrency_primitives_test.cpp
  gpu_driver_loading_test.cpp
  session_state_test.cpp
)

find_package(Threads REQUIRED)

# The tests that need the file layer or the platform (windows, input, the display servers) link both.
set(FOUNDATION_PLATFORM_TEST_SOURCES
  file_io_test.cpp
  wayland_connection_test.cpp
  wayland_window_test.cpp
  keyboard_input_test.cpp
  x11_window_test.cpp
  window_backend_test.cpp
  window_fullscreen_test.cpp
)

add_executable(foundation_base_tests ${FOUNDATION_BASE_TEST_SOURCES})
target_link_libraries(foundation_base_tests PRIVATE foundation_base Catch2::Catch2WithMain Threads::Threads)
target_include_directories(foundation_base_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)

# The file-layer and window tests need foundation_io and the platform library, which Windows builds from Days 151 and 158.
if(TARGET platform)
  add_executable(foundation_tests ${FOUNDATION_PLATFORM_TEST_SOURCES})
  target_link_libraries(foundation_tests PRIVATE platform foundation Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(foundation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)
endif()

include(Catch)
# Tests that open a window share one desktop and its input focus. A window another test opens or closes moves the real
# focus, and a real FocusOut legitimately releases held keys, so run concurrently they fail at random (seen: the 17 X11
# tests under `ctest -j6` failed in 3 runs out of 4, and one in most full runs; each passes alone). The window tests hold
# the ctest resource `display` (shared with tests/rhi), so ctest runs them one at a time while the rest stays parallel.
engine_discover_tests(foundation_base_tests)
if(TARGET foundation_tests)
  engine_discover_tests(foundation_tests TEST_SPEC "~[window]~[x11]~[wayland]")
  engine_discover_tests(foundation_tests TEST_SPEC "[window],[x11],[wayland]" PROPERTIES LABELS display RESOURCE_LOCK display)
endif()
