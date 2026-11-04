# Phase 4: the renderer (docs/superpowers/specs/2026-11-04-renderer-design.md). It sits on the RHI only and never names
# Vulkan (enforced by tools/rhi_boundary_check). Built plain and under ASan/UBSan and TSan from one source list, each
# linked against the matching rhi/jobs/foundation variant.
set(RENDERER_SOURCES
  renderer_info.cpp
)

# Headers are included as "renderer/<name>.h", rooted at engine/.
set(RENDERER_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_renderer_lib suffix)
  add_library(renderer${suffix} STATIC ${RENDERER_SOURCES})
  target_include_directories(renderer${suffix} PUBLIC ${RENDERER_INCLUDE_ROOT})
  target_compile_options(renderer${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(renderer${suffix} PUBLIC rhi${suffix} jobs${suffix} foundation${suffix})
endfunction()

engine_add_renderer_lib("")
engine_add_renderer_lib("_asan")
engine_add_renderer_lib("_tsan")
