# Dev-time tool (Phase 5 Day 71, folds in Phase 4's spv_embed): turns a compiled SPIR-V binary plus slangc's
# -reflection-json output into the committed shaders/<name>.spv.inc and shaders/<name>.refl.inc, and from Phase 13 Day 163
# the DXIL beside them into shaders/<name>.dxil.inc. Never linked into the engine or a game. It is the one place third-party
# JSON code is used (nlohmann/json, PLAN.md assumption #19).
#
# Built with exceptions on (ENGINE_EXCEPTIONS_COMPILE_OPTIONS), not the engine's production options: nlohmann/json is written
# to throw, and cl rejects that under the engine's /EHs-c- (Phase 13 Day 163; the tool parses with allow_exceptions=false, so
# nothing is thrown in practice).
set(SHADER_MINT_SOURCES reflect_json.cpp permutations.cpp run_process.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND SHADER_MINT_SOURCES windows/run_process_windows.cpp)
else()
  list(APPEND SHADER_MINT_SOURCES run_process_posix.cpp)
endif()
add_library(shader_mint_lib STATIC ${SHADER_MINT_SOURCES})
target_compile_options(shader_mint_lib PRIVATE ${ENGINE_EXCEPTIONS_COMPILE_OPTIONS})
target_include_directories(shader_mint_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(shader_mint_lib SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json)
target_link_libraries(shader_mint_lib PUBLIC renderer)

add_executable(shader_mint main.cpp)
target_compile_options(shader_mint PRIVATE ${ENGINE_EXCEPTIONS_COMPILE_OPTIONS})
target_link_libraries(shader_mint PRIVATE shader_mint_lib)
