# Phase 18: the spatial module (CPU only, portable): bounding-volume hierarchies, spatial sets, queries. Layer 10, beside ecs; depends on the
# foundation (the shapes of math/geometry.h) and, from later days, ecs and jobs. Headers are included as "spatial/<name>.h", rooted at engine/.
# Day 238 creates it with headers only (an interface library); the first sources arrive with the static BVH (Day 239). Built plain, ASan and TSan,
# each against the matching foundation variant, like ecs.
set(SPATIAL_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_spatial_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _spatial_flavors "_asan")
  endif()
  foreach(suffix "" ${_spatial_flavors})
    add_library(spatial${suffix} INTERFACE)
    target_include_directories(spatial${suffix} INTERFACE ${SPATIAL_INCLUDE_ROOT})
    target_link_libraries(spatial${suffix} INTERFACE foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(spatial${suffix} INTERFACE)
  target_include_directories(spatial${suffix} INTERFACE ${SPATIAL_INCLUDE_ROOT})
  target_link_libraries(spatial${suffix} INTERFACE foundation${suffix})
endforeach()
