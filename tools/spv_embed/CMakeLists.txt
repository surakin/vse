# Dev-time tool that embeds a compiled SPIR-V binary as a C++ include file (Phase 3 Day 58). Never linked into
# the engine or a game; the files it generates are committed.
add_executable(spv_embed main.cpp)
target_compile_options(spv_embed PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(spv_embed PRIVATE foundation)
