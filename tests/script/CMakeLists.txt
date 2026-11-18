# The script tests (Phase 27): the smoke test of the vendored LuaJIT (Day 445) and, from Day 446, the module. Like tests/audio, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions). One executable per sanitizer flavor; the flavors link the same `luajit` library
# (its assembler core is not instrumented, KNOWN-ISSUES.md).
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux x86-64 only so far
endif()

set(SCRIPT_TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/engine/script/luajit/luajit_smoke_test.cpp  # Day 445, moved beside the backend on Day 448 (the one place that names LuaJIT)
  value_test.cpp  # Day 446
  backend_test.cpp  # Day 446
  backend_conformance_test.cpp  # Day 447
  luajit_backend_test.cpp  # Day 448
  bindings_test.cpp  # Day 450
  script_world_test.cpp  # Day 451
  world_view_test.cpp  # Day 452
  runtime_bindings_test.cpp  # Day 453
  settings_bindings_test.cpp  # Day 454
  entity_bindings_test.cpp  # Day 455
  component_bindings_test.cpp  # Day 456
  script_system_test.cpp  # Day 457
  parallel_script_test.cpp  # Day 458
  event_test.cpp  # Day 459
  latent_test.cpp  # Day 460
  task_test.cpp  # Day 461
  script_load_test.cpp  # Day 463: minted scripts through the resource manager
  script_asset_test.cpp  # Day 462: the SCRP blob and the script minter under test
  ${CMAKE_SOURCE_DIR}/tools/asset_mint/script_source.cpp  # Day 462
)

find_package(Threads REQUIRED)
include(Catch)

function(engine_add_script_tests target script_lib foundation_lib asset_lib suffix label)
  add_executable(${target} ${SCRIPT_TEST_SOURCES})
  target_link_libraries(${target} PRIVATE ${script_lib} ${asset_lib} ${foundation_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tools/asset_mint)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_script_tests(script_tests script foundation asset "" "script")
if(ENGINE_HAVE_ASAN)
  engine_add_script_tests(script_tests_asan script_asan foundation_asan asset_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_script_tests(script_tests_tsan script_tsan foundation_tsan asset_tsan " [tsan]" "tsan")
endif()
