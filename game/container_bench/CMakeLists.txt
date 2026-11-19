# Container audit Day 1 (maintenance series, plans/maintenance/container-audit.md): the benchmark of the foundation
# containers and of the engine paths the audit found slow. Run by hand in a Release build; nothing is asserted, the
# numbers are recorded in the series' day files.
add_executable(container_bench main.cpp micro.cpp paths.cpp)
engine_split_debug_info(container_bench)
target_compile_options(container_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(container_bench PRIVATE renderer ui script storage asset ecs rhi foundation)
