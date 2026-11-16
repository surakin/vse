# Phase 25: the physics module (CPU only, portable): rigid bodies, colliders, the fixed step. Layer 13, above spatial, ecs and jobs (docs/superpowers/
# specs/2026-11-16-physics-design.md). Headers are included as "physics/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list, each
# against the matching foundation variant, like spatial. The floating-point rules (no contraction, no fast-math) come from ENGINE_FP_COMPILE_OPTIONS,
# which the root CMakeLists.txt applies to every target of ours.
set(PHYSICS_SOURCES
  determinism.cpp  # Day 374
  validation.cpp  # Day 375
  shapes.cpp  # Day 375
  mass.cpp  # Day 375
  components.cpp  # Day 375
  world.cpp  # Day 376
  sync.cpp  # Day 376
  broad_phase.cpp  # Day 377
  step.cpp  # Day 378
  physics_settings.cpp  # Day 378
  system.cpp  # Day 378
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
    target_link_libraries(physics${suffix} PUBLIC spatial${suffix} ecs${suffix} foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(physics${suffix} STATIC ${PHYSICS_SOURCES})
  target_include_directories(physics${suffix} PUBLIC ${PHYSICS_INCLUDE_ROOT})
  target_compile_options(physics${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(physics${suffix} PUBLIC spatial${suffix} ecs${suffix} foundation${suffix})
endforeach()
