# The render features benchmark (Phase 28 Day 504). Run by hand in a Release build (docs/guide/instancing.md and terrain.md, "Cost").
# `render_features_bench --scene check` is the regression check ctest runs in an optimized build (generous bounds: a 3x slowdown
# fails it), registered only there (a debug or sanitizer build is 8 to 15 times slower; the bounds do not fit it).
add_executable(render_features_bench main.cpp ../common/settings_main.cpp)
engine_split_debug_info(render_features_bench)
target_compile_options(render_features_bench PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(render_features_bench PRIVATE ${CMAKE_SOURCE_DIR}/game/common)
target_link_libraries(render_features_bench PRIVATE render_features_lib platform rhi_backends)

if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  add_test(NAME render_features_bench_check COMMAND render_features_bench --scene check)
  set_tests_properties(render_features_bench_check PROPERTIES TIMEOUT 600 PASS_REGULAR_EXPRESSION "render_features_bench: check ok")
  engine_label_tests(render_features_bench_check)
endif()
