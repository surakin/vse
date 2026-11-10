# Phase 18: the spatial module (CPU only, portable): bounding-volume hierarchies, spatial sets, queries. Layer 10, beside ecs; depends on the
# foundation (the shapes of math/geometry.h) and, from later days, ecs and jobs. Headers are included as "spatial/<name>.h", rooted at engine/.
# Built plain, ASan and TSan from one source list, each against the matching foundation variant, like ecs.
set(SPATIAL_SOURCES
  static_bvh.cpp  # Day 239
  dynamic_bvh.cpp  # Day 240
  hash_grid.cpp  # Day 242
  spatial_set.cpp  # Day 242
  pair_tracker.cpp  # Day 243
)
set(SPATIAL_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_spatial_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _spatial_flavors "_asan")
  endif()
  foreach(suffix "" ${_spatial_flavors})
    add_library(spatial${suffix} STATIC ${SPATIAL_SOURCES})
    target_include_directories(spatial${suffix} PUBLIC ${SPATIAL_INCLUDE_ROOT})
    target_compile_options(spatial${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(spatial${suffix} PUBLIC foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(spatial${suffix} STATIC ${SPATIAL_SOURCES})
  target_include_directories(spatial${suffix} PUBLIC ${SPATIAL_INCLUDE_ROOT})
  target_compile_options(spatial${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(spatial${suffix} PUBLIC foundation${suffix})
endforeach()
