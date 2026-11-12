# Phase 20 Day 281: the networking demo, a headless console program: the same scripted conversation over the simulated, UDP and TCP
# transports, on real loopback sockets or (--simulated) on the simulated network.
add_executable(net_demo main.cpp)
engine_split_debug_info(net_demo)
target_compile_options(net_demo PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(net_demo PRIVATE net jobs foundation)
add_test(NAME net_demo_runs COMMAND net_demo)
add_test(NAME net_demo_simulated_runs COMMAND net_demo --simulated)
set_tests_properties(net_demo_runs net_demo_simulated_runs PROPERTIES LABELS "demo" PASS_REGULAR_EXPRESSION "net_demo: ok" ENVIRONMENT "${DEMO_PROFILE_ENV}")
set_tests_properties(net_demo_runs PROPERTIES LABELS "demo;network")
engine_label_tests(net_demo_runs net_demo_simulated_runs)
