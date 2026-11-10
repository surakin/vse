# Phase 17 Day 235: the ECS benchmark against the object-oriented baseline. Not run by ctest (tests/ecs compiles bench.cpp for its checks and a ratio guard);
# the numbers are recorded in plans/phase-17-ecs/day-235-benchmarks.md and docs/guide/ecs.md.
add_executable(ecs_bench main.cpp bench.cpp)
engine_split_debug_info(ecs_bench)
target_compile_options(ecs_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(ecs_bench PRIVATE ecs jobs foundation)
