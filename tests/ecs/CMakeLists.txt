# The ECS tests (Phase 17). Like tests/text, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the
# library under test is built without. One executable per sanitizer flavor.
set(ECS_TEST_SOURCES
  entity_test.cpp
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_ecs_tests target ecs_lib suffix label)
  add_executable(${target} ${ECS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${ecs_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
