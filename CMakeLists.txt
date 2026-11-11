cmake_minimum_required(VERSION 3.21)  # 3.21: presets that name a toolchain file (CMakePresets.json)
# C is enabled alongside CXX only to compile the generated xdg-shell
# protocol bindings (platform/linux/wayland/generated/xdg-shell-protocol.c,
# wayland-scanner's own output format) — the engine itself is C++20
# throughout; see PLAN.md assumption #2.

# Linux: Clang is the compiler of choice, GCC the fallback. Chosen before project() when nothing else says which: not when CMAKE_CXX_COMPILER or
# CMAKE_C_COMPILER is given (-D or a toolchain file), nor when CXX or CC is set in the environment, nor in a build directory that already
# has a compiler (its cache holds it). Both must build clean under warnings as errors (CLAUDE.md).
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_TOOLCHAIN_FILE
   AND NOT CMAKE_CXX_COMPILER AND NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CXX} AND NOT DEFINED ENV{CC})
  find_program(ENGINE_CLANGXX_PROGRAM clang++)
  find_program(ENGINE_CLANG_PROGRAM clang)
  if(ENGINE_CLANGXX_PROGRAM AND ENGINE_CLANG_PROGRAM)
    set(CMAKE_CXX_COMPILER "${ENGINE_CLANGXX_PROGRAM}")
    set(CMAKE_C_COMPILER "${ENGINE_CLANG_PROGRAM}")
  endif()
endif()

# macOS (Phase 26 Day 284): Apple clang from the Command Line Tools is the only compiler (Phase 26 rules), found through xcrun so that a
# MacPorts or Homebrew clang first on PATH is never picked up by accident; same conditions as the Linux choice above. The deployment target
# is macOS 13.3: Apple's libc++ has floating-point std::to_chars from 13.3 (cstr/number.h), and Metal 3 needs 13.0
# (docs/superpowers/specs/2026-11-11-macos-port-design.md, decision 1). Both must be decided before project().
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_TOOLCHAIN_FILE
   AND NOT CMAKE_CXX_COMPILER AND NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CXX} AND NOT DEFINED ENV{CC})
  execute_process(COMMAND xcrun --find clang++ OUTPUT_VARIABLE _engine_xcrun_clangxx OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_VARIABLE _engine_xcrun_error RESULT_VARIABLE _engine_xcrun_result)
  execute_process(COMMAND xcrun --find clang OUTPUT_VARIABLE _engine_xcrun_clang OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT _engine_xcrun_result EQUAL 0 OR NOT EXISTS "${_engine_xcrun_clangxx}" OR NOT EXISTS "${_engine_xcrun_clang}")
    message(FATAL_ERROR "No Apple clang found through xcrun (${_engine_xcrun_error}). Install the Command Line Tools with "
                        "`xcode-select --install`; see BUILD.md (macOS).")
  endif()
  set(CMAKE_CXX_COMPILER "${_engine_xcrun_clangxx}")
  set(CMAKE_C_COMPILER "${_engine_xcrun_clang}")
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_SYSTEM_NAME)  # not for a cross build (iOS, Phase 27)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version (13.3: floating-point std::to_chars; Metal 3 needs 13.0)")
endif()

project(engine_placeholder CXX C)
if(APPLE)
  # Objective-C++ (.mm files with ARC) is how the macOS backends talk to AppKit, Metal and QuartzCore (Phase 26 decision 3).
  enable_language(OBJCXX)
endif()

# Supported targets: x86-64 Linux, x86-64 Windows (Phase 13), and x86-64 macOS (being ported in Phase 26, a side phase on the Mac; arm64 macOS gets a
# compile-only configuration on Phase 26 Day 310). Anything else names the phase that ports it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(ENGINE_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(ENGINE_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
  set(ENGINE_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS on ${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_OSX_ARCHITECTURES}): only x86-64 is ported so far; the arm64 compile-only "
                      "configuration is Phase 26 Day 310 (plans/phase-26-macos-metal.md)")
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Android|iOS)$")
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is ported in Phase 27 (plans/phase-27-android-ios.md)")
else()
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} is not a supported target (x86-64 Linux, x86-64 Windows; consoles: Phase 28)")
endif()

# Every executable gets a GNU build id (a hash of its contents) so a crash report and a minidump name the exact binary.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_link_options(-Wl,--build-id)
endif()

# Release symbols (Phase 7 Day 99): with this on, every build keeps debug information (-g) and the demos and benches have it split
# out after linking (scripts/split-debug.sh: <program>.debug beside a stripped program that keeps its build id and links to the
# debug file). Put the .debug files in a symbol store with scripts/store-symbols.sh to read crash dumps from stripped builds.
option(ENGINE_SPLIT_DEBUG_INFO "Build with -g and split the debug information out of the demos and benches (Linux)" OFF)
if(ENGINE_SPLIT_DEBUG_INFO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_compile_options(-g)
endif()
# engine_split_debug_info(<executable>): the per-program half of the option above.
function(engine_split_debug_info target)
  if(ENGINE_SPLIT_DEBUG_INFO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/split-debug.sh $<TARGET_FILE:${target}>
      COMMENT "Splitting the debug information of ${target}")
  endif()
endfunction()

# Whether programs install the crash handler by default (debug/program_setup.h): on in Debug and RelWithDebInfo, off in Release
# and MinSizeRel. ENGINE_CRASH_HANDLER=1/0 in the environment and --crash-handler=on|off on a command line override it.
add_compile_definitions($<IF:$<CONFIG:Release,MinSizeRel>,ENGINE_CRASH_HANDLER_DEFAULT=0,ENGINE_CRASH_HANDLER_DEFAULT=1>
  ENGINE_BUILD_CONFIG="$<LOWER_CASE:$<CONFIG>>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)  # macOS: the .mm files are C++20 as well
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Exceptions/RTTI/stack-protector are disabled for the engine's own production code (see CLAUDE.md's hard architectural
# constraints), through ENGINE_PRODUCTION_COMPILE_OPTIONS applied per target (engine/foundation/CMakeLists.txt), not build-wide,
# so vendored test tooling (Catch2, which needs exceptions for its REQUIRE/CHECK macros) and the test executables aren't affected.
# The options per compiler (GCC/Clang on Linux; cl and clang-cl on Windows) and the sanitizer flavors each compiler has are in:
include(cmake/EngineCompilerOptions.cmake)

# Debug checks (engine/foundation/log/assert.h): ENGINE_VERIFY and ENGINE_UNREACHABLE check, and ENGINE_CHECK_SLOW
# evaluates, only in builds without NDEBUG (the Debug configuration) unless forced on here. ENGINE_ASSERT and
# ENGINE_NOT_IMPLEMENTED are always on.
option(ENGINE_FORCE_DEBUG_CHECKS "Keep ENGINE_VERIFY and ENGINE_UNREACHABLE checks in every build configuration" OFF)
option(ENGINE_FORCE_SLOW_CHECKS "Keep ENGINE_CHECK_SLOW checks in every build configuration" OFF)
if(ENGINE_FORCE_DEBUG_CHECKS)
  add_compile_definitions(ENGINE_DEBUG_CHECKS=1)
endif()
if(ENGINE_FORCE_SLOW_CHECKS)
  add_compile_definitions(ENGINE_SLOW_CHECKS=1)
endif()

enable_testing()
# Where tests keep the files they make while they run (a private settings profile, pipeline caches, the command-line checks' folders, scratch
# projects): one folder of the build tree, safe to delete at any time.
set(ENGINE_TEST_TMP_DIR "${CMAKE_BINARY_DIR}/test-tmp")
file(MAKE_DIRECTORY "${ENGINE_TEST_TMP_DIR}/pipeline-cache" "${ENGINE_TEST_TMP_DIR}/screenshots")
add_subdirectory(third_party/catch2)
# macOS (Phase 26) adds these with the layers that use them (text on Day 294; storage when it is ported), not before: nothing configured
# there links them yet, and building them would only cost time on the Mac.
if(NOT ENGINE_PLATFORM STREQUAL "macos")
  add_subdirectory(third_party/freetype)  # Phase 14 Day 175: FreeType, vendored (PLAN.md assumption #17)
  add_subdirectory(third_party/sqlite)  # Phase 19 Day 251: SQLite, vendored (PLAN.md assumption #25), used only behind engine/database/sqlite/
endif()
# Catch2's REQUIRE/CHECK throw: exceptions on for Catch2 and, through PUBLIC, every test executable that links it.
if(ENGINE_EXCEPTIONS_COMPILE_OPTIONS)
  target_compile_options(Catch2 PUBLIC ${ENGINE_EXCEPTIONS_COMPILE_OPTIONS})
endif()

# Windows (Phase 13) builds one layer more each day, in dependency order (ARCHITECTURE.md): today the base layer of the
# foundation and the Windows-only tests. Each later day moves its directories from the skipped list into the build.
if(ENGINE_PLATFORM STREQUAL "windows")
  # sentry-native for the crash handler (Phase 13 Days 154-156), configured as on Linux (below) and added before the engine's own options,
  # so it keeps its own. libbacktrace is not built on Windows: stack traces resolve through DbgHelp (os/windows/debug/BACKEND.md).
  set(SENTRY_BACKEND breakpad CACHE STRING "" FORCE)
  set(SENTRY_TRANSPORT none CACHE STRING "" FORCE)
  set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  # The engine sets each thread's stack guarantee itself (RegisterThreadForCrashHandling, os/windows/debug/BACKEND.md); the library's own
  # setting at sentry_init would be a second, smaller one that only logs a warning when ours is already there.
  set(SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/sentry_native)
  # sentry-native builds itself with -Werror -Wall under clang-cl, where -Wall means -Weverything: a newer clang than the pinned release
  # was tested with (23 here) stops on warnings the release predates (-Wc++-keyword, -Wpadded in vendor/mpack.h, ...). Warnings off for
  # that vendored target only (its warnings are not ours to fix, and the sources stay unmodified).
  if(ENGINE_CLANG_CL)
    target_compile_options(sentry PRIVATE -Wno-error -w)
  endif()
  # The engine's own Windows options, for every target created from here on (the vendored libraries above keep their own).
  add_compile_options(${ENGINE_WINDOWS_COMPILE_OPTIONS} ${ENGINE_WARNING_COMPILE_OPTIONS})
  add_compile_definitions(${ENGINE_WINDOWS_DEFINITIONS})
  # The Vulkan C headers (os/gpu_driver_probe.h, the Vulkan backend) from the SDK the toolchain file found. Only vulkan/*.h and the
  # vk_video/ headers vulkan_core.h includes are copied into the build tree: the SDK's Include/ also holds slang, shaderc, dxc and
  # glslang, and exposing those would let engine code compile against a shader compiler (PLAN.md assumption #18).
  if(NOT ENGINE_VULKAN_SDK_DIR OR NOT EXISTS "${ENGINE_VULKAN_SDK_DIR}/Include/vulkan/vulkan_core.h")
    message(FATAL_ERROR "No Vulkan SDK found (ENGINE_VULKAN_SDK, VULKAN_SDK, C:/VulkanSDK/<version>): its headers are needed to build. See BUILD.md.")
  endif()
  set(ENGINE_VULKAN_HEADERS_DIR "${CMAKE_BINARY_DIR}/vulkan_headers")
  file(COPY "${ENGINE_VULKAN_SDK_DIR}/Include/vulkan" "${ENGINE_VULKAN_SDK_DIR}/Include/vk_video" DESTINATION "${ENGINE_VULKAN_HEADERS_DIR}"
       FILES_MATCHING PATTERN "*.h")
  include(cmake/EngineTestPhases.cmake)
  option(ENGINE_UNITY_BUILD "Compile each target's sources as merged unity translation units" ON)
  set(CMAKE_UNITY_BUILD ${ENGINE_UNITY_BUILD})
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
  add_subdirectory(engine/foundation)
  add_subdirectory(engine/platform)  # Day 158: the Win32 window and the windowed programs' entry point (input: Day 159)
  add_subdirectory(tests/foundation)  # foundation_base_tests, and from Day 158 the file-layer and window tests (foundation_tests)
  add_subdirectory(tests/types)
  add_subdirectory(tests/lang)  # Phase 16 Day 208
  add_subdirectory(tests/sync)  # Phase 16 Day 209
  add_subdirectory(tests/cstr)  # Phase 16 Day 211
  add_subdirectory(tests/foundation_sanitized)
  add_subdirectory(tests/windows)
  include(cmake/EngineDeathTests.cmake)
  add_subdirectory(engine/jobs)  # Day 153
  add_subdirectory(engine/text)  # Phase 14 Day 175
  add_subdirectory(tests/text)
  add_subdirectory(engine/ecs)  # Phase 17 Day 223
  add_subdirectory(tests/ecs)
  add_subdirectory(engine/spatial)  # Phase 18 Day 238
  add_subdirectory(tests/spatial)
  add_subdirectory(engine/database)  # Phase 19 Day 251
  add_subdirectory(tests/database)
  add_subdirectory(engine/storage)  # Phase 19 Day 256
  add_subdirectory(tests/storage)
  add_subdirectory(engine/ui)  # Phase 15 Day 184
  add_subdirectory(engine/ui_window)  # Phase 15 Day 194
  add_subdirectory(tests/ui)
  add_subdirectory(tests/io)  # the file layer, Days 151-152 (its asynchronous tests need jobs)
  add_subdirectory(tests/jobs)
  add_subdirectory(tools/crash_extract)  # Day 156: the dump reader the crash scripts use
  add_subdirectory(tools/docgen)  # the reference and the documentation checks (Day 157), the samples (Day 161)
  add_subdirectory(tests/debug)  # the debugging facilities, Days 154-156
  add_subdirectory(tests/settings)  # the settings layer and the registry store, Day 157
  add_subdirectory(engine/rhi)  # Day 160: the RHI and the Vulkan backend (DX12 from Day 162)
  add_subdirectory(engine/renderer)  # Day 161: the renderer and its suite (tests/rhi links it from Day 160)
  add_subdirectory(engine/scene)  # Phase 17 Day 233
  add_subdirectory(tests/rhi)  # Day 160: the Phase 4 RHI suite through Vulkan
  add_subdirectory(tests/renderer)  # Day 161
  add_subdirectory(tests/scene)  # Phase 17 Day 233
  add_subdirectory(game)  # hello_window (Day 158), the demos and the benchmarks (Day 161)
  add_subdirectory(tools/shader_mint)  # Day 163: the shader minter, with DXIL
  add_subdirectory(tests/shader_mint)
  add_subdirectory(shaders)  # the `shaders` target and shaders_check / shaders_portability, when slangc is found
  # The source gates are CMake scripts over the tree, the same on every platform (Day 150).
  foreach(gate rhi_boundary_check raw_new_check file_access_check std_usage_check settings_fixtures_check config_access_check
               signal_safety_check platform_neutral_check module_deps_check ui_widget_style_check sqlite_boundary_check database_sql_check)
    add_subdirectory(tools/${gate})
  endforeach()
  add_subdirectory(tests/phase_labels)
  add_subdirectory(tests/test_phase_script)
  message(STATUS "Windows (Phase 13): building the foundation, jobs, the platform layer, the RHI with its Vulkan backend, the renderer, "
                 "hello_window, the demos and the benchmarks, docgen, crash_extract, shader_mint and the shaders, the gates and their tests. Not yet "
                 "ported, so not configured: DX12 (Days 164-172). See plans/phase-13-windows-dx12.md.")
  return()
endif()

# macOS (Phase 26, a side phase on the Mac) builds one layer more each day, in dependency order (ARCHITECTURE.md), as Windows did in Phase 13:
# Day 284 the base layer and its tests (types, lang, sync, cstr) and the macOS-only checks; Day 285 the os backend and the foundation's base tests; Day 286 the gates and the test tooling. Each later day moves its directories into this list;
# the "not yet ported" message shrinks with it and is gone on Day 310 (plans/phase-26-macos-metal.md).
if(ENGINE_PLATFORM STREQUAL "macos")
  include(cmake/EngineTestPhases.cmake)
  option(ENGINE_UNITY_BUILD "Compile each target's sources as merged unity translation units" ON)
  set(CMAKE_UNITY_BUILD ${ENGINE_UNITY_BUILD})
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
  add_compile_options(${ENGINE_WARNING_COMPILE_OPTIONS} ${ENGINE_MACOS_COMPILE_OPTIONS})
  add_compile_definitions(${ENGINE_MACOS_DEFINITIONS})
  add_subdirectory(engine/foundation)
  add_subdirectory(tests/foundation)  # the base-layer tests (foundation_base_tests), Day 285; the file-layer and window ones need later days
  add_subdirectory(tests/foundation_sanitized)
  add_subdirectory(tests/io)  # the file layer, Day 287 (the asynchronous tests wait for jobs, Day 289)
  add_subdirectory(tests/types)
  add_subdirectory(tests/lang)
  add_subdirectory(tests/sync)
  add_subdirectory(tests/cstr)
  add_subdirectory(tests/macos)  # the macOS-only checks (phase 26): build_defs.h on Apple clang, the headers beside AppKit and Metal
  # The source gates are CMake scripts over the tree, the same on every platform (Day 286, as Windows on Phase 13 Day 150).
  foreach(gate rhi_boundary_check raw_new_check file_access_check std_usage_check settings_fixtures_check config_access_check
               signal_safety_check platform_neutral_check module_deps_check ui_widget_style_check sqlite_boundary_check database_sql_check)
    add_subdirectory(tools/${gate})
  endforeach()
  add_subdirectory(tests/phase_labels)
  add_subdirectory(tests/test_phase_script)
  message(STATUS "macOS (Phase 26 Day 287): building the base layer and the file layer of the foundation and their tests, the gates and the test tooling. Not "
                 "yet ported, so not configured: docgen (Day 293), the file layer's asynchronous I/O (Day 288), jobs (Day 289), debugging (Days 290-292), settings (Day 293), text, UI, ECS and "
                 "spatial (Day 294), the platform layer (Days 295-297), the RHI and renderer (Days 298-309), the demos and tools (Day 310).")
  return()
endif()

add_subdirectory(third_party/libbacktrace)

# sentry-native (third_party/sentry_native, PLAN.md assumption #20): crash capture and minidumps only, Breakpad backend, no
# transport (nothing can be uploaded). Configured here, before add_subdirectory, so its options are not left to defaults.
set(SENTRY_BACKEND breakpad CACHE STRING "" FORCE)
set(SENTRY_TRANSPORT none CACHE STRING "" FORCE)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# sentry-native sets C99 when no standard is given; glibc's memchr/strchr macros use _Generic, so every call warned (-Wc11-extensions, 16 of
# them on Clang). C11 for this library only: the variable is restored after the subdirectory so no other C target changes.
set(CMAKE_C_STANDARD 11)
add_subdirectory(third_party/sentry_native)
unset(CMAKE_C_STANDARD)
include(cmake/EngineTestPhases.cmake)
include(cmake/EngineDeathTests.cmake)

# Unity (jumbo) build: each target compiles its sources in batches merged into a few translation units, which cuts
# full-build time. Set after the vendored Catch2 so only our own targets are batched. Turn it off
# (-DENGINE_UNITY_BUILD=OFF) to check that every source still compiles on its own, or for clearer per-file errors.
option(ENGINE_UNITY_BUILD "Compile each target's sources as merged unity translation units" ON)
set(CMAKE_UNITY_BUILD ${ENGINE_UNITY_BUILD})
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)

# Warnings are errors for everything from here on (CLAUDE.md): the vendored libraries above keep their own flags.
add_compile_options(${ENGINE_WARNING_COMPILE_OPTIONS})

add_subdirectory(engine/foundation)
add_subdirectory(engine/platform)
add_subdirectory(engine/jobs)
add_subdirectory(engine/text)  # Phase 14 Day 175
add_subdirectory(engine/ecs)  # Phase 17 Day 223
add_subdirectory(engine/spatial)  # Phase 18 Day 238
add_subdirectory(engine/database)  # Phase 19 Day 251
add_subdirectory(engine/storage)  # Phase 19 Day 256
add_subdirectory(engine/ui)  # Phase 15 Day 184
add_subdirectory(engine/ui_window)  # Phase 15 Day 194
add_subdirectory(engine/rhi)
add_subdirectory(engine/renderer)
add_subdirectory(engine/scene)  # Phase 17 Day 233
add_subdirectory(tools/docgen)
add_subdirectory(tools/shader_mint)
add_subdirectory(tools/rhi_boundary_check)
add_subdirectory(tools/raw_new_check)
add_subdirectory(tools/file_access_check)
add_subdirectory(tools/std_usage_check)
add_subdirectory(tools/settings_fixtures_check)
add_subdirectory(tools/config_access_check)
add_subdirectory(tools/signal_safety_check)
add_subdirectory(tools/platform_neutral_check)
add_subdirectory(tools/module_deps_check)
add_subdirectory(tools/ui_widget_style_check)  # Phase 15 Day 195
add_subdirectory(tools/sqlite_boundary_check)  # Phase 19 Day 251
add_subdirectory(tools/database_sql_check)  # Phase 19 Day 252
add_subdirectory(tools/crash_extract)
add_subdirectory(tests/foundation)
add_subdirectory(tests/jobs)
add_subdirectory(tests/io)
add_subdirectory(tests/types)
add_subdirectory(tests/lang)  # Phase 16 Day 208
add_subdirectory(tests/sync)  # Phase 16 Day 209
add_subdirectory(tests/cstr)  # Phase 16 Day 211
add_subdirectory(tests/text)  # Phase 14 Day 175
add_subdirectory(tests/ecs)  # Phase 17 Day 223
add_subdirectory(tests/spatial)  # Phase 18 Day 238
add_subdirectory(tests/database)  # Phase 19 Day 251
add_subdirectory(tests/storage)  # Phase 19 Day 256
add_subdirectory(tests/ui)  # Phase 15 Day 184
add_subdirectory(tests/settings)
add_subdirectory(tests/foundation_sanitized)
add_subdirectory(tests/rhi)
add_subdirectory(tests/renderer)
add_subdirectory(tests/scene)  # Phase 17 Day 233
add_subdirectory(tests/debug)
add_subdirectory(tests/shader_mint)
add_subdirectory(shaders)
add_subdirectory(game)
add_subdirectory(tests/docgen)
add_subdirectory(tests/phase_labels)
add_subdirectory(tests/test_phase_script)
