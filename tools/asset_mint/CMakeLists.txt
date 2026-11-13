# asset_mint: Phase 22 Day 316, the asset minter's first cut (the `pack` command; Day 332 completes the tool). An offline tool: never
# linked into the engine, a game or an engine test. Its tests drive the library (tests/asset).
add_executable(asset_mint main.cpp)
target_compile_options(asset_mint PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(asset_mint PRIVATE asset foundation)
