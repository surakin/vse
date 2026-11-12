# The Phase 15 showcase: the engine's own widgets around a viewport that shows the Phase 5 lit scene, in a window or offscreen (--screenshot).
# The widget tree, the script and the GPU glue are a small static library so the tests build the very same demo.
add_library(ui_demo_lib STATIC ui_demo_app.cpp ui_script.cpp ui_demo_gpu.cpp)
target_compile_options(ui_demo_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(ui_demo_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_demo_lib PUBLIC renderer_demo_lib ui renderer renderer_passes rhi foundation)

add_executable(ui_demo main.cpp ../common/settings_main.cpp)
engine_split_debug_info(ui_demo)
target_compile_options(ui_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(ui_demo PRIVATE RENDERER_DEMO_ASSET_DIR="${CMAKE_SOURCE_DIR}/game/renderer_demo/assets"
  UI_DEMO_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
target_link_libraries(ui_demo PRIVATE ui_demo_lib ui_window rhi_backends)

# Failure behaviour and the scripted paths, as for renderer_demo (its expect_run.cmake is reused). Tests that need a GPU skip (77) without one.
function(ui_demo_expect name code out skip_re)
  string(REPLACE "UI_DEMO_SHOT" "${ENGINE_TEST_TMP_DIR}/screenshots/${name}.ppm" args "${ARGN}")
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:ui_demo>;${args}" -DEXPECT_CODE=${code} "-DEXPECT_OUT=${out}"
      "-DSKIP_IF_OUT=${skip_re}" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
set(UI_DEMO_NO_GPU "cannot open loader library|no physical devices|no suitable GPU")
if(APPLE)
  string(APPEND UI_DEMO_NO_GPU "|backend is not built for macOS")  # Vulkan by name on macOS (Metal: the *_metal test)
endif()
ui_demo_expect(ui_demo_screenshot 0 "wrote .*ui_demo_screenshot.ppm \\(960x600\\)" "${UI_DEMO_NO_GPU}" --screenshot UI_DEMO_SHOT --validation --rhi vulkan)
if(APPLE)
  # The same picture on Metal by name (Phase 26 Day 324), with Metal's validation on (DEMO_PROFILE_ENV).
  # Named outside `ui_demo_*` (phase 15 in tests/phases.txt) so it carries phase 26 with the other Metal ctests.
  ui_demo_expect(metal_ui_demo_screenshot 0 "wrote .*metal_ui_demo_screenshot.ppm \\(960x600\\)" "" --screenshot UI_DEMO_SHOT --validation --rhi metal)
endif()
# Every kind of script word, offscreen, with the validation layer on and silent: input, menus, themes, drops, the clipboard, a resize and two pictures.
ui_demo_expect(ui_demo_screenshot_script 0 "wrote .*ui_demo_screenshot_script_a.ppm .*wrote .*ui_demo_screenshot_script.ppm \\(640x480\\)" "${UI_DEMO_NO_GPU}"
  --screenshot UI_DEMO_SHOT --validation --scale 2 --frames 40 --script "frame:2 click:btn_new frame:4 type:abc key:Tab key:ctrl+a frame:6 theme:dark frame:8 openmenu:View frame:10 key:Escape drop:text:dropped frame:12 paste:pasted frame:13 rendering frame:14 key:Escape drag:list_items:0,60 wheel:viewport:3 frame:18 shot:${ENGINE_TEST_TMP_DIR}/screenshots/ui_demo_screenshot_script_a.ppm resize:320x240 frame:24 menu:Help/About frame:30")
# A bad script word fails cleanly (exit 1), a bad scale is a usage error (exit 2), a missing Vulkan loader is reported.
ui_demo_expect(ui_demo_bad_script 1 "unknown script word|cannot do" "${UI_DEMO_NO_GPU}" --screenshot UI_DEMO_SHOT --script "frame:1 sparkle:now")
ui_demo_expect(ui_demo_bad_scale 2 "usage" "" --scale 3)
if(NOT APPLE)  # macOS has no Vulkan
  add_test(NAME ui_demo_no_vulkan
    COMMAND ${CMAKE_COMMAND} -E env ENGINE_VULKAN_LIBRARY=/nonexistent
      ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:ui_demo>;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/ui_demo_no_vulkan.ppm;--rhi;vulkan" -DEXPECT_CODE=1 "-DEXPECT_OUT=no vulkan device"
      -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(ui_demo_no_vulkan PROPERTIES LABELS demo ENVIRONMENT "${DEMO_PROFILE_ENV}")
  engine_label_tests(ui_demo_no_vulkan)
endif()

# The live paths in a real window on each display backend, with validation on: clicks, a menu, the theme, a drop, a settings change of every cost class that
# applies to a window, a forced device reset and a lost device with its recovery. These open a window, so they hold the `display` resource and skip without a session.
function(ui_demo_scripted name backend rhi)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:ui_demo>;--backend;${backend};--rhi;${rhi};--validation;--frames;150;--script;frame:5 click:btn_new frame:8 menu:View/Theme/Dark frame:12 drop:text:hello frame:15 set:render_scale=0.5 frame:20 set:present_mode=immediate frame:30 reset frame:40 set:shader_quality=low rendering frame:55 theme:light frame:60 loss frame:120 quit"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=swapchain done in.*device-reset done in.*pipeline-rebuild done in.*recovered after 1 attempt"
      "-DSKIP_IF_OUT=${UI_DEMO_NO_GPU}" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|(ERROR|WARN): D3D12 debug layer|failed assertion" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
if(WIN32)
  ui_demo_scripted(ui_demo_script_win32 win32 vulkan)
  ui_demo_scripted(ui_demo_script_win32_dx12 win32 dx12) # Phase 15 Day 205: the same run on Direct3D 12
elseif(APPLE)
  ui_demo_scripted(ui_demo_script_cocoa cocoa metal)  # Phase 26 Day 324 (on null from Day 314)
else()
  ui_demo_scripted(ui_demo_script_wayland wayland vulkan)
  ui_demo_scripted(ui_demo_script_x11 x11 vulkan)
endif()

demo_settings_tests(ui_demo)
