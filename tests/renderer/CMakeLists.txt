# Like /tests/rhi, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `renderer` library under test is still built exception/RTTI-free.
set(RENDERER_TEST_SOURCES
  smoke_test.cpp
  shader_reflection_test.cpp
  shader_permutation_test.cpp
  shader_dxil_test.cpp
  shader_metallib_test.cpp  # Phase 21 Day 299
  geometry_test.cpp
  scene_test.cpp
  mesh_topology_test.cpp
  material_test.cpp
  render_graph_test.cpp
  render_graph_barrier_test.cpp
  renderer_test.cpp
  gbuffer_test.cpp
  skinning_test.cpp  # Phase 22 Day 322
  lighting_test.cpp
  sky_test.cpp
  terrain_tiles_test.cpp  # Phase 28 Day 487
  terrain_material_test.cpp  # Phase 28 Day 489
  instance_set_test.cpp  # Phase 28 Day 495
  instance_cull_test.cpp  # Phase 28 Day 496
  instance_tree_test.cpp  # Phase 28 Day 499
  instance_tree_cull_test.cpp  # Phase 28 Day 500
  renderer_fault_test.cpp
  render_settings_test.cpp
  device_resources_test.cpp
  recreatable_renderer_test.cpp
  apply_settings_test.cpp
  device_loss_recovery_test.cpp
  reset_soak_test.cpp
  shared_settings_test.cpp
  text_renderer_test.cpp
  text_pass_test.cpp
  ui_renderer_test.cpp
  viewport_target_test.cpp
  ui_surface_target_test.cpp
  # The demo's frame and scene are compiled into the test binary (as tests/rhi does) so each sanitizer flavor gets its own objects.
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_scene.cpp
  ${CMAKE_SOURCE_DIR}/game/renderer_demo/demo_assets.cpp
  ${CMAKE_SOURCE_DIR}/game/common/textures.cpp
)
# LeakSanitizer's suppressions and its driver-unloading workaround: Linux only (MSVC's AddressSanitizer has no leak checker).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND RENDERER_TEST_SOURCES lsan_options.cpp)
endif()

# Header self-containment: one generated translation unit per public renderer header that includes only that header.
file(GLOB RENDERER_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/renderer/*.h)
set(RENDERER_HEADER_CHECK_SOURCES "")
foreach(header ${RENDERER_PUBLIC_HEADERS})
  get_filename_component(header_name ${header} NAME_WE)
  set(check_src ${CMAKE_CURRENT_BINARY_DIR}/header_check_${header_name}.cpp)
  file(GENERATE OUTPUT ${check_src} CONTENT "#include \"renderer/${header_name}.h\"\n")
  list(APPEND RENDERER_HEADER_CHECK_SOURCES ${check_src})
endforeach()

find_package(Threads REQUIRED)
include(Catch)

# One executable per sanitizer flavor, as for the RHI. GPU tests (tag [gpu], label `gpu`) skip without a device; the
# TSan flavor excludes them (the proprietary NVIDIA driver's threads crash ThreadSanitizer).
function(engine_add_renderer_tests target suffix label spec)
  string(REGEX REPLACE "^renderer_tests" "" flavor "${target}")
  add_executable(${target} ${RENDERER_TEST_SOURCES} ${RENDERER_HEADER_CHECK_SOURCES} ${CMAKE_SOURCE_DIR}/tests/support/register_backends.cpp ${CMAKE_SOURCE_DIR}/tests/support/crash_handler_listener.cpp)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target}) # before the discovery below, which runs the program (Windows: the runtime DLLs beside it)
  endif()
  # rhi_backends registers the backends (tests/support/register_backends.cpp calls it before the run); the renderer library
  # itself does not depend on any backend, and tests/renderer is scanned by rhi_boundary_check, so a test cannot name Vulkan.
  target_link_libraries(${target} PRIVATE scene${flavor} ecs${flavor} renderer_passes${flavor} renderer${flavor} rhi_backends${flavor} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/game/common ${CMAKE_SOURCE_DIR}/game/renderer_demo ${CMAKE_SOURCE_DIR}/tests/io ${CMAKE_SOURCE_DIR}/tests/support ${CMAKE_SOURCE_DIR}/tests/rhi ${CMAKE_SOURCE_DIR}/tests/text)
  target_compile_definitions(${target} PRIVATE TEXT_TEST_FONT_FILE="${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf" ENGINE_RHI_LSAN_SUPP="${CMAKE_SOURCE_DIR}/tests/rhi/lsan.supp" SETTINGS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/settings/fixtures" ENGINE_TEST_CRASH_DIR="${ENGINE_TEST_TMP_DIR}/crash-dumps/${target}")
  # On macOS the GPU suites run through Metal (Phase 21 Day 307) with its API and shader validation on, an error stopping the test.
  set(env "ENGINE_RHI_CACHE_DIR=${ENGINE_TEST_TMP_DIR}/pipeline-cache/renderer_tests-${label}")
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND env MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1)
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "${spec}" PROPERTIES LABELS "${label}" ENVIRONMENT "${env}")
  # The instance set's million-instance form (1,000,000 instead of 50,000, tests/support/soak.h): the plain flavor only.
  if(label STREQUAL "renderer")
    engine_soak_catch_test(${target} "An instance set holds and edits a million instances and agrees with a model"
      SOURCE tests/renderer/instance_set_test.cpp)
    engine_soak_catch_test(${target} "Instance tree cull: the work follows the visible nodes, not the tree"
      SOURCE tests/renderer/instance_tree_cull_test.cpp)
  endif()
endfunction()

engine_add_renderer_tests(renderer_tests "" "renderer" "")
if(ENGINE_HAVE_ASAN)
  engine_add_renderer_tests(renderer_tests_asan " [asan]" "asan" "~[x11]")
endif()
if(ENGINE_HAVE_TSAN)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    engine_add_renderer_tests(renderer_tests_tsan " [tsan]" "tsan" "")  # Metal's GPU tests run under TSan (Phase 21 Day 307)
  else()
    engine_add_renderer_tests(renderer_tests_tsan " [tsan]" "tsan" "~[gpu]")
  endif()
endif()
