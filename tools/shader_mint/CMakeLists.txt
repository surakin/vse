# Dev-time tool (Phase 4 Day 71, folds in Phase 3's spv_embed): turns a compiled SPIR-V binary plus slangc's
# -reflection-json output into the committed shaders/<name>.spv.inc and shaders/<name>.refl.inc. Never linked into the
# engine or a game. It is the one place third-party JSON code is used (nlohmann/json, PLAN.md assumption #19).
add_library(shader_mint_lib STATIC reflect_json.cpp)
target_compile_options(shader_mint_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_include_directories(shader_mint_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(shader_mint_lib SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)
target_link_libraries(shader_mint_lib PUBLIC renderer)

add_executable(shader_mint main.cpp)
target_compile_options(shader_mint PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(shader_mint PRIVATE shader_mint_lib)
