# The scene bridge tests (Phase 17 Day 233). Like tests/renderer they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use
# exceptions). The CPU tests run on the null device; one executable per sanitizer flavor.
set(SCENE_TEST_SOURCES
  scene_sync_test.cpp
  scene_render_test.cpp
  scene_cost_test.cpp
  visibility_test.cpp  # Phase 18 Day 247
  level_spawner_test.cpp  # Phase 22 Day 329
  scene_spikes_test.cpp  # Phase 24 Day 358: the spikes of the C API's scenes
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/level_source.cpp  # Day 329: the level minter, to make the levels the spawner loads
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_frame.cpp
)

# The GPU test creates a real device: LeakSanitizer's suppressions and driver-unloading workaround, as for tests/renderer (Linux only).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND SCENE_TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/renderer/lsan_options.cpp)
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_scene_tests target flavor suffix label spec)
  add_executable(${target} ${SCENE_TEST_SOURCES} ${CMAKE_SOURCE_DIR}/tests/support/register_backends.cpp)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  target_link_libraries(${target} PRIVATE scene${flavor} renderer_passes${flavor} renderer${flavor} ecs${flavor} rhi_backends${flavor} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/rhi ${CMAKE_SOURCE_DIR}/game/renderer_demo ${CMAKE_SOURCE_DIR}/tools/asset_mint ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_definitions(${target} PRIVATE ENGINE_RHI_LSAN_SUPP="${CMAKE_SOURCE_DIR}/tests/rhi/lsan.supp" "SPIKE_FONT_FILE=\"${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf\"")
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "${spec}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_scene_tests(scene_tests "" "" "scene" "")
if(ENGINE_HAVE_ASAN)
  engine_add_scene_tests(scene_tests_asan "_asan" " [asan]" "asan" "")
endif()
if(ENGINE_HAVE_TSAN)
  # The [gpu] tests leave TSan out on Linux, as in tests/rhi: the NVIDIA driver starts threads TSan does not know ("CHECK failed: thr->slot != 0" in its
  # allocator interceptor, a SIGSEGV in the driver's calloc; Phase 23 Day 349). Metal's run under it.
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    engine_add_scene_tests(scene_tests_tsan "_tsan" " [tsan]" "tsan" "")
  else()
    engine_add_scene_tests(scene_tests_tsan "_tsan" " [tsan]" "tsan" "~[gpu]")
  endif()
endif()
