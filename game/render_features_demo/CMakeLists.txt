# The render features showcase: a terrain under a cube map sky, a forest of instanced trees (an instance hierarchy), rocks, a
# character and bodies on the height field. `--headless` needs no device (the physics hash and the hash of the visible
# instance list), `--screenshot` draws offscreen, the default is a window (Phase 28 Day 504).
add_library(render_features_lib STATIC features_world.cpp features_gpu.cpp)
target_compile_options(render_features_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(render_features_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render_features_lib PUBLIC physics asset renderer_passes renderer rhi jobs foundation)

add_executable(render_features_demo main.cpp ../common/settings_main.cpp ../common/textures.cpp)
engine_split_debug_info(render_features_demo)
target_compile_options(render_features_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(render_features_demo PRIVATE ${CMAKE_SOURCE_DIR}/game/common)
target_compile_definitions(render_features_demo PRIVATE
  RENDER_FEATURES_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
target_link_libraries(render_features_demo PRIVATE render_features_lib platform rhi_backends)

# The physics state and the visible-instance list after 300 steps, at 1, 2, 4 and 16 threads: one value of each (the determinism rule).
foreach(threads 1 2 4 16)
  add_test(NAME render_features_demo_hash_${threads} COMMAND render_features_demo --headless --steps 300 --threads ${threads})
  set_tests_properties(render_features_demo_hash_${threads} PROPERTIES LABELS "demo"
    PASS_REGULAR_EXPRESSION "200000 trees, state hash 0xd9708d0f24c0d4a4, visible hash 0xbfdfe69adcee0598, 55896 tree nodes and 14 rock runs visible ok")
  engine_label_tests(render_features_demo_hash_${threads})
endforeach()

# Offscreen: 240 frames of the scene, the last one reduced and compared with the committed reference image (a mean channel
# difference of at most 1.5%; `--write-reference` makes a new one). Skips (77) without a GPU.
add_test(NAME render_features_screenshot
  COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:render_features_demo>;--frames;240;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/render_features_screenshot.ppm;--reference;${CMAKE_CURRENT_SOURCE_DIR}/reference/forest.ppm;--tolerance;15"
    -DEXPECT_CODE=0 "-DEXPECT_OUT=the frame matches the reference.*wrote .*render_features_screenshot.ppm \\(200000 trees, [1-9][0-9]* draws"
    "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
set_tests_properties(render_features_screenshot PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77 TIMEOUT 300
  ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/render_features_screenshot;${DEMO_PROFILE_ENV}")
engine_label_tests(render_features_screenshot)

# In a real window on each display backend, with validation on: 1000 frames of a smaller forest (20,000 trees); with
# `reset_every` the device is reset that often (a reset-heavy run: every object that owns device objects is rebuilt).
function(render_features_windowed name backend reset_every)
  set(extra "")
  if(NOT reset_every EQUAL 0)
    set(extra ";--reset-every;${reset_every}")
  endif()
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:render_features_demo>;--backend;${backend};--set;renderer.validation=on;--frames;1000;--trees;20000${extra}"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=showed the scene for 1000 frames"
      "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|(ERROR|WARN): D3D12 debug layer|failed assertion" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77 TIMEOUT 600
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
if(WIN32)
  render_features_windowed(render_features_window_win32 win32 0)
  render_features_windowed(render_features_window_win32_reset win32 100)
elseif(APPLE)
  render_features_windowed(render_features_window_cocoa cocoa 0)
  render_features_windowed(render_features_window_cocoa_reset cocoa 100)
else()
  render_features_windowed(render_features_window_wayland wayland 0)
  render_features_windowed(render_features_window_x11 x11 0)
  render_features_windowed(render_features_window_wayland_reset wayland 100)
  # X11 copy: a soak test (docs/guide/building-and-testing.md, "Long tests")
  engine_soak_tests(render_features_window_x11)
endif()

# A sky name that is not one, and a reset without a window: a message and exit 2, before any device.
add_test(NAME render_features_bad_sky COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:render_features_demo>;--sky;marble"
  -DEXPECT_CODE=2 "-DEXPECT_OUT=--sky is procedural or cube" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
add_test(NAME render_features_reset_needs_window COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:render_features_demo>;--headless;--reset-every;10"
  -DEXPECT_CODE=2 "-DEXPECT_OUT=--reset-every needs a window" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
engine_label_tests(render_features_bad_sky render_features_reset_needs_window)
