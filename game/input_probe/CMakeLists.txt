# The input probe (Phase 15 Day 186): opens a window and prints the input event stream, so a person can check the events, the cursor shapes, a custom cursor
# image and the pointer lock by eye on both backends. Keys: L toggles the pointer lock, C cycles the cursor shapes, I toggles a generated cursor image, Escape quits.
add_executable(input_probe main.cpp)
target_compile_options(input_probe PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(input_probe PRIVATE platform)

# The banner line once overflowed a 256-byte scratch arena and aborted the program at start (found by hand on 2026-11-11): a one-second run on each display
# backend, in a real window, must reach the end. Holds the `display` resource and skips without a session for that backend. macOS has one
# backend, Cocoa (Phase 26 Day 312).
if(APPLE)
  set(INPUT_PROBE_BACKENDS cocoa)
else()
  set(INPUT_PROBE_BACKENDS wayland x11)
endif()
foreach(backend ${INPUT_PROBE_BACKENDS})
  add_test(NAME input_probe_runs_${backend}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:input_probe>;--backend=${backend};--run-seconds=1"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=backend ${backend}: shapes=" "-DSKIP_NO_DISPLAY_OUT=no window could be opened|could not open a window"
      -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(input_probe_runs_${backend} PROPERTIES LABELS "demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77)
  engine_label_tests(input_probe_runs_${backend})
endforeach()
