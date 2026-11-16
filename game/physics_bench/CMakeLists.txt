# Phase 25 Day 400: the physics benchmark. Not run by ctest (tests/physics/perf_smoke_test.cpp runs a small version with a generous bound); the numbers
# are recorded in plans/phase-25-physics/day-400-benchmarks-and-stress.md and docs/guide/physics.md ("Cost").
add_executable(physics_bench main.cpp)
engine_split_debug_info(physics_bench)
target_compile_options(physics_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(physics_bench PRIVATE physics jobs foundation)
