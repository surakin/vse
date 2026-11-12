# net_probe: Phase 20 Day 281, a dev tool for checking the transports by hand over real sockets (see main.cpp). Never linked into
# the engine. Its test starts an echo server and pings and sends a file through it (probe_test.cmake).
add_executable(net_probe main.cpp)
target_compile_options(net_probe PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(net_probe PRIVATE net jobs foundation)
add_test(NAME net_probe_resolve COMMAND net_probe resolve [::1]:80)
set_tests_properties(net_probe_resolve PROPERTIES PASS_REGULAR_EXPRESSION "\\[::1\\]:80" LABELS "network")
engine_label_tests(net_probe_resolve)
