# Phase 4: the Render Hardware Interface (docs/superpowers/specs/
# 2026-11-04-rhi-design.md). `rhi` holds the backend-agnostic interface and
# the validating null backend; `rhi_vulkan` is the only place Vulkan is
# named. Built plain and under ASan/UBSan from one source list, each linked
# against the matching foundation/jobs variant (the TSan flavor arrived on
# Day 54, with multithreaded command recording).
set(RHI_SOURCES
  rhi.cpp
  adapter.cpp
  desc.cpp
  spirv.cpp
  uniform_ring.cpp
  null/null_device.cpp
)

set(RHI_VULKAN_SOURCES
  vulkan/vk_loader.cpp
  vulkan/vk_functions.cpp
  vulkan/vk_result.cpp
  vulkan/vk_instance.cpp
  vulkan/vk_format.cpp
  vulkan/vk_memory.cpp
  vulkan/vk_memory_backend.cpp
  vulkan/vk_device_select.cpp
  vulkan/vk_ring.cpp
  vulkan/vulkan_device.cpp
  vulkan/vulkan_device_buffers.cpp
  vulkan/vulkan_device_commands.cpp
  vulkan/vulkan_device_textures.cpp
  vulkan/vulkan_device_surface.cpp
  vulkan/vk_swapchain.cpp
  vulkan/vk_swapchain_policy.cpp
  vulkan/vk_pipeline_policy.cpp
  vulkan/vulkan_device_pipelines.cpp
  vulkan/vulkan_device_bindings.cpp
  vulkan/vk_barrier.cpp
  vulkan/vk_command_list.cpp
  vulkan/create_device.cpp
  vulkan/device_host.cpp
)

# The presentation surface of each platform's windows (Phase 13 Day 160): Xlib in its own translation unit on Linux (Wayland's is in
# vulkan_device_surface.cpp), Win32 under windows/ (it includes <windows.h>).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND RHI_VULKAN_SOURCES vulkan/vk_xlib_surface.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND RHI_VULKAN_SOURCES vulkan/windows/vk_win32_surface.cpp)
endif()

# Headers are included as "rhi/<name>.h", rooted at engine/.
set(RHI_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_rhi_libs suffix foundation_lib jobs_lib platform_lib)
  add_library(rhi${suffix} STATIC ${RHI_SOURCES})
  target_include_directories(rhi${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
  target_compile_options(rhi${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(rhi${suffix} PUBLIC ${foundation_lib} ${jobs_lib} ${platform_lib})

  add_library(rhi_vulkan${suffix} STATIC ${RHI_VULKAN_SOURCES})
  target_include_directories(rhi_vulkan${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
  target_compile_options(rhi_vulkan${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(rhi_vulkan${suffix} PUBLIC rhi${suffix})
endfunction()

engine_add_rhi_libs("" foundation jobs platform)
if(ENGINE_HAVE_ASAN)
  engine_add_rhi_libs("_asan" foundation_asan jobs_asan platform_asan)
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_rhi_libs("_tsan" foundation_tsan jobs_tsan platform_tsan)
endif()
