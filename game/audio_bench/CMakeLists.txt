# Phase 26 Day 442: the audio benchmark. Not run by ctest (tests/audio/perf_smoke_test.cpp runs a small version with a generous bound); the numbers
# are recorded in plans/phase-26-audio/day-442-benchmarks-stress-and-limits.md and docs/guide/audio.md ("Cost and limits").
add_executable(audio_bench main.cpp)
engine_split_debug_info(audio_bench)
target_compile_options(audio_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(audio_bench PRIVATE audio jobs foundation)

# The Release-only stress run: two million blocks of a looping scene with random parameter changes, checked as it goes (ten million with --long-blocks: about 28 minutes).
if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_test(NAME audio_bench_large COMMAND audio_bench --scene large --long-blocks 2000000)
  set_tests_properties(audio_bench_large PROPERTIES TIMEOUT 900)
  engine_label_tests(audio_bench_large)
endif()
