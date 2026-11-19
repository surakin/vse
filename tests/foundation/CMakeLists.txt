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
  build_defs_test.cpp
  unique_ptr_test.cpp
  shared_ptr_test.cpp
  hash_map_test.cpp
  hash_set_test.cpp
  hash_test.cpp
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
  bytes_test.cpp  # Phase 19 Day 256
  concurrency_primitives_test.cpp
  session_state_test.cpp
)
# The GPU driver probe is Vulkan's and not built on macOS (Phase 21 decision 8), so neither is its test; os::LoadLibrary is tested there
# by tests/macos/os_macos_test.cpp.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND FOUNDATION_BASE_TEST_SOURCES gpu_driver_loading_test.cpp)
endif()

find_package(Threads REQUIRED)

# The tests that need the file layer or the platform (windows, input, the display servers) link both. The Wayland, X11 and keyboard
# tests are Linux's; the backend plan and the fullscreen toggle run on Windows too (Phase 13 Day 158; the Win32 cases are in tests/windows).
set(FOUNDATION_PLATFORM_TEST_SOURCES
  file_io_test.cpp
  window_backend_test.cpp
  window_fullscreen_test.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND FOUNDATION_PLATFORM_TEST_SOURCES
    wayland_connection_test.cpp
    wayland_window_test.cpp
    keyboard_input_test.cpp
    x11_window_test.cpp
    x11_scale_test.cpp
    window_events_test.cpp
    window_cursor_test.cpp
    window_clipboard_test.cpp
    window_dragdrop_test.cpp
  )
endif()

add_executable(foundation_base_tests ${FOUNDATION_BASE_TEST_SOURCES})
target_link_libraries(foundation_base_tests PRIVATE foundation_base Catch2::Catch2WithMain Threads::Threads)
target_include_directories(foundation_base_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)

# The file-layer and window tests need foundation_io and the platform library, which Windows builds from Days 151 and 158.
# macOS (Phase 21 Day 293) builds the file tests on the foundation aggregate; the window tests join with the platform layer (Day 295).
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT TARGET platform)
  add_executable(foundation_tests file_io_test.cpp)
  target_link_libraries(foundation_tests PRIVATE foundation Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(foundation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support)
elseif(TARGET platform)
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

# The window-layer checklist gate (engine/platform/BACKEND.md lists every platform directory); a CMake script, so it runs on every platform.
add_test(NAME window_backend_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_window_backend_lists.cmake)
add_test(NAME window_backend_lists_planted COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
         -DBACKEND_FILE=${CMAKE_CURRENT_SOURCE_DIR}/planted/planted_window_backend.md -P ${CMAKE_CURRENT_SOURCE_DIR}/check_window_backend_lists.cmake)
set_tests_properties(window_backend_lists_planted PROPERTIES WILL_FAIL TRUE)
engine_label_tests(window_backend_lists window_backend_lists_planted)
