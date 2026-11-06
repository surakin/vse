# The Phase 5 performance baseline: render graph cost, CPU frame time, draws after sorting, light volume cost and the transient
# pool's hit rate. Not run by ctest -- the numbers are recorded in plans/phase-05-renderer-core/day-83-*.md, never asserted.
add_executable(renderer_bench main.cpp)
engine_split_debug_info(renderer_bench)
target_compile_options(renderer_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(renderer_bench PRIVATE RENDERER_DEMO_ASSET_DIR="${CMAKE_SOURCE_DIR}/game/renderer_demo/assets")
target_link_libraries(renderer_bench PRIVATE renderer_demo_lib jobs rhi_vulkan)
