# Like /tests/foundation, deliberately does NOT apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `rhi` library under test is still built
# exception/RTTI-free.
set(RHI_TEST_SOURCES
  smoke_test.cpp
  types_test.cpp
  handle_pool_test.cpp
  null_device_test.cpp
  vulkan_loader_test.cpp
  vulkan_instance_test.cpp
  vulkan_device_test.cpp
  vulkan_memory_test.cpp
  vulkan_buffer_test.cpp
  vulkan_commands_test.cpp
  vulkan_texture_test.cpp
  vulkan_fault_test.cpp
  lsan_options.cpp
  vulkan_swapchain_test.cpp
  vulkan_frame_test.cpp
  vulkan_pipeline_test.cpp
  vulkan_bindings_test.cpp
  vulkan_golden_test.cpp
  vulkan_compute_test.cpp
  vulkan_cube_test.cpp
  vulkan_textured_cube_test.cpp
  vulkan_lit_cube_test.cpp
  vulkan_render_graph_test.cpp
  vulkan_strip_test.cpp
  ${CMAKE_SOURCE_DIR}/game/common/cube_scene.cpp
  ${CMAKE_SOURCE_DIR}/game/common/textures.cpp
  image_compare.cpp
  shader_test.cpp
  shader_interface_test.cpp
)

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
function(engine_add_rhi_tests target rhi_lib rhi_vulkan_lib suffix label spec)
  add_executable(${target} ${RHI_TEST_SOURCES} ${RHI_HEADER_CHECK_SOURCES})
  # cube_scene.cpp (compiled into this executable) uses the renderer's camera helpers; same sanitizer flavor as rhi_lib.
  string(REGEX REPLACE "^rhi" "renderer" renderer_lib "${rhi_lib}")
  target_link_libraries(${target} PRIVATE ${renderer_lib} ${rhi_lib} ${rhi_vulkan_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common)
  target_compile_definitions(${target} PRIVATE ENGINE_RHI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    ENGINE_RHI_DEMO_ASSET_DIR="${CMAKE_SOURCE_DIR}/game/rhi_demo/assets"
    ENGINE_RHI_LSAN_SUPP="${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
  # Tests that do not pass a cache directory must not write to the user's ~/.cache.
  # Tests that open a window share the desktop's input focus, so they hold the `display` resource and run one at a time
  # (see tests/foundation/CMakeLists.txt); the rest run in parallel. `spec` is ANDed onto each half.
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "~[window]~[x11]~[wayland]${spec}"
    PROPERTIES LABELS "${label}" ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-${label}")
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "[window]${spec},[x11]${spec},[wayland]${spec}"
    PROPERTIES LABELS "${label}" RESOURCE_LOCK display
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-${label}")
endfunction()

engine_add_rhi_tests(rhi_tests rhi rhi_vulkan "" "rhi" "")
# The X11 presentation test is left out of the ASan flavor: the NVIDIA driver's Xlib WSI path leaks inside a library
# it unloads before exit (every frame of the report is an anonymous module, so no suppression can name it), and the
# Wayland presentation test, which runs the same code of ours, is clean.
engine_add_rhi_tests(rhi_tests_asan rhi_asan rhi_vulkan_asan " [asan]" "asan" "~[x11]")
engine_add_rhi_tests(rhi_tests_tsan rhi_tsan rhi_vulkan_tsan " [tsan]" "tsan" "~[gpu]")
