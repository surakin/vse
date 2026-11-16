# The physics tests (Phase 25): the determinism foundation (Day 374) and, from Day 375, the module. Like tests/spatial, they do not apply
# ENGINE_PRODUCTION_COMPILE_OPTIONS (Catch2's macros use exceptions); the library under test is built without. One executable per sanitizer flavor.
set(PHYSICS_TEST_SOURCES
  state_hash_test.cpp  # Day 374
  determinism_test.cpp  # Day 374
  shapes_test.cpp  # Day 375
  mass_test.cpp  # Day 375
  world_test.cpp  # Day 376
  sync_test.cpp  # Day 376
  broad_phase_test.cpp  # Day 377
  step_test.cpp  # Day 378
  collide_test.cpp  # Day 379
  hull_collide_test.cpp  # Day 381
  mesh_collide_test.cpp  # Day 382
  compound_test.cpp  # Day 383
  solver_test.cpp  # Day 384
  stability_test.cpp  # Day 385
  joint_test.cpp  # Day 386
  hinge_test.cpp  # Day 387
  sleep_test.cpp  # Day 388
  parallel_test.cpp  # Day 389
  ccd_test.cpp  # Day 390
  query_test.cpp  # Day 391
  character_test.cpp  # Day 392
  events_test.cpp  # Day 393
  snapshot_test.cpp  # Day 394
  golden_test.cpp  # Day 395
  stress_test.cpp  # Day 400
  perf_smoke_test.cpp  # Day 400
)

find_package(Threads REQUIRED)
include(Catch)

# The control probe: the same expression with contraction allowed (and FMA instructions on x86-64), so the test can show that fusing would be seen.
set(_physics_control_options "")
if(NOT ENGINE_MSVC_LIKE)
  set(_physics_control_options -O2 -ffp-contract=fast)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    list(APPEND _physics_control_options -mfma)
  endif()
endif()
add_library(physics_fp_probe_project OBJECT fp_contract_probe.cpp)
target_compile_definitions(physics_fp_probe_project PRIVATE FP_PROBE_NAME=ProbeProject)
target_include_directories(physics_fp_probe_project PRIVATE ${CMAKE_SOURCE_DIR}/engine/foundation ${CMAKE_CURRENT_SOURCE_DIR})
add_library(physics_fp_probe_control OBJECT fp_contract_probe.cpp)
target_compile_definitions(physics_fp_probe_control PRIVATE FP_PROBE_NAME=ProbeControl)
target_include_directories(physics_fp_probe_control PRIVATE ${CMAKE_SOURCE_DIR}/engine/foundation ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(physics_fp_probe_control PRIVATE ${_physics_control_options})

function(engine_add_physics_tests target physics_lib suffix label)
  add_executable(${target} ${PHYSICS_TEST_SOURCES} ../ecs/alloc_counter.cpp $<TARGET_OBJECTS:physics_fp_probe_project> $<TARGET_OBJECTS:physics_fp_probe_control>)
  target_link_libraries(${target} PRIVATE ${physics_lib} Catch2::Catch2WithMain Threads::Threads)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../ecs)
  if(label STREQUAL "asan")
    engine_asan_runtime(${target})
  endif()
  engine_discover_tests(${target} TEST_SUFFIX "${suffix}" PROPERTIES LABELS "${label}")
endfunction()

engine_add_physics_tests(physics_tests physics "" "physics")
if(ENGINE_HAVE_ASAN)
  engine_add_physics_tests(physics_tests_asan physics_asan " [asan]" "asan")
endif()
if(ENGINE_HAVE_TSAN)  # not with MSVC
  engine_add_physics_tests(physics_tests_tsan physics_tsan " [tsan]" "tsan")
endif()

# The probe program: prints the hash and the mode; the committed hash is checked here and in determinism_test.cpp.
add_executable(fp_determinism_probe fp_determinism_probe.cpp)
target_link_libraries(fp_determinism_probe PRIVATE physics)
add_test(NAME physics_fp_determinism_probe COMMAND fp_determinism_probe)
set_tests_properties(physics_fp_determinism_probe PROPERTIES PASS_REGULAR_EXPRESSION "fp_determinism_probe: hash 50e1be9251ea408f contraction=off denormals=kept")
engine_label_tests(physics_fp_determinism_probe)

# The configure-time flag check on planted flags.
add_test(NAME physics_fp_flags_check COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/fp_flags_check.cmake)
engine_label_tests(physics_fp_flags_check)
