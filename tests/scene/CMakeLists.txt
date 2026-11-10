# The scene bridge tests (Phase 17 Day 233). Like tests/renderer they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use
# exceptions). The CPU tests run on the null device; one executable per sanitizer flavor.
set(SCENE_TEST_SOURCES
  scene_sync_test.cpp
  scene_render_test.cpp
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_frame.cpp
)

# The GPU test creates a real device: LeakSanitizer's suppressions and driver-unloading workaround, as for tests/renderer (Linux only).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND SCENE_TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/renderer/lsan_options.cpp)
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_scene_tests target flavor suffix label)
  add_executable(${target} ${SCENE_TEST_SOURCES} ${CMAKE_SOURCE_DIR}/tests/support/register_backends.cpp)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  target_link_libraries(${target} PRIVATE scene${flavor} renderer_passes${flavor} renderer${flavor} ecs${flavor} rhi_backends${flavor} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/rhi ${CMAKE_SOURCE_DIR}/game/renderer_demo ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_definitions(${target} PRIVATE ENGINE_RHI_LSAN_SUPP="${CMAKE_SOURCE_DIR}/tests/rhi/lsan.supp")
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_scene_tests(scene_tests "" "" "scene")
if(ENGINE_HAVE_ASAN)
  engine_add_scene_tests(scene_tests_asan "_asan" " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)
  engine_add_scene_tests(scene_tests_tsan "_tsan" " [tsan]" "tsan")
endif()
