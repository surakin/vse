# Phase 25 Day 412: the physics benchmark. Not run by ctest (tests/physics/perf_smoke_test.cpp runs a small version with a generous bound); the numbers
# are recorded in plans/phase-25-physics/day-412-benchmarks-and-stress.md and docs/guide/physics.md ("Cost").
add_executable(physics_bench main.cpp)
engine_split_debug_info(physics_bench)
target_compile_options(physics_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(physics_bench PRIVATE physics audio_physics jobs foundation)

# Phase 25 Day 413: the Release-only stress run (the planned scale does not fit a debug or sanitizer build: Day 412 measured
# those at 8 to 15 times slower). 50000 bodies fall and pile up, then 100 bodies are stepped 1,000,000 times; the state is
# checked as it goes and a failed check exits 1. Registered only for an optimized build.
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_test(NAME physics_bench_large COMMAND physics_bench --scene large --threads 1)
  set_tests_properties(physics_bench_large PROPERTIES TIMEOUT 900)
  engine_label_tests(physics_bench_large)
  engine_soak_tests(physics_bench_large)  # 50,000 bodies for a million steps: only with --all or --soak
endif()
