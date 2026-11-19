# Phase 27 Day 475: the script demo. A crowd of bodies, each with a small Lua behavior, a player, pickups and a planner that searches a
# field with task.parallel_for, in the full frame order (scripts, physics, events). The world is in a small file set that the program and
# the tests (tests/script_demo) both build, so they run the very same game. The script is scripts/demo.lua: minted by the build into the
# blob the demo loads, with an editable copy it watches for hot reload, and embedded too (demo_script.cpp.in) for the tests and
# --built-in. The program draws the world in a window with an overlay (demo_view.cpp), offscreen with --screenshot, or runs headless.
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.lua DEMO_LUA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.lua)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/demo_script.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/demo_script.cpp @ONLY)

# The script, minted (Day 475): the blob the demo loads (`minted/`, mounted at "/") and the editable copy it watches (`dev/`). A
# compile-only tree runs nothing it builds, so it mints nothing and the demo there would use the built-in copy.
set(SCRIPT_DEMO_MINTED_DIR ${CMAKE_CURRENT_BINARY_DIR}/minted)
set(SCRIPT_DEMO_DEV_DIR ${CMAKE_CURRENT_BINARY_DIR}/dev)
if(ENGINE_COMPILE_ONLY)
  add_custom_target(script_demo_minted)
else()
  add_custom_command(OUTPUT ${SCRIPT_DEMO_MINTED_DIR}/demo.blob ${SCRIPT_DEMO_DEV_DIR}/5c27de0000000475.lua
                     COMMAND ${CMAKE_COMMAND} -E make_directory ${SCRIPT_DEMO_MINTED_DIR} ${SCRIPT_DEMO_DEV_DIR}
                     COMMAND $<TARGET_FILE:asset_mint> mint-script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.script.src
                             ${SCRIPT_DEMO_MINTED_DIR}/demo.blob --dev-out=${SCRIPT_DEMO_DEV_DIR}
                     DEPENDS asset_mint ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.script.src ${CMAKE_CURRENT_SOURCE_DIR}/scripts/demo.lua
                     COMMENT "Minting the script demo's script" VERBATIM)
  add_custom_target(script_demo_minted DEPENDS ${SCRIPT_DEMO_MINTED_DIR}/demo.blob)
endif()

add_executable(script_demo main.cpp demo_world.cpp demo_view.cpp ${CMAKE_CURRENT_BINARY_DIR}/demo_script.cpp
  ../common/settings_main.cpp ../common/textures.cpp)
add_dependencies(script_demo script_demo_minted)
target_compile_definitions(script_demo PRIVATE SCRIPT_DEMO_MINTED_DIR="${SCRIPT_DEMO_MINTED_DIR}" SCRIPT_DEMO_DEV_DIR="${SCRIPT_DEMO_DEV_DIR}"
  SCRIPT_DEMO_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
engine_split_debug_info(script_demo)
target_compile_options(script_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(script_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(script_demo PRIVATE script_media script_physics script physics storage audio asset renderer_passes renderer rhi
  platform ecs jobs foundation rhi_backends)

# Offscreen: 240 frames of the game drawn with its overlay, the picture written. Skips (77) without a GPU, as the other demos do.
add_test(NAME script_demo_screenshot
  COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:script_demo>;--frames;240;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/script_demo_screenshot.ppm"
    -DEXPECT_CODE=0 "-DEXPECT_OUT=wrote .*script_demo_screenshot.ppm \\(2000 bodies, [0-9]+ objects drawn, 240 frames"
    "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
set_tests_properties(script_demo_screenshot PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
  ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/script_demo_screenshot;${DEMO_PROFILE_ENV}")
engine_label_tests(script_demo_screenshot)

# In a real window on each display backend, with validation on: no script error, no validation error (Phase 27 Day 477's demo run).
# They hold the `display` resource like the other window tests and skip without a session. The ordinary run draws 120 frames (at the
# display's rate, 1,000 took about 17 s); `<name>_long` draws the 1,000 and is a soak test (shorter test runs, the user, 2026-11-19).
function(script_demo_windowed name backend frames)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:script_demo>;--backend;${backend};--set;renderer.validation=on;--audio;null;--frames;${frames}"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=ran ${frames} frames in a window"
      "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|(ERROR|WARN): D3D12 debug layer|failed assertion" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
if(WIN32)
  script_demo_windowed(script_demo_window_win32 win32 120)
  script_demo_windowed(script_demo_window_win32_long win32 1000)
  engine_soak_tests(script_demo_window_win32_long)
elseif(APPLE)
  script_demo_windowed(script_demo_window_cocoa cocoa 120)
  script_demo_windowed(script_demo_window_cocoa_long cocoa 1000)
  engine_soak_tests(script_demo_window_cocoa_long)
else()
  script_demo_windowed(script_demo_window_wayland wayland 120)
  script_demo_windowed(script_demo_window_wayland_long wayland 1000)
  script_demo_windowed(script_demo_window_x11 x11 120)
  script_demo_windowed(script_demo_window_x11_long x11 1000)
  # X11 copy: a soak test (docs/guide/building-and-testing.md, "Long tests"; the user, 2026-11-19)
  engine_soak_tests(script_demo_window_wayland_long script_demo_window_x11 script_demo_window_x11_long)
endif()
