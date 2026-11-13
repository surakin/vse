# crash_extract: a dev tool that pulls the minidump out of a crash envelope (see main.cpp). Never linked into the engine.
# The parser (envelope.cpp) is also compiled into the Phase 7 test executables, per sanitizer flavor.
# minidump.cpp reads a dump's modules and a Windows image's build id; resolve.cpp names a report's frames from a symbol store, with
# DbgHelp on Windows (windows/resolve.cpp, the tool's per-platform directory; Phase 13 Day 156).
add_executable(crash_extract main.cpp envelope.cpp minidump.cpp resolve.cpp)
target_compile_options(crash_extract PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
# File access goes through the engine's file layer (io::File), like every other tool; the parsers themselves stay free of it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_sources(crash_extract PRIVATE windows/resolve.cpp)
  target_link_libraries(crash_extract PRIVATE foundation_io dbghelp) # the file layer is all it needs (the settings layer arrives on Day 157)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_link_libraries(crash_extract PRIVATE foundation_io) # the file layer is all it needs (Phase 21; the aggregate arrives on Day 293)
else()
  target_link_libraries(crash_extract PRIVATE foundation)
endif()

# The tool on committed fixtures: a good envelope, one with no minidump, a truncated one, a missing file.
add_test(NAME crash_extract_tool
  COMMAND ${CMAKE_COMMAND}
    -DTOOL=$<TARGET_FILE:crash_extract>
    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    -DWORK=${ENGINE_TEST_TMP_DIR}/crash_extract
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tool_test.cmake)
set_tests_properties(crash_extract_tool PROPERTIES LABELS debug)
engine_label_tests(crash_extract_tool)
