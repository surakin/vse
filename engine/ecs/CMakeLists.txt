# Phase 17: the ECS core (CPU only, portable). Depends on the foundation (settings included); the job system arrives with the parallel scheduler (Day 231).
# Built plain, ASan and TSan from one source list, each linked against the matching foundation variant, like jobs and text.
# Headers are included as "ecs/<name>.h", rooted at engine/. It includes no renderer, RHI or platform header (module_deps_check).
set(ECS_SOURCES
  entity_table.cpp
  component.cpp
  chunk.cpp
  archetype.cpp
  world.cpp
  command_buffer.cpp
  schedule.cpp
)
set(ECS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_ecs_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _ecs_flavors "_asan")
  endif()
  foreach(suffix "" ${_ecs_flavors})
    add_library(ecs${suffix} STATIC ${ECS_SOURCES})
    target_include_directories(ecs${suffix} PUBLIC ${ECS_INCLUDE_ROOT})
    target_compile_options(ecs${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(ecs${suffix} PUBLIC foundation_settings${suffix} foundation_io${suffix})
  endforeach()
  return()
endif()

add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC ${ECS_INCLUDE_ROOT})
target_compile_options(ecs PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(ecs PUBLIC foundation)

add_library(ecs_asan STATIC ${ECS_SOURCES})
target_include_directories(ecs_asan PUBLIC ${ECS_INCLUDE_ROOT})
target_compile_options(ecs_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(ecs_asan PUBLIC foundation_asan)

add_library(ecs_tsan STATIC ${ECS_SOURCES})
target_include_directories(ecs_tsan PUBLIC ${ECS_INCLUDE_ROOT})
target_compile_options(ecs_tsan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(ecs_tsan PUBLIC foundation_tsan)
