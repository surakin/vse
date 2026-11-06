# crash_extract: a dev tool that pulls the minidump out of a crash envelope (see main.cpp). Never linked into the engine.
# The parser (envelope.cpp) is also compiled into the Phase 6 test executables, per sanitizer flavor.
add_executable(crash_extract main.cpp envelope.cpp)
target_compile_options(crash_extract PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
# File access goes through the engine's file layer (io::File), like every other tool; the envelope parser itself stays free of it.
target_link_libraries(crash_extract PRIVATE foundation)

# The tool on committed fixtures: a good envelope, one with no minidump, a truncated one, a missing file.
add_test(NAME crash_extract_tool
  COMMAND ${CMAKE_COMMAND}
    -DTOOL=$<TARGET_FILE:crash_extract>
    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tool_test.cmake)
set_tests_properties(crash_extract_tool PROPERTIES LABELS debug)
engine_label_tests(crash_extract_tool)
