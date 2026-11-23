# Phase 18 Day 248: the spatial demo, a headless console program (the rendered version with a window waits for Phase 35's scene work, KNOWN-ISSUES.md).
add_executable(spatial_demo main.cpp)
engine_split_debug_info(spatial_demo)
target_compile_options(spatial_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(spatial_demo PRIVATE spatial ecs jobs foundation)
add_test(NAME spatial_demo_runs COMMAND spatial_demo --entities 5000 --frames 20 --stats --debug-draw)
set_tests_properties(spatial_demo_runs PROPERTIES LABELS "demo" PASS_REGULAR_EXPRESSION "spatial_demo: ok")
engine_label_tests(spatial_demo_runs)
