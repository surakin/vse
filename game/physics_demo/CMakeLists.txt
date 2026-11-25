# The physics showcase: random shapes falling onto a floor, simulated by engine/physics through the ECS and drawn by the
# deferred renderer, in a window or offscreen (--screenshot). The scene is in a small static library so a test builds the
# very same world.
add_library(physics_demo_lib STATIC physics_scene.cpp)
target_compile_options(physics_demo_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(physics_demo_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(physics_demo_lib PUBLIC physics scene ecs renderer_passes renderer rhi foundation)

add_executable(physics_demo main.cpp ../common/settings_main.cpp ../common/textures.cpp)
engine_split_debug_info(physics_demo)
target_compile_options(physics_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(physics_demo PRIVATE ${CMAKE_SOURCE_DIR}/game/common)
target_compile_definitions(physics_demo PRIVATE
  PHYSICS_DEMO_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
target_link_libraries(physics_demo PRIVATE physics_demo_lib rhi_backends)

# A run offscreen: the shapes fall for 400 frames, none leaves the tray, and the picture is written. Skips (77) without a GPU,
# as the renderer demo's tests do.
add_test(NAME physics_demo_screenshot
  COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:physics_demo>;--frames;400;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/physics_demo_screenshot.ppm"
    -DEXPECT_CODE=0 "-DEXPECT_OUT=wrote .*physics_demo_screenshot.ppm \\([0-9]+ shapes, [0-9]+ objects drawn, 400 physics steps"
    "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU|VK_ERROR_INCOMPATIBLE_DRIVER" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
set_tests_properties(physics_demo_screenshot PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
  ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/physics_demo_screenshot;${DEMO_PROFILE_ENV}")
engine_label_tests(physics_demo_screenshot)
