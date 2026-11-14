# resource_server: Phase 22 Day 331. Serves a directory of loose blobs or a pak file over TCP (asset/resource_server.h); a tool,
# never linked into a game or an engine test. Its tests drive the library (tests/asset/resource_server_test.cpp) and the usage text.
add_executable(resource_server main.cpp)
target_compile_options(resource_server PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(resource_server PRIVATE asset net jobs foundation)
add_test(NAME resource_server_usage COMMAND resource_server)
set_tests_properties(resource_server_usage PROPERTIES WILL_FAIL FALSE PASS_REGULAR_EXPRESSION "usage: resource_server" LABELS "asset")
engine_label_tests(resource_server_usage)
