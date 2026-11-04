# The first dogfood app: opens a window and animates a clear color through the RHI.
# Draws the triangle; --screenshot out.ppm renders offscreen without a window.
# Runs until the window closes, or for RHI_DEMO_FRAMES frames when that variable is set.
add_executable(rhi_demo main.cpp)
target_compile_options(rhi_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(rhi_demo PRIVATE ${CMAKE_SOURCE_DIR}/shaders)
target_link_libraries(rhi_demo PRIVATE rhi_vulkan rhi foundation)
