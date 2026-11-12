# The first dogfood app: opens a window and animates a clear color and spins a cube through the RHI.
# Draws a lit, textured cube; --screenshot out.ppm renders offscreen without a window.
# Runs until the window closes, or for RHI_DEMO_FRAMES frames when that variable is set.
add_executable(rhi_demo main.cpp ../common/cube_scene.cpp ../common/textures.cpp ../common/settings_main.cpp)
engine_split_debug_info(rhi_demo)
target_compile_options(rhi_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(rhi_demo PRIVATE RHI_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_include_directories(rhi_demo PRIVATE ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(rhi_demo PRIVATE renderer rhi_backends rhi foundation)

# Failure injection and missing-Vulkan behaviour of the demo (Phase 4 Day 66). The shell is the process boundary, so
# a crash (signal) shows up as a wrong exit code. Tests that need a GPU skip (77) without one.
set(RHI_DEMO_SHOT ${ENGINE_TEST_TMP_DIR}/screenshots/rhi_demo_fault.ppm)
function(rhi_demo_expect name code out skip_re)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:rhi_demo>;${ARGN}" -DEXPECT_CODE=${code} "-DEXPECT_OUT=${out}"
      "-DSKIP_IF_OUT=${skip_re}" -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/rhi_demo;${DEMO_PROFILE_ENV}")
endfunction()
set(RHI_NO_GPU "cannot open loader library|no physical devices|no suitable GPU")
if(APPLE)
  # macOS until Metal draws (Phase 26 Day 325): a backend asked for by name is not built (no GPU), and auto runs the null
  # backend, which has no device memory for an allocation fault to hit.
  string(APPEND RHI_NO_GPU "|backend is not built for macOS")
  set(RHI_NEEDS_DEVICE_MEMORY "${RHI_NO_GPU}|using the null backend")
else()
  set(RHI_NEEDS_DEVICE_MEMORY "${RHI_NO_GPU}")
endif()
rhi_demo_expect(rhi_demo_device_loss_screenshot 3 "the GPU device was lost" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --frames 5 --inject-device-loss 3)
# A lost device also leaves a trace of where it was noticed (an ensure, once), and the frame still ends as lost (exit 3).
rhi_demo_expect(rhi_demo_device_loss_trace 3 "ENSURE FAILED[^\n]*the Vulkan device was lost" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --frames 5 --inject-device-loss 3 --rhi vulkan)
# Device creation allocates a driver-dependent number of times, so the third allocation fails either creation itself or
# the first resource after it; the demo must report either cleanly (exit 1), never crash.
rhi_demo_expect(rhi_demo_oom_later 1 "no [a-z0-9]+ device|could not create the target or the cube" "${RHI_NEEDS_DEVICE_MEMORY}"
  --screenshot ${RHI_DEMO_SHOT} --inject-oom 3)
# The very first allocation (the staging ring) fails: creation fails with that reason.
rhi_demo_expect(rhi_demo_oom_device 1 "cannot set up the staging ring" "${RHI_NO_GPU}"
  --screenshot ${RHI_DEMO_SHOT} --inject-oom 1 --rhi vulkan)
set(_rhi_demo_dx12_tests)
if(WIN32)
  # The same faults on Direct3D 12 (Phase 13 Day 173): its loss and its staging ring report themselves in their own words.
  rhi_demo_expect(rhi_demo_device_loss_trace_dx12 3 "ENSURE FAILED[^\n]*the D3D12 device was lost" ""
    --screenshot ${RHI_DEMO_SHOT} --frames 5 --inject-device-loss 3 --rhi dx12)
  rhi_demo_expect(rhi_demo_oom_device_dx12 1 "no memory for the resource 'engine staging ring'" ""
    --screenshot ${RHI_DEMO_SHOT} --inject-oom 1 --rhi dx12)
  rhi_demo_expect(rhi_demo_list_gpus_dx12 0 "backend dx12.*Microsoft Basic Render Driver" "" --list-gpus --rhi dx12)
  set(_rhi_demo_dx12_tests rhi_demo_device_loss_trace_dx12 rhi_demo_oom_device_dx12 rhi_demo_list_gpus_dx12)
endif()
# A mistyped fault is a usage error (exit 2), not "inject nothing".
# A mistyped fault is a usage error (exit 2), not "inject nothing".
# The GPUs are listed without a window; a GPU that does not exist fails cleanly and does not fall back to another.
rhi_demo_expect(rhi_demo_list_gpus 0 "[0-9]+ GPU\\(s\\):" "${RHI_NO_GPU}" --list-gpus)
rhi_demo_expect(rhi_demo_unknown_gpu 1 "no [a-z0-9]+ device" "${RHI_NO_GPU}" --screenshot ${RHI_DEMO_SHOT} --gpu no-such-gpu)
# A graphics backend that is not a name is a usage error of the setting (exit 2).
rhi_demo_expect(rhi_demo_bad_rhi 2 "renderer.backend" "" --rhi sparkles)
rhi_demo_expect(rhi_demo_bad_injection 2 "usage" "" --inject-oom abc)
rhi_demo_expect(rhi_demo_negative_injection 2 "usage" "" --inject-device-loss -1)


# Without a Vulkan loader the demo reports it and exits 1; it does not crash. Needs no GPU. (Not on macOS, which has no Vulkan.)
if(NOT APPLE)
  add_test(NAME rhi_demo_no_vulkan
    COMMAND ${CMAKE_COMMAND} -E env ENGINE_VULKAN_LIBRARY=/nonexistent
      ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:rhi_demo>;--screenshot;${RHI_DEMO_SHOT};--rhi;vulkan" -DEXPECT_CODE=1 "-DEXPECT_OUT=no vulkan device"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_run.cmake)
  set_tests_properties(rhi_demo_no_vulkan PROPERTIES LABELS demo ENVIRONMENT "${DEMO_PROFILE_ENV}")
  set(_rhi_demo_no_vulkan rhi_demo_no_vulkan)
endif()
engine_label_tests(rhi_demo_list_gpus rhi_demo_unknown_gpu rhi_demo_device_loss_screenshot rhi_demo_device_loss_trace rhi_demo_oom_later rhi_demo_oom_device rhi_demo_bad_injection
  rhi_demo_negative_injection ${_rhi_demo_no_vulkan} rhi_demo_bad_rhi ${_rhi_demo_dx12_tests})

demo_settings_tests(rhi_demo)
