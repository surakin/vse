# The first dogfood app: opens a window and animates a clear color and spins a cube through the RHI.
# Draws a lit, textured cube; --screenshot out.ppm renders offscreen without a window.
# Runs until the window closes, or for RHI_DEMO_FRAMES frames when that variable is set.
add_executable(rhi_demo main.cpp ../common/cube_scene.cpp ../common/textures.cpp)
target_compile_options(rhi_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_compile_definitions(rhi_demo PRIVATE RHI_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_include_directories(rhi_demo PRIVATE ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(rhi_demo PRIVATE rhi_vulkan rhi foundation)
