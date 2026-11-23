# The render features showcase: a terrain with a sphere and a box dropped onto its height-field collider. Headless it prints the hash of the
# physics state (the same at every thread count); the picture mode (sky, terrain, instances in a window or offscreen) is completed by the
# last day of the phase (KNOWN-ISSUES.md).
add_executable(render_features_demo main.cpp)
engine_split_debug_info(render_features_demo)
target_compile_options(render_features_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(render_features_demo PRIVATE physics jobs foundation)

# The hash of the terrain scene after 300 steps, at 1, 2, 4 and 16 threads: one value (the determinism rule).
foreach(threads 1 2 4 16)
  add_test(NAME render_features_demo_hash_${threads} COMMAND render_features_demo --headless --steps 300 --threads ${threads})
  set_tests_properties(render_features_demo_hash_${threads} PROPERTIES LABELS "demo"
    PASS_REGULAR_EXPRESSION "state hash 0x1e1cb53ed947ac04 ok")
  engine_label_tests(render_features_demo_hash_${threads})
endforeach()
