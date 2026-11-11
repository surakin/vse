# Phase 19 Day 264: the storage demo, a headless console program: a city of entities with the spatial sets is saved to a SQLite file named by the settings, loaded
# into a fresh world and compared, then saved by the autosaver while it runs and loaded again.
add_executable(storage_demo main.cpp)
engine_split_debug_info(storage_demo)
target_compile_options(storage_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(storage_demo PRIVATE storage database_sqlite database spatial ecs jobs foundation)
add_test(NAME storage_demo_runs COMMAND storage_demo --entities 3000 --frames 60)
add_test(NAME storage_demo_columns_runs COMMAND storage_demo --entities 3000 --frames 60 --columns)
set_tests_properties(storage_demo_runs storage_demo_columns_runs PROPERTIES LABELS "demo" PASS_REGULAR_EXPRESSION "storage_demo: ok" ENVIRONMENT "${DEMO_PROFILE_ENV}")
engine_label_tests(storage_demo_runs storage_demo_columns_runs)
