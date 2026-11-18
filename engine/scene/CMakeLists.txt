# Phase 17 Day 233: the scene bridge (ECS entities to the renderer's RenderScene). Layer 13: depends on ecs and renderer; nothing in ecs or renderer includes it.
# Built plain, ASan and TSan from one source list, each linked against the matching ecs and renderer variant.
set(SCENE_SOURCES
  render_sync.cpp
  level_spawner.cpp  # Phase 22 Day 329
  scene_components.cpp  # Phase 27 Day 456
)
set(SCENE_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

function(engine_add_scene_lib suffix)
  add_library(scene${suffix} STATIC ${SCENE_SOURCES})
  target_include_directories(scene${suffix} PUBLIC ${SCENE_INCLUDE_ROOT})
  target_compile_options(scene${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(scene${suffix} PUBLIC ecs${suffix} renderer${suffix} animation${suffix} asset${suffix})
endfunction()

engine_add_scene_lib("")
if(ENGINE_HAVE_ASAN)
  engine_add_scene_lib("_asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_scene_lib("_tsan")
endif()
