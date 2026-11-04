# The first dogfood app: opens a window and animates a clear color through the RHI.
# Runs until the window closes, or for RHI_DEMO_FRAMES frames when that variable is set.
add_executable(rhi_demo main.cpp)
target_compile_options(rhi_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(rhi_demo PRIVATE rhi_vulkan rhi foundation)
