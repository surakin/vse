# Phase 5: the renderer (docs/superpowers/specs/2026-11-04-renderer-design.md). It sits on the RHI only and never names
# Vulkan (enforced by tools/rhi_boundary_check). Built plain and under ASan/UBSan and TSan from one source list, each
# linked against the matching rhi/jobs/foundation variant.
set(RENDERER_SOURCES
  renderer_info.cpp
  camera.cpp
  mesh.cpp
  render_scene.cpp
  frame_data.cpp
  material.cpp
  render_graph.cpp
  transient_pool.cpp
  renderer.cpp
  render_settings.cpp
  terrain_tiles.cpp  # Phase 28 Day 487
  terrain_material.cpp  # Phase 28 Day 489
  terrain_source.cpp  # Phase 28 Day 490
  instance_set.cpp  # Phase 28 Day 495
  shared_settings.cpp
  device_resources.cpp
  texture.cpp
  glyph_atlas_texture.cpp
)

# The passes embed the committed shader includes (<name>.spv.inc), which `shader_mint` generates and which itself links the
# core renderer library, so they live in a second library to keep the build graph acyclic.
set(RENDERER_PASS_SOURCES
  gbuffer.cpp
  lighting.cpp
  sky.cpp
  terrain_pass.cpp  # Phase 28 Day 488
  text_renderer.cpp
  ui_renderer.cpp
  viewport_target.cpp
  ui_surface_target.cpp
  scene_frame.cpp  # Phase 24 Day 359 (moved from game/renderer_demo)
)

# Shader reflection and permutation keys: the part of the renderer the shader minter uses. A library of its own below `renderer`
# (glTF coverage Day 14): `shader_mint` links it, and must not link `renderer`, which embeds shader includes `shader_mint` generates
# (the morph pass's compute shader is in the material system), or the build is a cycle. It needs only the RHI's types and the foundation.
set(RENDERER_REFLECTION_SOURCES
  shader_reflection.cpp
  shader_permutation.cpp
)

# Headers are included as "renderer/<name>.h", rooted at engine/.
set(RENDERER_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_renderer_lib suffix)
  add_library(renderer_reflection${suffix} STATIC ${RENDERER_REFLECTION_SOURCES})
  target_include_directories(renderer_reflection${suffix} PUBLIC ${RENDERER_INCLUDE_ROOT})
  target_compile_options(renderer_reflection${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(renderer_reflection${suffix} PUBLIC rhi${suffix} foundation${suffix})
  add_library(renderer${suffix} STATIC ${RENDERER_SOURCES})
  target_include_directories(renderer${suffix} PUBLIC ${RENDERER_INCLUDE_ROOT})
  # The morph pass's compute shader (glTF coverage Day 7): the material system owns its pipeline, so it survives a reset.
  target_include_directories(renderer${suffix} PRIVATE ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_options(renderer${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  # Only the neutral RHI: devices come through the backend registry, and the program links rhi_backends (Phase 13 Day 162).
  target_link_libraries(renderer${suffix} PUBLIC renderer_reflection${suffix} rhi${suffix} jobs${suffix} text${suffix} ui${suffix} spatial${suffix} foundation${suffix}) # spatial: the culling hierarchy of RenderScene (Phase 18 Day 247); ui: the draw list the UI pass draws (Phase 15 Day 190); text: the CPU glyph atlas the GPU copy follows (Phase 14 Day 180)
  add_library(renderer_passes${suffix} STATIC ${RENDERER_PASS_SOURCES})
  target_include_directories(renderer_passes${suffix} PRIVATE ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_options(renderer_passes${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(renderer_passes${suffix} PUBLIC renderer${suffix})
endfunction()

engine_add_renderer_lib("")
if(ENGINE_HAVE_ASAN)
  engine_add_renderer_lib("_asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_renderer_lib("_tsan")
endif()
