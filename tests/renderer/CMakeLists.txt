# Like /tests/rhi, deliberately does NOT apply ENGINE_PRODUCTION_COMPILE_OPTIONS: Catch2's REQUIRE/CHECK are
# exception-based. The `renderer` library under test is still built exception/RTTI-free.
set(RENDERER_TEST_SOURCES
  smoke_test.cpp
  shader_reflection_test.cpp
  shader_permutation_test.cpp
  geometry_test.cpp
  scene_test.cpp
  lsan_options.cpp
)

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
  add_executable(${target} ${RENDERER_TEST_SOURCES} ${RENDERER_HEADER_CHECK_SOURCES})
  # rhi_vulkan is linked only so tests can create a device through rhi::CreateDevice (which lives there); the renderer
  # library itself does not depend on it, and tests/renderer is scanned by rhi_boundary_check, so a test cannot name Vulkan.
  target_link_libraries(${target} PRIVATE renderer${flavor} rhi_vulkan${flavor} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders)
  target_compile_definitions(${target} PRIVATE ENGINE_RHI_LSAN_SUPP="${CMAKE_SOURCE_DIR}/tests/rhi/lsan.supp")
  catch_discover_tests(${target} TEST_SUFFIX "${suffix}" TEST_SPEC "${spec}"
    PROPERTIES LABELS "${label}" ENVIRONMENT "ENGINE_RHI_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-${label}")
endfunction()

engine_add_renderer_tests(renderer_tests "" "renderer" "")
engine_add_renderer_tests(renderer_tests_asan " [asan]" "asan" "~[x11]")
engine_add_renderer_tests(renderer_tests_tsan " [tsan]" "tsan" "~[gpu]")
