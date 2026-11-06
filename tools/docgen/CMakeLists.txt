# Phase 3: docgen, our own documentation tool (tools/docgen/DESIGN.md).
# A dev-time tool built on the foundation; never linked into the engine or
# a game. Built with the production flags like the rest of our own code.
set(DOCGEN_SOURCES
  src/scanner.cpp
  src/extractor.cpp
  src/doc_parser.cpp
  src/module_map.cpp
  src/symbol_index.cpp
  src/markdown.cpp
  src/reference.cpp
  src/md_scan.cpp
  src/links.cpp
  src/samples.cpp
  src/lint.cpp
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

# Phase 3 Day 43: the documentation checks. `cmake --build build --target
# docs` regenerates docs/reference and then runs every check; ctest runs the
# four checks (no regeneration) as docs_check, docs_links, docs_samples and
# docs_lint. Samples are compiled with the engine's own production flags,
# include roots and libraries.
set(DOCGEN_FLAGS_ARGS --flag -std=c++20)
foreach(opt ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  list(APPEND DOCGEN_FLAGS_ARGS --flag ${opt})
endforeach()
set(DOCGEN_LINK_ARGS --link $<TARGET_FILE:renderer_passes> --link $<TARGET_FILE:renderer> --link $<TARGET_FILE:rhi_vulkan> --link $<TARGET_FILE:rhi> --link $<TARGET_FILE:jobs> --link $<TARGET_FILE:foundation> --link $<TARGET_FILE:backtrace> --link $<TARGET_FILE:sentry> --link $<TARGET_FILE:breakpad_client> --link $<TARGET_FILE:unwind> --link -pthread --link -ldl)
# The system libraries `foundation` links (see engine/foundation/CMakeLists.txt), asked of pkg-config
# here instead of borrowed from variables set in another directory's scope.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DocsSampleLibs REQUIRED wayland-client xkbcommon x11)
foreach(flag ${DocsSampleLibs_LDFLAGS})
  list(APPEND DOCGEN_LINK_ARGS --link ${flag})
endforeach()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs-samples)
set(DOCGEN_SAMPLES_ARGS
  --root ${CMAKE_SOURCE_DIR}
  --compiler ${CMAKE_CXX_COMPILER}
  ${DOCGEN_FLAGS_ARGS}
  -I ${CMAKE_SOURCE_DIR}/engine/foundation
  -I ${CMAKE_SOURCE_DIR}/engine
  -I ${CMAKE_SOURCE_DIR}/shaders
  --prelude ${CMAKE_SOURCE_DIR}/docs/samples-prelude.h
  --build-dir ${CMAKE_BINARY_DIR}/docs-samples
  --jobs 4
  ${DOCGEN_LINK_ARGS})
set(DOCGEN_LINT_ARGS --root ${CMAKE_SOURCE_DIR} --baseline ${CMAKE_SOURCE_DIR}/docs/reference/lint-baseline.txt)

add_test(NAME docs_check COMMAND docgen check --root ${CMAKE_SOURCE_DIR})
add_test(NAME docs_links COMMAND docgen links --root ${CMAKE_SOURCE_DIR})
add_test(NAME docs_samples COMMAND docgen samples ${DOCGEN_SAMPLES_ARGS})
add_test(NAME docs_lint COMMAND docgen lint ${DOCGEN_LINT_ARGS} --quiet)
set_tests_properties(docs_check docs_links docs_samples docs_lint PROPERTIES LABELS "docs" TIMEOUT 300)
engine_label_tests(docs_check docs_links docs_samples docs_lint)

add_custom_target(docs
  COMMAND docgen reference --root ${CMAKE_SOURCE_DIR}
  COMMAND docgen check --root ${CMAKE_SOURCE_DIR}
  COMMAND docgen links --root ${CMAKE_SOURCE_DIR}
  COMMAND docgen samples ${DOCGEN_SAMPLES_ARGS}
  COMMAND docgen lint ${DOCGEN_LINT_ARGS}
  DEPENDS docgen renderer_passes renderer rhi_vulkan rhi jobs foundation backtrace sentry breakpad_client unwind
  COMMENT "Regenerating docs/reference and running the documentation checks"
  USES_TERMINAL)
