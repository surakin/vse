# Phase 19: the storage module: the component registry and the canonical encoding (Day 256-257), identities (258), the record store and its backends (259-263).
# Layer 11, above ecs, spatial, jobs and database; nothing below includes it. Headers are included as "storage/<name>.h", rooted at engine/.
# Built plain, ASan and TSan from one source list, each against the matching foundation variant, like spatial.
set(STORAGE_SOURCES
  status.cpp  # Day 256
  component_registry.cpp  # Day 256
  builtin_components.cpp  # Day 256
)
set(STORAGE_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_storage_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _storage_flavors "_asan")
  endif()
  foreach(suffix "" ${_storage_flavors})
    add_library(storage${suffix} STATIC ${STORAGE_SOURCES})
    target_include_directories(storage${suffix} PUBLIC ${STORAGE_INCLUDE_ROOT})
    target_compile_options(storage${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(storage${suffix} PUBLIC ecs${suffix} spatial${suffix} foundation_base${suffix})
  endforeach()
  return()
endif()

foreach(suffix "" "_asan" "_tsan")
  add_library(storage${suffix} STATIC ${STORAGE_SOURCES})
  target_include_directories(storage${suffix} PUBLIC ${STORAGE_INCLUDE_ROOT})
  target_compile_options(storage${suffix} PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(storage${suffix} PUBLIC ecs${suffix} spatial${suffix} foundation${suffix})
endforeach()
