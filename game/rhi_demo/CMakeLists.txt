# The first dogfood app: opens a window and animates a clear color and spins a cube through the RHI.
# Draws a lit, textured cube; --screenshot out.ppm renders offscreen without a window.
# Runs until the window closes, or for RHI_DEMO_FRAMES frames when that variable is set.
add_executable(rhi_demo main.cpp ../common/cube_scene.cpp ../common/textures.cpp)
engine_split_debug_info(rhi_demo)
target_compile_options(rhi_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(rhi_demo PRIVATE RHI_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_include_directories(rhi_demo PRIVATE ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(rhi_demo PRIVATE renderer rhi_vulkan rhi foundation)

# Failure injection and missing-Vulkan behaviour of the demo (Phase 3 Day 66). The shell is the process boundary, so
# a crash (signal) shows up as a wrong exit code. Tests that need a GPU skip (77) without one.
set(RHI_DEMO_SHOT ${CMAKE_CURRENT_BINARY_DIR}/fault.ppm)
function(rhi_demo_expect name code out skip_re)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:rhi_demo>;${ARGN}" -DEXPECT_CODE=${code} "-DEXPECT_OUT=${out}"
      "-DSKIP_IF_OUT=${skip_re}" -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache")
endfunction()
set(RHI_NO_GPU "cannot open loader library|no physical devices|no suitable GPU")
rhi_demo_expect(rhi_demo_device_loss_screenshot 3 "the GPU device was lost" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --frames 5 --inject-device-loss 3)
# Device creation allocates a driver-dependent number of times, so the third allocation fails either creation itself or
# the first resource after it; the demo must report either cleanly (exit 1), never crash.
rhi_demo_expect(rhi_demo_oom_later 1 "no Vulkan device|could not create the target or the cube" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --inject-oom 3)
# The very first allocation (the staging ring) fails: creation fails with that reason.
rhi_demo_expect(rhi_demo_oom_device 1 "cannot set up the staging ring" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --inject-oom 1)
# A mistyped fault is a usage error (exit 2), not "inject nothing".
rhi_demo_expect(rhi_demo_bad_injection 2 "usage" "" --inject-oom abc)
rhi_demo_expect(rhi_demo_negative_injection 2 "usage" "" --inject-device-loss -1)

# Without a Vulkan loader the demo reports it and exits 1; it does not crash. Needs no GPU.
add_test(NAME rhi_demo_no_vulkan
  COMMAND ${CMAKE_COMMAND} -E env ENGINE_VULKAN_LIBRARY=/nonexistent
    ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:rhi_demo>;--screenshot;${RHI_DEMO_SHOT}" -DEXPECT_CODE=1 "-DEXPECT_OUT=no Vulkan device"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_run.cmake)
set_tests_properties(rhi_demo_no_vulkan PROPERTIES LABELS demo)
engine_label_tests(rhi_demo_device_loss_screenshot rhi_demo_oom_later rhi_demo_oom_device rhi_demo_bad_injection
  rhi_demo_negative_injection rhi_demo_no_vulkan)
