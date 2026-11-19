# The script demo's tests (Phase 27 Day 475): the demo's world (game/script_demo) built into the test, run at several thread counts and
# compared by state hash, with hot reload. Like tests/script, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use
# exceptions). One executable per sanitizer flavor.
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_demo_tests target lib media_lib foundation_lib asset_lib suffix label)
  add_executable(${target} script_demo_test.cpp
    ${CMAKE_SOURCE_DIR}/game/script_demo/demo_world.cpp
    ${CMAKE_BINARY_DIR}/game/script_demo/demo_script.cpp)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/game/script_demo ${CMAKE_SOURCE_DIR}/tests/io ${CMAKE_SOURCE_DIR}/tests/support)
  target_compile_definitions(${target} PRIVATE SCRIPT_DEMO_MINTED_DIR="${CMAKE_BINARY_DIR}/game/script_demo/minted")
  add_dependencies(${target} script_demo_minted)
  target_link_libraries(${target} PRIVATE ${media_lib} ${lib} ${asset_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_demo_tests(script_demo_tests script_physics script_media foundation asset "" "script_demo")
if(ENGINE_HAVE_ASAN)
  engine_add_script_demo_tests(script_demo_tests_asan script_physics_asan script_media_asan foundation_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_demo_tests(script_demo_tests_tsan script_physics_tsan script_media_tsan foundation_tsan asset_tsan " [tsan]" "tsan")
endif()
