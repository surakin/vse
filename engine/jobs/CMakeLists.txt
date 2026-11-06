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

# Headers are included as "jobs/<name>.h", rooted at engine/.
set(JOBS_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

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
