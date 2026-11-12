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
# The sample runner's child processes (src/child_process.h): posix_spawn, or a job object per child on Windows (Day 161).
if(WIN32)
  list(APPEND DOCGEN_SOURCES src/windows/child_process_windows.cpp)
else()
  list(APPEND DOCGEN_SOURCES src/child_process_posix.cpp)
endif()

add_library(docgen_lib STATIC ${DOCGEN_SOURCES})
target_include_directories(docgen_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(docgen_lib PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(docgen_lib PUBLIC foundation)

# ASan/UBSan flavor, linked against the matching foundation: the scanner
# reads arbitrary bytes, so out-of-bounds reads are the main risk.
if(ENGINE_HAVE_ASAN) # (not clang-cl on Windows, whose configuration has no ASan flavor)
  add_library(docgen_lib_asan STATIC ${DOCGEN_SOURCES})
  target_include_directories(docgen_lib_asan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(docgen_lib_asan PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
  target_link_libraries(docgen_lib_asan PUBLIC foundation_asan)
endif()

add_executable(docgen src/main.cpp)
target_compile_options(docgen PRIVATE ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
target_link_libraries(docgen PRIVATE docgen_lib)

# macOS (Phase 26 Day 309): the reference and the three checks that need only docgen. The samples link the renderer, UI, text and the rest,
# which macOS builds from Day 310 on; docs_samples and the samples step of `docs` join on Day 326.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(DOCGEN_LINT_ARGS --root ${CMAKE_SOURCE_DIR} --baseline ${CMAKE_SOURCE_DIR}/docs/reference/lint-baseline.txt)
  add_test(NAME docs_check COMMAND docgen check --root ${CMAKE_SOURCE_DIR})
  add_test(NAME docs_links COMMAND docgen links --root ${CMAKE_SOURCE_DIR})
  add_test(NAME docs_lint COMMAND docgen lint ${DOCGEN_LINT_ARGS} --quiet)
  set_tests_properties(docs_check docs_links docs_lint PROPERTIES LABELS "docs" TIMEOUT 300)
  engine_label_tests(docs_check docs_links docs_lint)
  add_custom_target(docs
    COMMAND $<TARGET_FILE:docgen> reference --root ${CMAKE_SOURCE_DIR}
    COMMAND $<TARGET_FILE:docgen> check --root ${CMAKE_SOURCE_DIR}
    COMMAND $<TARGET_FILE:docgen> links --root ${CMAKE_SOURCE_DIR}
    COMMAND $<TARGET_FILE:docgen> lint ${DOCGEN_LINT_ARGS}
    DEPENDS docgen
    COMMENT "Regenerating docs/reference and running the documentation checks (macOS: no samples before Phase 26 Day 326)"
    VERBATIM
    USES_TERMINAL)
  return()
endif()

# Phase 3 Day 43: the documentation checks. `cmake --build build --target docs` regenerates docs/reference and then runs every check; ctest
# runs the four checks (no regeneration) as docs_check, docs_links, docs_samples and docs_lint. Samples are compiled with the engine's own
# production flags, include roots and libraries.
#
# Windows (Phase 13; the samples from Day 161): a sample is compiled by the build's own cl or clang-cl with the engine's Windows options and
# definitions, the runtime library the engine's libraries use, the toolchain's system headers and libraries by flag (the build runs without
# a Developer Command Prompt, so nothing comes from the environment), and the libraries by path. The option lists are the build's own
# (cmake/EngineCompilerOptions.cmake), with their per-language generator expressions resolved here, where no language applies.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(DOCGEN_FLAGS_ARGS --flag /nologo --flag /std:c++20)
  foreach(opt IN LISTS ENGINE_WINDOWS_COMPILE_OPTIONS ENGINE_WARNING_COMPILE_OPTIONS ENGINE_PRODUCTION_COMPILE_OPTIONS)
    if(opt MATCHES "^\\$<\\$<COMPILE_LANGUAGE:C,CXX>:(.*)>$")
      set(opt "${CMAKE_MATCH_1}")
    elseif(opt MATCHES "^\\$<\\$<COMPILE_LANG_AND_ID:CXX,MSVC>:(.*)>$")
      if(ENGINE_CLANG_CL)
        continue()
      endif()
      set(opt "${CMAKE_MATCH_1}")
    endif()
    list(APPEND DOCGEN_FLAGS_ARGS --flag ${opt})
  endforeach()
  foreach(def IN LISTS ENGINE_WINDOWS_DEFINITIONS)
    list(APPEND DOCGEN_FLAGS_ARGS --flag /D${def})
  endforeach()
  list(APPEND DOCGEN_FLAGS_ARGS --flag $<IF:$<CONFIG:Debug>,/MDd,/MD>) # the engine's libraries' runtime (CMake's default per configuration)
  if(ENGINE_CLANG_CL)
    set(_system_include /imsvc)
  else()
    set(_system_include /external:I)
    list(APPEND DOCGEN_FLAGS_ARGS --flag /external:W0)
  endif()
  foreach(dir IN LISTS ENGINE_WINDOWS_INCLUDE_DIRS)
    list(APPEND DOCGEN_FLAGS_ARGS --flag "${_system_include}${dir}")
  endforeach()
  set(DOCGEN_LINK_ARGS "")
  foreach(lib renderer_passes renderer ui net storage database_sqlite database sqlite3 spatial ecs text freetype rhi_backends rhi_dx12 rhi_vulkan rhi jobs platform foundation_settings foundation_debug foundation_io foundation_base sentry breakpad_client)
    list(APPEND DOCGEN_LINK_ARGS --link $<TARGET_LINKER_FILE:${lib}>)
  endforeach()
  # The system libraries those link beyond CMake's standard ones (engine/foundation, engine/platform and sentry's CMakeLists.txt), then
  # the standard ones every program of the build gets (CMAKE_CXX_STANDARD_LIBRARIES: kernel32, user32, gdi32, ...).
  separate_arguments(_standard_libraries WINDOWS_COMMAND "${CMAKE_CXX_STANDARD_LIBRARIES}")
  foreach(lib shlwapi.lib version.lib dbghelp.lib psapi.lib synchronization.lib bcrypt.lib wtsapi32.lib d3d12.lib dxgi.lib dxguid.lib ${_standard_libraries})
    list(APPEND DOCGEN_LINK_ARGS --link ${lib})
  endforeach()
  if(ENGINE_CLANG_CL)
    list(APPEND DOCGEN_LINK_ARGS --link ${ENGINE_CLANG_BUILTINS})
  endif()
  list(APPEND DOCGEN_LINK_ARGS --link /link)
  foreach(dir IN LISTS ENGINE_WINDOWS_LIB_DIRS)
    list(APPEND DOCGEN_LINK_ARGS --link "/LIBPATH:${dir}")
  endforeach()
  set(DOCGEN_EXTRA_INCLUDES -I ${ENGINE_VULKAN_HEADERS_DIR})
  set(DOCGEN_SAMPLE_LIBRARIES platform breakpad_client rhi_dx12)
else()
  set(DOCGEN_FLAGS_ARGS --flag -std=c++20)
  foreach(opt ${ENGINE_PRODUCTION_COMPILE_OPTIONS})
    list(APPEND DOCGEN_FLAGS_ARGS --flag ${opt})
  endforeach()
  set(DOCGEN_LINK_ARGS --link $<TARGET_FILE:renderer_passes> --link $<TARGET_FILE:renderer> --link $<TARGET_FILE:ui> --link $<TARGET_FILE:net> --link $<TARGET_FILE:storage> --link $<TARGET_FILE:database_sqlite> --link $<TARGET_FILE:database> --link $<TARGET_FILE:sqlite3> --link $<TARGET_FILE:spatial> --link $<TARGET_FILE:ecs> --link $<TARGET_FILE:text> --link $<TARGET_FILE:freetype> --link $<TARGET_FILE:rhi_backends> --link $<TARGET_FILE:rhi_vulkan> --link $<TARGET_FILE:rhi> --link $<TARGET_FILE:jobs> --link $<TARGET_FILE:platform> --link $<TARGET_FILE:foundation_settings> --link $<TARGET_FILE:foundation_debug> --link $<TARGET_FILE:foundation_io> --link $<TARGET_FILE:foundation_base> --link $<TARGET_FILE:backtrace> --link $<TARGET_FILE:sentry> --link $<TARGET_FILE:breakpad_client> --link $<TARGET_FILE:unwind> --link -pthread --link -ldl)
  # The system libraries `platform` links (see engine/platform/CMakeLists.txt), asked of pkg-config
  # here instead of borrowed from variables set in another directory's scope.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DocsSampleLibs REQUIRED wayland-client xkbcommon x11)
  foreach(flag ${DocsSampleLibs_LDFLAGS})
    list(APPEND DOCGEN_LINK_ARGS --link ${flag})
  endforeach()
  set(DOCGEN_EXTRA_INCLUDES "")
  set(DOCGEN_SAMPLE_LIBRARIES backtrace breakpad_client unwind)
endif()
# The guide's text samples open the shipped font; samples run in a scratch directory, so they get its absolute path as a macro.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(DOCGEN_FONT_FLAG "/DDOCS_FONT_FILE=\"${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf\"")
else()
  set(DOCGEN_FONT_FLAG "-DDOCS_FONT_FILE=\"${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf\"")
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs-samples)
set(DOCGEN_SAMPLES_ARGS
  --root ${CMAKE_SOURCE_DIR}
  --compiler ${CMAKE_CXX_COMPILER}
  ${DOCGEN_FLAGS_ARGS}
  --flag ${DOCGEN_FONT_FLAG}
  -I ${CMAKE_SOURCE_DIR}/engine/foundation
  -I ${CMAKE_SOURCE_DIR}/engine
  -I ${CMAKE_SOURCE_DIR}/shaders
  ${DOCGEN_EXTRA_INCLUDES}
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
  COMMAND $<TARGET_FILE:docgen> reference --root ${CMAKE_SOURCE_DIR}
  COMMAND $<TARGET_FILE:docgen> check --root ${CMAKE_SOURCE_DIR}
  COMMAND $<TARGET_FILE:docgen> links --root ${CMAKE_SOURCE_DIR}
  COMMAND $<TARGET_FILE:docgen> samples ${DOCGEN_SAMPLES_ARGS}
  COMMAND $<TARGET_FILE:docgen> lint ${DOCGEN_LINT_ARGS}
  DEPENDS docgen renderer_passes renderer net storage database_sqlite database sqlite3 spatial ecs text freetype rhi_backends rhi_vulkan rhi jobs foundation sentry ${DOCGEN_SAMPLE_LIBRARIES}
  COMMENT "Regenerating docs/reference and running the documentation checks"
  VERBATIM
  USES_TERMINAL)
