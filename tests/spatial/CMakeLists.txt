# The spatial tests (Phase 18): the shared shapes (Day 238) and, from Day 239, the structures. Like tests/ecs, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under test is built without. One executable per sanitizer flavor.
set(SPATIAL_TEST_SOURCES
  geometry_test.cpp
  static_bvh_test.cpp  # Day 239
  dynamic_bvh_test.cpp  # Day 240
  query_test.cpp  # Day 241
  spatial_set_test.cpp  # Day 242
  pair_test.cpp  # Day 243
  world_component_test.cpp  # Day 244
  sync_test.cpp  # Day 245
  ../ecs/alloc_counter.cpp  # the heap-allocation counter of tests/ecs (replaces operator new in this executable)
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_spatial_tests target spatial_lib suffix label)
  add_executable(${target} ${SPATIAL_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${spatial_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/ecs)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_spatial_tests(spatial_tests spatial "" "spatial")
if(ENGINE_HAVE_ASAN)
  engine_add_spatial_tests(spatial_tests_asan spatial_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_spatial_tests(spatial_tests_tsan spatial_tsan " [tsan]" "tsan")
endif()
