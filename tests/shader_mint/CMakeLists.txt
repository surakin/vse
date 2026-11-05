# Tests of the offline shader_mint tool's reflection conversion (Phase 4 Day 71). This is the one test target that links
# the tool's code, and with it the vendored JSON library (PLAN.md assumption #19); like the other test directories it
# does not apply ENGINE_PRODUCTION_COMPILE_OPTIONS because Catch2's REQUIRE/CHECK are exception-based.
add_executable(shader_mint_tests reflect_json_test.cpp permutations_test.cpp)
target_link_libraries(shader_mint_tests PRIVATE shader_mint_lib Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(shader_mint_tests PROPERTIES LABELS "shaders")
