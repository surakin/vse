# The Phase 4 performance baseline: draw throughput, barrier cost, upload bandwidth and pipeline creation time. Not
# run by ctest -- the numbers are recorded in plans/phase-04-vulkan-rhi/day-67-*.md, never asserted.
add_executable(rhi_bench main.cpp ../common/cube_scene.cpp)
engine_split_debug_info(rhi_bench)
target_compile_options(rhi_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(rhi_bench PRIVATE ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(rhi_bench PRIVATE renderer rhi_vulkan rhi foundation)
