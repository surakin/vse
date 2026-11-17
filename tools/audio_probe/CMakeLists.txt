# audio_probe: Phase 26 Day 425, a dev tool for listening to the real audio backend by hand (see main.cpp). Never linked into the engine.
add_executable(audio_probe main.cpp)
target_compile_options(audio_probe PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(audio_probe PRIVATE audio foundation)
add_test(NAME audio_probe_list COMMAND audio_probe list --backend=null)
set_tests_properties(audio_probe_list PROPERTIES PASS_REGULAR_EXPRESSION "null" LABELS "audio")
engine_label_tests(audio_probe_list)
