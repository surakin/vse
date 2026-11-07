# Phase 2: the job system, a portable layer above Platform (PLAN.md
# assumption #15). Built three ways from one source list — plain, ASan/
# UBSan, and ThreadSanitizer (TSan can't combine with ASan, so it is its
# own build; Phase 2 Day 27) — each linked against the matching
# foundation variant so instrumentation is consistent end to end.
set(JOBS_SOURCES
  frame_pool.cpp
  job.cpp
  topology.cpp
  worker_pool.cpp
  scheduler.cpp
  async_io.cpp
  file_async.cpp
)
# The per-platform half (jobs/detail/thread_platform.h; Phase 13 Day 153): CPU list, thread id, pinning, naming.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND JOBS_SOURCES windows/thread_platform.cpp)
else()
  list(APPEND JOBS_SOURCES linux/thread_platform.cpp)
endif()

# Headers are included as "jobs/<name>.h", rooted at engine/.
set(JOBS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Windows (Phase 13): plain and ASan. The job system uses the base and file layers only, so it links foundation_io until the
  # foundation aggregate exists there (the debugging and settings layers, Days 154-157).
  set(_jobs_flavors "")
  if(ENGINE_HAVE_ASAN)
    list(APPEND _jobs_flavors "_asan")
  endif()
  foreach(suffix "" ${_jobs_flavors})
    add_library(jobs${suffix} STATIC ${JOBS_SOURCES})
    target_include_directories(jobs${suffix} PUBLIC ${JOBS_INCLUDE_ROOT})
    target_compile_options(jobs${suffix} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${ENGINE_PRODUCTION_COMPILE_OPTIONS}>)
    target_link_libraries(jobs${suffix} PUBLIC foundation_io${suffix})
  endforeach()
  return()
endif()

add_library(jobs STATIC ${JOBS_SOURCES})
target_include_directories(jobs PUBLIC ${JOBS_INCLUDE_ROOT})
target_compile_options(jobs PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(jobs PUBLIC foundation)

add_library(jobs_asan STATIC ${JOBS_SOURCES})
target_include_directories(jobs_asan PUBLIC ${JOBS_INCLUDE_ROOT})
target_compile_options(jobs_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(jobs_asan PUBLIC foundation_asan)

add_library(jobs_tsan STATIC ${JOBS_SOURCES})
target_include_directories(jobs_tsan PUBLIC ${JOBS_INCLUDE_ROOT})
target_compile_options(jobs_tsan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(jobs_tsan PUBLIC foundation_tsan)
