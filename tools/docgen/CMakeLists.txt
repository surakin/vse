# Phase 2.5: docgen, our own documentation tool (tools/docgen/DESIGN.md).
# A dev-time tool built on the foundation; never linked into the engine or
# a game. Built with the production flags like the rest of our own code.
set(DOCGEN_SOURCES
  src/scanner.cpp
  src/source_file.cpp
)

add_library(docgen_lib STATIC ${DOCGEN_SOURCES})
target_include_directories(docgen_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(docgen_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(docgen_lib PUBLIC foundation)

# ASan/UBSan flavor, linked against the matching foundation: the scanner
# reads arbitrary bytes, so out-of-bounds reads are the main risk.
add_library(docgen_lib_asan STATIC ${DOCGEN_SOURCES})
target_include_directories(docgen_lib_asan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(docgen_lib_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(docgen_lib_asan PUBLIC foundation_asan)

add_executable(docgen src/main.cpp)
target_compile_options(docgen PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(docgen PRIVATE docgen_lib)
