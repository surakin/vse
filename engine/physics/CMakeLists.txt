# Phase 25: the physics module (CPU only, portable): rigid bodies, colliders, the fixed step. Layer 13, above spatial, ecs and jobs (docs/superpowers/
# specs/2026-11-16-physics-design.md). Headers are included as "physics/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list, each
# against the matching foundation variant, like spatial. The floating-point rules (no contraction, no fast-math) come from ENGINE_FP_COMPILE_OPTIONS,
# which the root CMakeLists.txt applies to every target of ours.
set(PHYSICS_SOURCES
  determinism.cpp  # Day 386
  validation.cpp  # Day 387
  shapes.cpp  # Day 387
  mass.cpp  # Day 387
  components.cpp  # Day 387
  world.cpp  # Day 388
  sync.cpp  # Day 388
  broad_phase.cpp  # Day 389
  step.cpp  # Day 390
  physics_settings.cpp  # Day 390
  system.cpp  # Day 390
  collide.cpp  # Day 391
  collide_box.cpp  # Day 392
  hull.cpp  # Day 393
  collide_hull.cpp  # Day 393
  triangle_mesh.cpp  # Day 394
  compound.cpp  # Day 395
  collide_compound.cpp  # Day 395
  solver.cpp  # Day 396
  joint.cpp  # Day 398
  island.cpp  # Day 400
  ccd.cpp  # Day 402
  query.cpp  # Day 403
  query_index.cpp  # query and hierarchy indices, Day 1
  character.cpp  # Day 404
  character_set.cpp  # Phase 27 Day 467, moved from script_physics (C API script physics, 2026-11-20)
  events.cpp  # Day 405
  debug_draw.cpp  # Day 405
  joint_sync.cpp  # Day 405
  snapshot.cpp  # Day 406
  input_log.cpp  # Day 406
)
set(PHYSICS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_physics_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _physics_flavors "_asan")
  endif()
  foreach(suffix "" ${_physics_flavors})
    add_library(physics${suffix} STATIC ${PHYSICS_SOURCES})
    target_include_directories(physics${suffix} PUBLIC ${PHYSICS_INCLUDE_ROOT})
    target_compile_options(physics${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(physics${suffix} PUBLIC spatial${suffix} ecs${suffix} storage${suffix} foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(physics${suffix} STATIC ${PHYSICS_SOURCES})
  target_include_directories(physics${suffix} PUBLIC ${PHYSICS_INCLUDE_ROOT})
  target_compile_options(physics${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(physics${suffix} PUBLIC spatial${suffix} ecs${suffix} storage${suffix} foundation${suffix})
endforeach()
