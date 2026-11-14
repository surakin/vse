# Phase 22 Day 321: the animation runtime (library `animation`): poses, blends, the clip sampler, skinning matrices and the ECS
# components and parallel system that play clips. Layer 13 beside asset: it reads asset's skeleton and clip views (the format
# layouts live in asset since Days 318 and 320), so asset -> animation is NOT an edge; animation -> asset, ecs, storage, jobs is.
# Headers are included as "animation/<name>.h", rooted at engine/. Built plain, ASan and TSan from one source list.
set(ANIMATION_SOURCES
  pose.cpp
  sampler.cpp
  system.cpp
)
set(ANIMATION_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

foreach(suffix "" "_asan" "_tsan")
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND suffix STREQUAL "_tsan")
    continue()
  endif()
  if(suffix STREQUAL "_asan" AND NOT ENGINE_HAVE_ASAN)
    continue()
  endif()
  if(suffix STREQUAL "_tsan" AND NOT ENGINE_HAVE_TSAN)
    continue()
  endif()
  add_library(animation${suffix} STATIC ${ANIMATION_SOURCES})
  target_include_directories(animation${suffix} PUBLIC ${ANIMATION_INCLUDE_ROOT})
  target_compile_options(animation${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
  target_link_libraries(animation${suffix} PUBLIC asset${suffix} ecs${suffix} storage${suffix} jobs${suffix} foundation${suffix})
endforeach()
