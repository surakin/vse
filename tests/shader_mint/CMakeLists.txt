# Tests of the offline shader_mint tool's reflection conversion (Phase 5 Day 71) and, from Phase 13 Day 163, its Direct3D 12 rules
# and the process helper. This is the one test target that links the tool's code, and with it the vendored JSON library (PLAN.md
# assumption #19); like the other test directories it does not apply ENGINE_PRODUCTION_COMPILE_OPTIONS because Catch2's REQUIRE/CHECK
# are exception-based.
add_executable(shader_mint_args_probe args_probe.cpp)
target_link_libraries(shader_mint_args_probe PRIVATE foundation)

add_executable(shader_mint_tests reflect_json_test.cpp permutations_test.cpp d3d12_conventions_test.cpp)
target_link_libraries(shader_mint_tests PRIVATE shader_mint_lib Catch2::Catch2WithMain)
target_include_directories(shader_mint_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(shader_mint_tests PRIVATE SHADER_MINT_ARGS_PROBE="$<TARGET_FILE:shader_mint_args_probe>"
                                                     SHADER_MINT_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(shader_mint_tests shader_mint_args_probe)
include(Catch)
engine_discover_tests(shader_mint_tests PROPERTIES LABELS "shaders")
