# Phase 4: the Render Hardware Interface (docs/superpowers/specs/
# 2026-11-04-rhi-design.md). `rhi` holds the backend-agnostic interface, the
# backend registry (with CreateDevice, EnumerateAdapters and DeviceHost) and
# the validating null backend; `rhi_vulkan` is the only place Vulkan is
# named; `rhi_backends` registers every backend the build has (Phase 13 Day
# 162) and is linked by programs, never by the renderer. Built plain and under ASan/UBSan from one source list, each linked
# against the matching foundation/jobs variant (the TSan flavor arrived on
# Day 54, with multithreaded command recording).
set(RHI_SOURCES
  rhi.cpp
  adapter.cpp
  create_device.cpp
  device_host.cpp
  desc.cpp
  spirv.cpp
  uniform_ring.cpp
  staging_ring.cpp
  pipeline_cache_file.cpp
  null/null_device.cpp
  null/null_backend.cpp
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
  vulkan/vulkan_backend.cpp
)

# The presentation surface of each platform's windows (Phase 13 Day 160): Xlib in its own translation unit on Linux (Wayland's is in
# vulkan_device_surface.cpp), Win32 under windows/ (it includes <windows.h>).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND RHI_VULKAN_SOURCES vulkan/vk_xlib_surface.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND RHI_VULKAN_SOURCES vulkan/windows/vk_win32_surface.cpp)
endif()

# The Direct3D 12 backend (Phase 13, Days 164-172), Windows only: the one place D3D12 and DXGI are named (rhi_boundary_check). It links
# the OS's own d3d12.dll and dxgi.dll through their import libraries.
set(RHI_DX12_SOURCES
  dx12/dx12_result.cpp
  dx12/dx12_debug.cpp
  dx12/dx12_adapters.cpp
  dx12/dx12_format.cpp
  dx12/dx12_descriptor_heap.cpp
  dx12/dx12_device.cpp
  dx12/dx12_device_resources.cpp
  dx12/dx12_device_bindings.cpp
  dx12/dx12_device_pipelines.cpp
  dx12/dx12_pipeline_cache.cpp
  dx12/dx12_device_later.cpp
  dx12/dx12_backend.cpp
)

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

  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(rhi_dx12${suffix} STATIC ${RHI_DX12_SOURCES})
    target_include_directories(rhi_dx12${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
    target_compile_options(rhi_dx12${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
    target_link_libraries(rhi_dx12${suffix} PUBLIC rhi${suffix} d3d12 dxgi dxguid)
  endif()

  add_library(rhi_backends${suffix} STATIC backends/backends.cpp)
  target_include_directories(rhi_backends${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
  target_compile_options(rhi_backends${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(rhi_backends${suffix} PUBLIC rhi_vulkan${suffix} rhi${suffix})
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(rhi_backends${suffix} PUBLIC rhi_dx12${suffix})
    target_compile_definitions(rhi_backends${suffix} PRIVATE ENGINE_RHI_HAVE_DX12=1) # the table in backends.cpp lists it
  endif()
endfunction()

engine_add_rhi_libs("" foundation jobs platform)
if(ENGINE_HAVE_ASAN)
  engine_add_rhi_libs("_asan" foundation_asan jobs_asan platform_asan)
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_rhi_libs("_tsan" foundation_tsan jobs_tsan platform_tsan)
endif()
