# The ECS tests (Phase 17). Like tests/text, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the
# library under test is built without. One executable per sanitizer flavor.
set(ECS_TEST_SOURCES
  entity_test.cpp
  component_test.cpp
  archetype_test.cpp
  world_test.cpp
  query_test.cpp
  command_test.cpp
  change_test.cpp
  raw_access_test.cpp
  schedule_test.cpp
  parallel_test.cpp
  transform_test.cpp
  hierarchy_index_test.cpp  # query and hierarchy indices, Day 2
  removal_test.cpp
  bench_test.cpp
  ../../game/ecs_bench/bench.cpp
  alloc_counter.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_ecs_tests target ecs_lib suffix label)
  add_executable(${target} ${ECS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${ecs_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/game/ecs_bench)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_ecs_tests(ecs_tests ecs "" "ecs")
if(ENGINE_HAVE_ASAN)
  engine_add_ecs_tests(ecs_tests_asan ecs_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_ecs_tests(ecs_tests_tsan ecs_tsan " [tsan]" "tsan")
endif()

# The component requirements are compile-time errors: case 0 of compile_fail/component_requirements.cpp compiles, the others must not.
# Checked at configure time with the same include path and language standard as the engine.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)  # compile only: the cases need no link
foreach(_case RANGE 0 7)
  try_compile(_ecs_case_${_case}_ok
    ${CMAKE_CURRENT_BINARY_DIR}/compile_fail_${_case}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/component_requirements.cpp
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/engine;${CMAKE_SOURCE_DIR}/engine/foundation"
    COMPILE_DEFINITIONS -DCASE=${_case}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
  if(_case EQUAL 0 AND NOT _ecs_case_0_ok)
    message(FATAL_ERROR "tests/ecs: the compile-fail control (case 0) does not compile, so the negative cases prove nothing")
  elseif(NOT _case EQUAL 0 AND _ecs_case_${_case}_ok)
    message(FATAL_ERROR "tests/ecs: component_requirements.cpp case ${_case} compiled, but the requirement must refuse it")
  endif()
endforeach()

# The query term rules are compile-time errors as well: case 0 of compile_fail/query_terms.cpp compiles, the others must not.
foreach(_case RANGE 0 9)
  try_compile(_ecs_query_case_${_case}_ok
    ${CMAKE_CURRENT_BINARY_DIR}/compile_fail_query_${_case}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/query_terms.cpp
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_SOURCE_DIR}/engine;${CMAKE_SOURCE_DIR}/engine/foundation"
    COMPILE_DEFINITIONS -DCASE=${_case}
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
  if(_case EQUAL 0 AND NOT _ecs_query_case_0_ok)
    message(FATAL_ERROR "tests/ecs: the query compile-fail control (case 0) does not compile, so the negative cases prove nothing")
  elseif(NOT _case EQUAL 0 AND _ecs_query_case_${_case}_ok)
    message(FATAL_ERROR "tests/ecs: query_terms.cpp case ${_case} compiled, but the term rules must refuse it")
  endif()
endforeach()
