# The script demo's tests (Phase 27 Day 475): the demo's world (game/script_demo) built into the test, run at several thread counts and
# compared by state hash, with hot reload. Like tests/script, they do not apply ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use
# exceptions). One executable per sanitizer flavor.
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux x86-64 only so far
endif()

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_demo_tests target lib foundation_lib asset_lib suffix label)
  add_executable(${target} script_demo_test.cpp
    ${CMAKE_SOURCE_DIR}/game/script_demo/demo_world.cpp
    ${CMAKE_BINARY_DIR}/game/script_demo/demo_script.cpp)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/game/script_demo)
  target_link_libraries(${target} PRIVATE ${lib} ${asset_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_demo_tests(script_demo_tests script_physics foundation asset "" "script_demo")
if(ENGINE_HAVE_ASAN)
  engine_add_script_demo_tests(script_demo_tests_asan script_physics_asan foundation_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_demo_tests(script_demo_tests_tsan script_physics_tsan foundation_tsan asset_tsan " [tsan]" "tsan")
endif()
