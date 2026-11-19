# The Phase 5 showcase: a large lit scene drawn by the deferred renderer, in a window or offscreen (--screenshot).
# The scene, the frame and the asset generator are in a small static library so the tests build the very same scene.
add_library(renderer_demo_lib STATIC demo_assets.cpp demo_scene.cpp ../common/textures.cpp)
target_compile_options(renderer_demo_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(renderer_demo_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(renderer_demo_lib PUBLIC scene ecs renderer_passes renderer rhi foundation)

add_executable(renderer_demo main.cpp ../common/settings_main.cpp)
engine_split_debug_info(renderer_demo)
target_compile_options(renderer_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(renderer_demo PRIVATE RENDERER_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  RENDERER_DEMO_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
target_link_libraries(renderer_demo PRIVATE renderer_demo_lib rhi_backends)

# Writes the texture files under assets/ (see README.md); a test compares them with the committed ones.
add_executable(renderer_demo_mint mint_assets.cpp)
target_compile_options(renderer_demo_mint PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(renderer_demo_mint PRIVATE renderer_demo_lib)

# Failure injection and missing-Vulkan behaviour, as for rhi_demo (its expect_run.cmake is reused). Tests that need a GPU skip
# (77) without one.
# Each test gets its own output file and pipeline-cache directory, so they can run in parallel.
function(renderer_demo_expect name code out skip_re)
  string(REPLACE "RENDERER_DEMO_SHOT" "${ENGINE_TEST_TMP_DIR}/screenshots/${name}.ppm" args "${ARGN}")
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:renderer_demo>;${args}" -DEXPECT_CODE=${code} "-DEXPECT_OUT=${out}"
      "-DSKIP_IF_OUT=${skip_re}" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
endfunction()
set(RENDERER_NO_GPU "cannot open loader library|no physical devices|no suitable GPU")
if(APPLE)
  # macOS: Vulkan asked for by name is not built there (auto is Metal from Phase 21 Day 309; the *_metal tests ask for it by name).
  # A Mac without a Metal 3 GPU (a VM, an older Mac): auto finds no backend and says why, which skips as no GPU does elsewhere.
  string(APPEND RENDERER_NO_GPU "|backend is not built for macOS|this Mac has no Metal device|no Metal device of the Mac2 family")
endif()
set(RENDERER_NEEDS_DEVICE_MEMORY "${RENDERER_NO_GPU}")
# A normal offscreen run writes the picture and says how much it drew.
renderer_demo_expect(renderer_demo_screenshot 0 "wrote .*renderer_demo_screenshot.ppm \\(540 objects, [0-9]+ point lights" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --validation)
# And once on each backend by name, with the validation layer on and silent.
renderer_demo_expect(renderer_demo_screenshot_vulkan 0 "wrote .*renderer_demo_screenshot_vulkan.ppm \\(540 objects" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --validation --rhi vulkan)
set(_renderer_demo_dx12_tests)
if(WIN32)
  renderer_demo_expect(renderer_demo_screenshot_dx12 0 "wrote .*renderer_demo_screenshot_dx12.ppm \\(540 objects" ""
    --screenshot RENDERER_DEMO_SHOT --validation --rhi dx12)
  renderer_demo_expect(renderer_demo_device_loss_trace_dx12 3 "ENSURE FAILED[^\n]*the D3D12 device was lost" ""
    --screenshot RENDERER_DEMO_SHOT --frames 5 --inject-device-loss 3 --rhi dx12)
  renderer_demo_expect(renderer_demo_oom_device_dx12 1 "no memory for the resource 'engine staging ring'" ""
    --screenshot RENDERER_DEMO_SHOT --inject-oom 1 --rhi dx12)
  set(_renderer_demo_dx12_tests renderer_demo_screenshot_dx12 renderer_demo_device_loss_trace_dx12 renderer_demo_oom_device_dx12)
endif()
set(_renderer_demo_metal_tests)
if(APPLE)
  # Metal by name (Phase 21 Day 307; auto picks it too from Day 309): the picture with validation on, the loss trace, the staging ring's OOM.
  renderer_demo_expect(renderer_demo_screenshot_metal 0 "wrote .*renderer_demo_screenshot_metal.ppm \\(540 objects" ""
    --screenshot RENDERER_DEMO_SHOT --validation --rhi metal)
  renderer_demo_expect(renderer_demo_device_loss_trace_metal 3 "ENSURE FAILED[^\n]*the Metal device was lost" ""
    --screenshot RENDERER_DEMO_SHOT --frames 5 --inject-device-loss 3 --rhi metal)
  renderer_demo_expect(renderer_demo_oom_device_metal 1 "no memory for the resource 'engine staging ring'" ""
    --screenshot RENDERER_DEMO_SHOT --inject-oom 1 --rhi metal)
  set(_renderer_demo_metal_tests renderer_demo_screenshot_metal renderer_demo_device_loss_trace_metal renderer_demo_oom_device_metal)
endif()
renderer_demo_expect(renderer_demo_gbuffer_layers 0 "wrote" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --gbuffer normal --objects 100 --lights 10)
renderer_demo_expect(renderer_demo_device_loss_screenshot 3 "the GPU device was lost" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --frames 5 --inject-device-loss 3)
renderer_demo_expect(renderer_demo_device_loss_trace 3 "ENSURE FAILED[^\n]*the Vulkan device was lost" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --frames 5 --inject-device-loss 3 --rhi vulkan)
# Device creation allocates a driver-dependent number of times, so the third allocation fails either creation itself or
# the first resource after it; the demo must report either cleanly (exit 1), never crash.
renderer_demo_expect(renderer_demo_oom_later 1 "no [a-z0-9]+ device|could not create the target or the scene" "${RENDERER_NEEDS_DEVICE_MEMORY}"
  --screenshot RENDERER_DEMO_SHOT --inject-oom 3)
renderer_demo_expect(renderer_demo_oom_device 1 "cannot set up the staging ring" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --inject-oom 1 --rhi vulkan)
# The GPUs are listed (one line each) without a window.
renderer_demo_expect(renderer_demo_list_gpus 0 "[0-9]+ GPU\\(s\\):" "${RENDERER_NO_GPU}" --list-gpus)
# A GPU that does not exist fails cleanly and does not fall back to another one.
renderer_demo_expect(renderer_demo_unknown_gpu 1 "no [a-z0-9]+ device|could not create the target or the scene" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --gpu no-such-gpu)
renderer_demo_expect(renderer_demo_bad_injection 2 "usage" "" --inject-oom abc)
renderer_demo_expect(renderer_demo_bad_layer 2 "usage" "" --gbuffer sparkles)
renderer_demo_expect(renderer_demo_bad_objects 2 "usage" "" --objects 3)

# The live settings paths without a keyboard: a swapchain change, a forced device reset, a pipeline rebuild, a lost device and its
# recovery, in a real window on each display backend with validation on. The run must end cleanly and report no validation error.
# These open a window, so they hold the `display` resource like the other window tests and skip without a session.
function(renderer_demo_scripted name backend rhi)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:renderer_demo>;--backend;${backend};--rhi;${rhi};--validation;--frames;120;--script;frame:10 set:present_mode=immediate frame:20 reset frame:30 set:shader_quality=low frame:40 loss frame:100 quit"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=swapchain done in.*device-reset done in.*pipeline-rebuild done in.*recovered after 1 attempt"
      "-DSKIP_IF_OUT=${RENDERER_NO_GPU}" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|(ERROR|WARN): D3D12 debug layer" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
endfunction()
if(WIN32)
  renderer_demo_scripted(renderer_demo_script_win32 win32 vulkan)
  renderer_demo_scripted(renderer_demo_script_win32_dx12 win32 dx12)
  set(_crash_backend win32)
  set(_abort_code 3) # abort() ends a Windows process with exit code 3; the report in the output tells it from exit(3)
elseif(APPLE)
  renderer_demo_scripted(renderer_demo_script_cocoa cocoa metal)  # Phase 21 Day 307 (on null from Day 298)
  set(_crash_backend cocoa)
  # The crash handler's Breakpad ends a macOS process with _exit(exception type) once the dump is written
  # (third_party/sentry_native/external/breakpad/src/client/mac/handler/exception_handler.cc): an abort is EXC_SOFTWARE, 5.
  set(_abort_code 5)
else()
  renderer_demo_scripted(renderer_demo_script_wayland wayland vulkan)
  renderer_demo_scripted(renderer_demo_script_x11 x11 vulkan)
  set(_crash_backend wayland)
  set(_abort_code "Subprocess aborted")
endif()

# The text overlay (Phase 14 Day 182): the F11 toggle as a script word, in a real window with validation on; and a screenshot with the overlay in it.
function(renderer_demo_overlay_scripted name backend)
  set(rhi vulkan)
  if(APPLE)
    set(rhi metal)  # Phase 21 Day 307 (null from Day 298)
  endif()
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:renderer_demo>;--backend;${backend};--rhi;${rhi};--validation;--frames;60;--script;frame:5 overlay frame:15 overlay frame:20 reset frame:30 quit"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=text overlay is off.*text overlay is on.*device-reset done in"
      "-DSKIP_IF_OUT=${RENDERER_NO_GPU}" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|failed assertion" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
endfunction()
if(WIN32)
  renderer_demo_overlay_scripted(renderer_demo_overlay_win32 win32)
  engine_label_tests(renderer_demo_overlay_win32)
elseif(APPLE)
  renderer_demo_overlay_scripted(renderer_demo_overlay_cocoa cocoa)
  engine_label_tests(renderer_demo_overlay_cocoa)
else()
  renderer_demo_overlay_scripted(renderer_demo_overlay_wayland wayland)
  renderer_demo_overlay_scripted(renderer_demo_overlay_x11 x11)
  engine_label_tests(renderer_demo_overlay_wayland renderer_demo_overlay_x11)
endif()
renderer_demo_expect(renderer_demo_screenshot_text 0 "wrote .*renderer_demo_screenshot_text.ppm" "${RENDERER_NO_GPU}"
  --screenshot RENDERER_DEMO_SHOT --validation --rhi vulkan --text "Hello, world!")
engine_label_tests(renderer_demo_screenshot_text)

# A crash after a device reset: the report (Phase 7) shows the device epoch and the reset count in its state section.
add_test(NAME renderer_demo_crash_after_reset
  COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:renderer_demo>;--backend;${_crash_backend};--frames;60;--script;frame:10 reset frame:20 crash:abort"
    "-DEXPECT_CODE=${_abort_code}" "-DEXPECT_OUT=rhi_device: epoch [0-9]+, 1 reset\\(s\\)" "-DSKIP_IF_OUT=${RENDERER_NO_GPU}" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
    -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
set_tests_properties(renderer_demo_crash_after_reset PROPERTIES LABELS "gpu;demo;display;crash" RESOURCE_LOCK display SKIP_RETURN_CODE 77
  ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/renderer_demo_crash_after_reset;${DEMO_PROFILE_ENV}")

if(NOT APPLE)  # macOS has no Vulkan
  add_test(NAME renderer_demo_no_vulkan
    COMMAND ${CMAKE_COMMAND} -E env ENGINE_VULKAN_LIBRARY=/nonexistent
      ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:renderer_demo>;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/renderer_demo_no_vulkan.ppm;--rhi;vulkan" -DEXPECT_CODE=1 "-DEXPECT_OUT=no vulkan device"
      -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(renderer_demo_no_vulkan PROPERTIES LABELS demo ENVIRONMENT "${DEMO_PROFILE_ENV}")
  set(_renderer_demo_no_vulkan renderer_demo_no_vulkan)
endif()

# The committed textures are what the generator writes, byte for byte, and writing is deterministic.
add_test(NAME renderer_demo_assets_current
  COMMAND ${CMAKE_COMMAND} -DMINT=$<TARGET_FILE:renderer_demo_mint> -DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets
    -DWORK=${ENGINE_TEST_TMP_DIR}/renderer_demo_assets_check -P ${CMAKE_CURRENT_SOURCE_DIR}/check_assets.cmake)
set_tests_properties(renderer_demo_assets_current PROPERTIES LABELS demo)
if(WIN32)
  engine_label_tests(renderer_demo_script_win32 renderer_demo_script_win32_dx12)
elseif(APPLE)
  engine_label_tests(renderer_demo_script_cocoa)
else()
  engine_label_tests(renderer_demo_script_wayland renderer_demo_script_x11)
  # X11 copy: a soak test (docs/guide/building-and-testing.md, "Long tests"; the user, 2026-11-19)
  engine_soak_tests(renderer_demo_script_x11 renderer_demo_overlay_x11)
endif()
engine_label_tests(renderer_demo_crash_after_reset renderer_demo_list_gpus renderer_demo_unknown_gpu renderer_demo_screenshot renderer_demo_gbuffer_layers renderer_demo_device_loss_screenshot renderer_demo_device_loss_trace renderer_demo_oom_later
  renderer_demo_oom_device renderer_demo_bad_injection renderer_demo_bad_layer renderer_demo_bad_objects ${_renderer_demo_no_vulkan}
  renderer_demo_assets_current renderer_demo_screenshot_vulkan ${_renderer_demo_dx12_tests} ${_renderer_demo_metal_tests})

demo_settings_tests(renderer_demo)
