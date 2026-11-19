# Phase 27 Day 476: the script benchmark. Run by hand in a Release build (docs/guide/scripting.md, "Cost"); the numbers are recorded in
# plans/phase-27-scripting/day-476-benchmarks-and-limits.md. `script_bench --scene check` is the regression check ctest runs in an
# optimized build (generous bounds, scripts/check.cmake is not needed: the program exits 1 when a bound is broken).
if(NOT TARGET luajit)
  return()  # LuaJIT builds on Linux and Windows x86-64 so far (macOS: Day 474)
endif()
add_executable(script_bench main.cpp)
engine_split_debug_info(script_bench)
target_compile_options(script_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(script_bench PRIVATE script storage ecs jobs foundation)

# Phase 27 Day 476: the regression check (a 3x slowdown fails it) and the large stress run, registered only for an optimized build
# (a debug or sanitizer build is 8 to 15 times slower; the bounds do not fit it).
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_test(NAME script_bench_check COMMAND script_bench --scene check)
  set_tests_properties(script_bench_check PROPERTIES TIMEOUT 600)
  engine_label_tests(script_bench_check)
  add_test(NAME script_stress_large COMMAND script_bench --scene stress)
  set_tests_properties(script_stress_large PROPERTIES TIMEOUT 3000)
  engine_label_tests(script_stress_large)
  engine_soak_tests(script_stress_large)  # a million scripted entities: only with --all or --soak
endif()
