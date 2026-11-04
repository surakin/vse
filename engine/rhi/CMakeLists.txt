# Phase 3: the Render Hardware Interface (docs/superpowers/specs/
# 2026-11-04-rhi-design.md). `rhi` holds the backend-agnostic interface and
# the validating null backend; `rhi_vulkan` is the only place Vulkan is
# named. Built plain and under ASan/UBSan from one source list, each linked
# against the matching foundation/jobs variant (a TSan flavor is added on
# Day 54, when multithreaded recording exists).
set(RHI_SOURCES
  rhi.cpp
  desc.cpp
  null/null_device.cpp
)

set(RHI_VULKAN_SOURCES
  vulkan/vk_loader.cpp
  vulkan/vk_functions.cpp
  vulkan/vk_result.cpp
  vulkan/vk_instance.cpp
)

# Headers are included as "rhi/<name>.h", rooted at engine/.
set(RHI_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_rhi_libs suffix foundation_lib jobs_lib)
  add_library(rhi${suffix} STATIC ${RHI_SOURCES})
  target_include_directories(rhi${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
  target_compile_options(rhi${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(rhi${suffix} PUBLIC ${foundation_lib} ${jobs_lib})

  add_library(rhi_vulkan${suffix} STATIC ${RHI_VULKAN_SOURCES})
  target_include_directories(rhi_vulkan${suffix} PUBLIC ${RHI_INCLUDE_ROOT})
  target_compile_options(rhi_vulkan${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(rhi_vulkan${suffix} PUBLIC rhi${suffix})
endfunction()

engine_add_rhi_libs("" foundation jobs)
engine_add_rhi_libs("_asan" foundation_asan jobs_asan)
