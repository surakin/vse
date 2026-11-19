# The script physics tests (Phase 27 Day 466): the physics bindings over a real physics world. Like tests/script, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions). One executable per sanitizer flavor.
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()

set(SCRIPT_PHYSICS_TEST_SOURCES
  script_physics_test.cpp  # Day 466
  script_physics_events_test.cpp  # Day 467
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_physics_tests target lib foundation_lib asset_lib suffix label)
  add_executable(${target} ${SCRIPT_PHYSICS_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${lib} ${asset_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_physics_tests(script_physics_tests script_physics foundation asset "" "script_physics")
if(ENGINE_HAVE_ASAN)
  engine_add_script_physics_tests(script_physics_tests_asan script_physics_asan foundation_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_physics_tests(script_physics_tests_tsan script_physics_tsan foundation_tsan asset_tsan " [tsan]" "tsan")
endif()
