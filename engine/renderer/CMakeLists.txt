# Phase 4: the renderer (docs/superpowers/specs/2026-11-04-renderer-design.md). It sits on the RHI only and never names
# Vulkan (enforced by tools/rhi_boundary_check). Built plain and under ASan/UBSan and TSan from one source list, each
# linked against the matching rhi/jobs/foundation variant.
set(RENDERER_SOURCES
  renderer_info.cpp
  shader_reflection.cpp
  shader_permutation.cpp
  bounds.cpp
  camera.cpp
  frustum.cpp
  mesh.cpp
  render_scene.cpp
  frame_data.cpp
  material.cpp
  render_graph.cpp
  transient_pool.cpp
  renderer.cpp
  render_settings.cpp
  device_resources.cpp
  texture.cpp
)

# The passes embed the committed shader includes (<name>.spv.inc), which `shader_mint` generates and which itself links the
# core renderer library, so they live in a second library to keep the build graph acyclic.
set(RENDERER_PASS_SOURCES
  gbuffer.cpp
  lighting.cpp
)

# Headers are included as "renderer/<name>.h", rooted at engine/.
set(RENDERER_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_renderer_lib suffix)
  add_library(renderer${suffix} STATIC ${RENDERER_SOURCES})
  target_include_directories(renderer${suffix} PUBLIC ${RENDERER_INCLUDE_ROOT})
  target_compile_options(renderer${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  # rhi_vulkan comes with the RHI the renderer sits on: the settings layer creates devices through rhi::DeviceHost.
  target_link_libraries(renderer${suffix} PUBLIC rhi_vulkan${suffix} rhi${suffix} jobs${suffix} foundation${suffix})
  add_library(renderer_passes${suffix} STATIC ${RENDERER_PASS_SOURCES})
  target_include_directories(renderer_passes${suffix} PRIVATE ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_options(renderer_passes${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(renderer_passes${suffix} PUBLIC renderer${suffix})
endfunction()

engine_add_renderer_lib("")
engine_add_renderer_lib("_asan")
engine_add_renderer_lib("_tsan")
