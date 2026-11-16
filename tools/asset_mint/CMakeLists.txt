# asset_mint: Phase 22 Day 316, the asset minter's first cut (the `pack` command; Day 333 adds the project commands, Day 334 the rest). An offline tool: never
# linked into the engine, a game or an engine test. Its tests drive the library (tests/asset).
add_executable(asset_mint main.cpp project.cpp target_transform.cpp upgrade.cpp skeleton_source.cpp hull_source.cpp mcol_source.cpp mesh_source.cpp clip_source.cpp texture_source.cpp level_source.cpp)  # skeleton_source: Day 318 (the asset tests compile it too)
target_compile_options(asset_mint PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(asset_mint PRIVATE asset_import animation asset renderer_passes renderer foundation)  # animation: its components in a level, Day 327

# Day 333: the tool's own tests (ctest scripts over a copy of tests/asset/mint_sample).
foreach(mode mint verify incremental bad_source edit target gltf)
  add_test(NAME asset_mint_${mode} COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:asset_mint> -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/asset/mint_sample
           -DWORK=${CMAKE_CURRENT_BINARY_DIR}/test_${mode} -DMODE=${mode} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_mint.cmake)
  set_tests_properties(asset_mint_${mode} PROPERTIES LABELS "asset")
  engine_label_tests(asset_mint_${mode})
endforeach()
