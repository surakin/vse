# The model viewer (maintenance, 2026-11-19; plans/maintenance/model-viewer.md): a glTF or GLB file named on the command line, imported
# in memory, its default scene spawned and framed in a window with an orbit camera, a skinned model playing its clips. A program under
# game/ may link the importer (tools/asset_boundary_check's allow list).
add_library(model_viewer_lib STATIC orbit_camera.cpp viewer_scene.cpp)
target_compile_options(model_viewer_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(model_viewer_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(model_viewer_lib PUBLIC asset_import scene animation asset storage ecs renderer_passes renderer rhi jobs foundation)

add_executable(model_viewer main.cpp ../common/settings_main.cpp ../common/textures.cpp)
engine_split_debug_info(model_viewer)
target_compile_options(model_viewer PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(model_viewer PRIVATE ${CMAKE_SOURCE_DIR}/game/common)
target_compile_definitions(model_viewer PRIVATE
  MODEL_VIEWER_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf")
target_link_libraries(model_viewer PRIVATE model_viewer_lib platform rhi_backends)

set(MODEL_VIEWER_FIXTURES ${CMAKE_SOURCE_DIR}/tests/engine_library/fixtures)
# Offscreen: a skinned, animated robot and a static meadow, each drawn for 120 frames and written. Skips (77) without a GPU.
function(model_viewer_screenshot name file expect)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:model_viewer>;${file};--frames;120;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/${name}.ppm"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=${expect}"
      "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
model_viewer_screenshot(model_viewer_screenshot_robot ${MODEL_VIEWER_FIXTURES}/robot.glb
  "wrote .*model_viewer_screenshot_robot.ppm \\([0-9]+ meshes, [1-9][0-9]* objects drawn, [1-9][0-9]* triangles, [1-9][0-9]* clips, [1-9][0-9]* skinned")
model_viewer_screenshot(model_viewer_screenshot_field ${MODEL_VIEWER_FIXTURES}/green_field.glb
  "wrote .*model_viewer_screenshot_field.ppm \\([0-9]+ meshes, [1-9][0-9]* objects drawn, [1-9][0-9]* triangles, 0 clips")
model_viewer_screenshot(model_viewer_screenshot_gltf ${CMAKE_SOURCE_DIR}/tests/asset/data/gltf/model.gltf
  "wrote .*model_viewer_screenshot_gltf.ppm \\([0-9]+ meshes, [1-9][0-9]* objects drawn")

# A file that is not there, and one that is not glTF: a message and exit 1, before any window or device.
add_test(NAME model_viewer_missing_file COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:model_viewer>;${CMAKE_CURRENT_BINARY_DIR}/no-such.glb"
  -DEXPECT_CODE=1 "-DEXPECT_OUT=no-such.glb cannot be read" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
add_test(NAME model_viewer_not_gltf COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:model_viewer>;${CMAKE_CURRENT_SOURCE_DIR}/main.cpp;--screenshot;${ENGINE_TEST_TMP_DIR}/screenshots/model_viewer_not_gltf.ppm"
  -DEXPECT_CODE=1 "-DEXPECT_OUT=main.cpp: " "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
set_tests_properties(model_viewer_not_gltf PROPERTIES LABELS "gpu;demo" SKIP_RETURN_CODE 77)
engine_label_tests(model_viewer_missing_file model_viewer_not_gltf)

# In a real window on each display backend, with validation on: 300 frames of the animated robot.
function(model_viewer_windowed name backend)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:model_viewer>;${MODEL_VIEWER_FIXTURES}/robot.glb;--backend;${backend};--set;renderer.validation=on;--frames;300"
      -DEXPECT_CODE=0 "-DEXPECT_OUT=showed robot.glb for 300 frames"
      "-DSKIP_IF_OUT=cannot open loader library|no physical devices|no suitable GPU" "-DSKIP_NO_DISPLAY_OUT=could not open a window"
      "-DFORBID_OUT=a Vulkan validation error was reported|(ERROR|WARN): D3D12 debug layer|failed assertion" -P ${CMAKE_SOURCE_DIR}/game/rhi_demo/expect_run.cmake)
  set_tests_properties(${name} PROPERTIES LABELS "gpu;demo;display" RESOURCE_LOCK display SKIP_RETURN_CODE 77
    ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/${name};${DEMO_PROFILE_ENV}")
  engine_label_tests(${name})
endfunction()
if(WIN32)
  model_viewer_windowed(model_viewer_window_win32 win32)
elseif(APPLE)
  model_viewer_windowed(model_viewer_window_cocoa cocoa)
else()
  model_viewer_windowed(model_viewer_window_wayland wayland)
  model_viewer_windowed(model_viewer_window_x11 x11)
endif()
