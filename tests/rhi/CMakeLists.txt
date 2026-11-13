# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `rhi` library under test is still built
# exception/RTTI-free.
set(RHI_TEST_SOURCES
  smoke_test.cpp
  types_test.cpp
  handle_pool_test.cpp
  null_device_test.cpp
  adapter_test.cpp
  backend_registry_test.cpp
  pipeline_cache_file_test.cpp
  device_host_test.cpp
  swapchain_reconfigure_test.cpp
  reset_golden_gpu_test.cpp
  text_golden_gpu_test.cpp
  ui_golden_gpu_test.cpp
  adapter_switch_gpu_test.cpp
  lifecycle_conformance_test.cpp
  vulkan_loader_test.cpp
  vulkan_instance_test.cpp
  vulkan_device_test.cpp
  vulkan_memory_test.cpp
  vulkan_buffer_test.cpp
  vulkan_commands_test.cpp
  vulkan_texture_test.cpp
  vulkan_fault_test.cpp
  vulkan_swapchain_test.cpp
  vulkan_frame_test.cpp
  vulkan_pipeline_test.cpp
  vulkan_bindings_test.cpp
  golden_test.cpp
  vulkan_compute_test.cpp
  compute_test.cpp
  cube_test.cpp
  textured_cube_test.cpp
  lit_cube_test.cpp
  render_graph_gpu_test.cpp
  vulkan_strip_test.cpp
  vulkan_vertex_format_test.cpp  # Phase 22 Day 317
  gbuffer_gpu_test.cpp
  lighting_gpu_test.cpp
  renderer_demo_gpu_test.cpp
  ui_demo_gpu_test.cpp
  ${CMAKE_SOURCE_DIR}/game/ui_demo/ui_demo_app.cpp
  ${CMAKE_SOURCE_DIR}/game/ui_demo/ui_demo_gpu.cpp
  ${CMAKE_SOURCE_DIR}/game/ui_demo/ui_script.cpp
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_scene.cpp
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_frame.cpp
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_assets.cpp
  ${CMAKE_SOURCE_DIR}/game/common/cube_scene.cpp
  ${CMAKE_SOURCE_DIR}/game/common/textures.cpp
  image_compare.cpp
  shader_test.cpp
  shader_interface_test.cpp
)

# The Vulkan backend's own tests: not on macOS, which has no Vulkan (Phase 21 decision 8; the list is in the macOS port spec). The
# neutral and null-device suites, and the GPU-tagged tests (which skip without a GPU backend until Metal), run there.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(FILTER RHI_TEST_SOURCES EXCLUDE REGEX "/?vulkan_[a-z_]+_test\\.cpp$")
  # The Metal backend's own tests (Phase 21, Days 300-309), Objective-C++ under macos/, linked through rhi_backends (which links rhi_metal).
  list(APPEND RHI_TEST_SOURCES macos/metal_device_test.mm macos/metal_resource_test.mm macos/metal_bindings_test.mm macos/metal_pipeline_test.mm macos/metal_commands_test.mm macos/metal_swapchain_test.mm macos/metal_graph_test.mm macos/metal_lifecycle_test.mm macos/metal_vertex_format_test.mm)
endif()

# LeakSanitizer's suppressions and its driver-unloading workaround: Linux only (MSVC's AddressSanitizer has no leak checker).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND RHI_TEST_SOURCES lsan_options.cpp)
endif()
# The Direct3D 12 backend's own tests (Phase 13, Days 164-172): Windows only, linked through rhi_backends (which links rhi_dx12).
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND RHI_TEST_SOURCES dx12_device_test.cpp dx12_resource_test.cpp dx12_bindings_test.cpp dx12_pipeline_test.cpp dx12_commands_test.cpp dx12_swapchain_test.cpp)
endif()

# Header self-containment: one generated translation unit per public RHI
# header that includes only that header, so a header that relies on another
# having been included first (or that a sibling header shadows a foundation
# one for) fails to build. Headers are globbed, so new ones are covered
# automatically.
file(GLOB RHI_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/rhi/*.h)
set(RHI_HEADER_CHECK_SOURCES "")
foreach(header ${RHI_PUBLIC_HEADERS})
  get_filename_component(header_name ${header} NAME_WE)
  set(check_src ${CMAKE_CURRENT_BINARY_DIR}/header_check_${header_name}.cpp)
  file(GENERATE OUTPUT ${check_src} CONTENT "#include \"rhi/${header_name}.h\"\n")
  list(APPEND RHI_HEADER_CHECK_SOURCES ${check_src})
endforeach()

find_package(Threads REQUIRED)
include(Catch)

# One executable per sanitizer flavor; ctest names get a suffix so the
# flavors don't collide. GPU tests (tag [gpu], label `gpu`) are added from
# Day 49 and skip when no Vulkan device exists.
#
# `spec` is a Catch2 test spec limiting what ctest registers. The TSan flavor excludes [gpu]: the proprietary
# NVIDIA driver starts threads that ThreadSanitizer cannot track and crashes inside its allocator interceptor, so
# GPU threading is covered by the ASan flavor plus validation layers, and TSan covers the GPU-free
# multithreaded tests (null device) and the pure logic. With a Mesa software driver, run the whole binary by hand.
function(engine_add_rhi_tests target rhi_lib rhi_vulkan_lib rhi_backends_lib suffix label spec)
  add_executable(${target} ${RHI_TEST_SOURCES} ${RHI_HEADER_CHECK_SOURCES} ${CMAKE_SOURCE_DIR}/tests/support/register_backends.cpp)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target}) # before the discovery below, which runs the program (Windows: the runtime DLLs beside it)
  endif()
  # cube_scene.cpp (compiled into this executable) uses the renderer's camera helpers; same sanitizer flavor as rhi_lib.
  string(REGEX REPLACE "^rhi" "renderer" renderer_lib "${rhi_lib}")
  string(REGEX REPLACE "^rhi" "renderer_passes" passes_lib "${rhi_lib}")
  string(REGEX REPLACE "^rhi" "ui" ui_lib "${rhi_lib}")
  string(REGEX REPLACE "^rhi" "scene" scene_lib "${rhi_lib}")
  string(REGEX REPLACE "^rhi" "ecs" ecs_lib "${rhi_lib}")
  if(NOT TARGET ${rhi_vulkan_lib})
    set(rhi_vulkan_lib "")  # macOS: no Vulkan library (rhi_backends then leaves ENGINE_RHI_HAVE_VULKAN undefined)
  endif()
  target_link_libraries(${target} PRIVATE ${scene_lib} ${ecs_lib} ${ui_lib} ${passes_lib} ${renderer_lib} ${rhi_lib} ${rhi_vulkan_lib} ${rhi_backends_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common ${CMAKE_SOURCE_DIR}/game/renderer_demo ${CMAKE_SOURCE_DIR}/game/ui_demo ${CMAKE_SOURCE_DIR}/tests/renderer ${CMAKE_SOURCE_DIR}/tests/io ${CMAKE_SOURCE_DIR}/tests/support)
  target_compile_definitions(${target} PRIVATE ENGINE_RHI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    ENGINE_RHI_DEMO_ASSET_DIR="${CMAKE_SOURCE_DIR}/game/rhi_demo/assets"
    ENGINE_RENDERER_DEMO_ASSET_DIR="${CMAKE_SOURCE_DIR}/game/renderer_demo/assets"
    ENGINE_TEXT_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf"
    ENGINE_RHI_LSAN_SUPP="${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
  # Tests that do not pass a cache directory must not write to the user's ~/.cache. On macOS every test runs with Metal's API and
  # shader validation on, a validation error stopping the test (Phase 21 Day 300): Metal reads these when the process starts it.
  set(env "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/rhi_tests-${label}")
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # ENGINE_RHI_EXPECT_METAL_VALIDATION makes the Metal device test require the others, so losing them fails it.
    list(APPEND env MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1 ENGINE_RHI_EXPECT_METAL_VALIDATION=1)
  endif()
  # Tests that open a window share the desktop's input focus, so they hold the `display` resource and run one at a time
  # (see tests/foundation/CMakeLists.txt); the rest run in parallel. `spec` is ANDed onto each half.
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "~[window]~[x11]~[wayland]~[archive]${spec}"
    PROPERTIES LABELS "${label}" ENVIRONMENT "${env}")
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "[window]${spec},[x11]${spec},[wayland]${spec}"
    PROPERTIES LABELS "${label};display" RESOURCE_LOCK display ENVIRONMENT "${env}")
  # The Metal pipeline archive's tests (tag [archive], Phase 21 Day 309): Metal stops inside a binary archive while its
  # shader validation is on, and the device then leaves the archive off, so these run with the API validation alone.
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(archive_env "${env}")
    list(REMOVE_ITEM archive_env MTL_SHADER_VALIDATION=1)
    engine_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "[archive]${spec}"
      PROPERTIES LABELS "${label}" ENVIRONMENT "${archive_env}")
  endif()
endfunction()

engine_add_rhi_tests(rhi_tests rhi rhi_vulkan rhi_backends "" "rhi" "")
# The X11 presentation test is left out of the ASan flavor: the NVIDIA driver's Xlib WSI path leaks inside a library
# it unloads before exit (every frame of the report is an anonymous module, so no suppression can name it), and the
# Wayland presentation test, which runs the same code of ours, is clean.
if(ENGINE_HAVE_ASAN)
  engine_add_rhi_tests(rhi_tests_asan rhi_asan rhi_vulkan_asan rhi_backends_asan " [asan]" "asan" "~[x11]")
endif()
if(ENGINE_HAVE_TSAN)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # The [gpu] tests leave TSan out on Linux for the NVIDIA driver's threads; Metal's run under it (Phase 21 Day 300).
    engine_add_rhi_tests(rhi_tests_tsan rhi_tsan rhi_vulkan_tsan rhi_backends_tsan " [tsan]" "tsan" "")
  else()
    engine_add_rhi_tests(rhi_tests_tsan rhi_tsan rhi_vulkan_tsan rhi_backends_tsan " [tsan]" "tsan" "~[gpu]")
  endif()
endif()

# A planted Metal validation error (Phase 21 Day 300): a validation error must stop a program run with the GPU tests' environment.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  add_executable(metal_validation_planted macos/metal_validation_planted.mm)
  target_link_libraries(metal_validation_planted PRIVATE rhi_backends)
  # Through a driver script: the assertion aborts the program, and ctest fails any abort whatever its output.
  add_test(NAME metal_validation_planted COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:metal_validation_planted>
           -P ${CMAKE_CURRENT_SOURCE_DIR}/macos/run_validation_planted.cmake)
  set_tests_properties(metal_validation_planted PROPERTIES
    ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_DEBUG_LAYER_ERROR_MODE=assert"
    PASS_REGULAR_EXPRESSION "stopped by Metal's validation"
    FAIL_REGULAR_EXPRESSION "the planted call was not caught|validation is off")
  engine_label_tests(metal_validation_planted)
  # Every Metal object the backend's calls autorelease is drained by a pool of its own (Phase 21 Day 309): a caller
  # with no pool (a job thread, a C++ test) would otherwise keep it, and a released object autoreleases its device, so
  # a hundred resets held about 37 MiB. Only the test's own thread counts (tests/rhi/macos/run_autorelease_pools.cmake).
  # Without Metal's validation layer this run also keeps the loop's memory check, which that layer's own growth defeats.
  add_test(NAME metal_autorelease_pools COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:rhi_tests>
           -P ${CMAKE_CURRENT_SOURCE_DIR}/macos/run_autorelease_pools.cmake)
  set_tests_properties(metal_autorelease_pools PROPERTIES
    ENVIRONMENT "OBJC_DEBUG_MISSING_POOLS=YES;ENGINE_RHI_PRINT_TEST_THREAD=1;ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/metal_autorelease_pools"
    PASS_REGULAR_EXPRESSION "no Metal object autoreleased without a pool")
  engine_label_tests(metal_autorelease_pools)
endif()

# Phase 8 Day 114: the lifecycle checklist for the DX12 and Metal backends stays in step with the interfaces, and the neutral RHI
# headers compile for the Windows and macOS targets (needs clang++; skips without it).
add_test(NAME rhi_lifecycle_lists COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_lifecycle_list.cmake)
function(engine_rhi_cross_syntax_test name triple)
  add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -DTRIPLE=${triple} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cross_syntax/check_rhi_cross_syntax.cmake)
endfunction()
engine_rhi_cross_syntax_test(rhi_cross_syntax_windows x86_64-pc-windows-msvc)
# Kept now that macOS builds for real (Phase 21 Day 286): the Intel development Mac builds x86-64 only, so this is what compiles these
# headers for arm64 (Apple silicon) until Phase 21 Day 311's arm64 build.
engine_rhi_cross_syntax_test(rhi_cross_syntax_macos arm64-apple-macos13)
engine_label_tests(rhi_lifecycle_lists rhi_cross_syntax_windows rhi_cross_syntax_macos)
