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

# macOS (Phase 21 Day 284): Apple clang from the Command Line Tools is the only compiler (Phase 21 rules), found through xcrun so that a
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
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_SYSTEM_NAME)  # not for a cross build (iOS, Phase 28)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version (13.3: floating-point std::to_chars; Metal 3 needs 13.0)")
endif()

project(engine_placeholder CXX C)
if(APPLE)
  # Objective-C++ (.mm files with ARC) is how the macOS backends talk to AppKit, Metal and QuartzCore (Phase 21 decision 3).
  enable_language(OBJCXX)
endif()

# Supported targets: x86-64 Linux, x86-64 Windows (Phase 13), and x86-64 macOS (Phase 21, a side phase on the Mac). arm64 macOS has a compile-only
# configuration (Phase 21 Day 311, preset macos-clang-arm64): everything builds and nothing runs, since the Mac that builds it is Intel. Anything
# else names the phase that ports it.
option(ENGINE_COMPILE_ONLY "Build every target and run nothing built (no test discovery, no shader minting): a cross-compiled check" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(ENGINE_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(ENGINE_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
  set(ENGINE_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND (CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR
       (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")))
  # arm64: built and run natively on Apple silicon (not seen yet: no such Mac), compile-only from an Intel Mac (or a CMake under Rosetta).
  set(ENGINE_PLATFORM macos)
  if(NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" AND NOT ENGINE_COMPILE_ONLY)
    message(FATAL_ERROR "arm64 macOS on an Intel Mac builds compile-only: configure with -DENGINE_COMPILE_ONLY=ON (preset macos-clang-arm64)")
  endif()
  if(ENGINE_COMPILE_ONLY)
    message(STATUS "macOS arm64 (Phase 21 Day 311): compile-only, nothing built is run here")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS on ${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_OSX_ARCHITECTURES}): x86-64 and arm64 (compile-only) are the supported "
                      "architectures (plans/phase-21-macos-metal.md)")
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Android|iOS)$")
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is ported in Phase 28 (plans/phase-28-android-ios.md)")
else()
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} is not a supported target (x86-64 Linux, x86-64 Windows; consoles: Phase 29)")
endif()

# Every executable gets a GNU build id (a hash of its contents) so a crash report and a minidump name the exact binary.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_link_options(-Wl,--build-id)
endif()

# Release symbols (Phase 7 Day 99): with this on, every build keeps debug information (-g) and the demos and benches have it split
# out after linking (scripts/split-debug.sh: <program>.debug beside a stripped program that keeps its build id and links to the
# debug file). Put the .debug files in a symbol store with scripts/store-symbols.sh to read crash dumps from stripped builds.
option(ENGINE_SPLIT_DEBUG_INFO "Build with -g and split the debug information out of the demos and benches (Linux; macOS: see engine_dsym)" OFF)
if(ENGINE_SPLIT_DEBUG_INFO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_compile_options(-g)
endif()
# macOS (Phase 21 Day 290): the linker leaves the DWARF in the object files and only a debug map in the binary, and libbacktrace reads file and line
# from a dSYM bundle next to the binary. engine_dsym(<target>) runs dsymutil after linking (<binary>.dSYM beside <binary>); without dsymutil, or with
# ENGINE_DSYM off, traces name functions only (from the symbol table). Nothing is stripped: the dSYM is the debug information, the binary keeps its
# symbols.
if(APPLE)
  option(ENGINE_DSYM "macOS: make a dSYM next to the binaries that call engine_dsym (dsymutil) so their stack traces have file and line" ON)
  if(ENGINE_DSYM)
    execute_process(COMMAND xcrun --find dsymutil OUTPUT_VARIABLE ENGINE_DSYMUTIL OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT ENGINE_DSYMUTIL)
      message(STATUS "dsymutil not found (xcode-select --install): no dSYMs, so stack traces name functions without file and line")
    endif()
  endif()
endif()
function(engine_dsym target)
  if(NOT APPLE)
    return()
  endif()
  # Only a build with debug information has DWARF to collect (Debug, RelWithDebInfo); otherwise, and with the option off, an old dSYM is removed
  # after each link, so a stale bundle never sits beside a rebuilt binary.
  if(ENGINE_DSYM AND ENGINE_DSYMUTIL AND CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE:${target}>.dSYM
      COMMAND ${ENGINE_DSYMUTIL} $<TARGET_FILE:${target}> -o $<TARGET_FILE:${target}>.dSYM
      COMMENT "Writing the dSYM of ${target}")
  else()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE:${target}>.dSYM)
  endif()
endfunction()

# engine_split_debug_info(<executable>): the per-program half of the option above. On macOS it is the dSYM (engine_dsym).
function(engine_split_debug_info target)
  if(ENGINE_SPLIT_DEBUG_INFO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_SOURCE_DIR}/scripts/split-debug.sh $<TARGET_FILE:${target}>
      COMMENT "Splitting the debug information of ${target}")
  endif()
  engine_dsym(${target})
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

if(ENGINE_COMPILE_ONLY)
  # Nothing built here may run (Phase 21 Day 311): the tests are declared, so every target and test script is configured as usual, but no
  # CTestTestfile is written, so ctest finds nothing to run.
  message(STATUS "Compile-only build: tests are declared but not enabled (ctest runs nothing here)")
else()
  enable_testing()
endif()
# Where tests keep the files they make while they run (a private settings profile, pipeline caches, the command-line checks' folders, scratch
# projects): one folder of the build tree, safe to delete at any time.
set(ENGINE_TEST_TMP_DIR "${CMAKE_BINARY_DIR}/test-tmp")
file(MAKE_DIRECTORY "${ENGINE_TEST_TMP_DIR}/pipeline-cache" "${ENGINE_TEST_TMP_DIR}/screenshots")
add_subdirectory(third_party/catch2)
# Every platform, macOS since Phase 21 Day 294 (text and the database arrived there that day).
add_subdirectory(third_party/freetype)  # Phase 14 Day 175: FreeType, vendored (PLAN.md assumption #17)
add_subdirectory(third_party/sqlite)  # Phase 19 Day 251: SQLite, vendored (PLAN.md assumption #25), used only behind engine/database/sqlite/
# Catch2's REQUIRE/CHECK throw: exceptions on for Catch2 and, through PUBLIC, every test executable that links it.
if(ENGINE_EXCEPTIONS_COMPILE_OPTIONS)
  target_compile_options(Catch2 PUBLIC ${ENGINE_EXCEPTIONS_COMPILE_OPTIONS})
endif()

# Windows (Phase 13) builds one layer more each day, in dependency order (ARCHITECTURE.md): today the base layer of the
# foundation and the Windows-only tests. Each later day moves its directories from the skipped list into the build.
if(ENGINE_PLATFORM STREQUAL "windows")
  # sentry-native for the crash handler (Phase 13 Days 154-156), configured as on Linux (below) and added before the engine's own options,
  # so it keeps its own. libbacktrace is not built on Windows: stack traces resolve through DbgHelp (os/windows/debug/BACKEND.md).
  # The engine sets each thread's stack guarantee itself (RegisterThreadForCrashHandling, os/windows/debug/BACKEND.md); the library's own
  # setting at sentry_init would be a second, smaller one that only logs a warning when ours is already there.
  set(SENTRY_THREAD_STACK_GUARANTEE_AUTO_INIT OFF CACHE BOOL "" FORCE)
  include(cmake/EngineSentry.cmake)
  engine_add_sentry()
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
  add_subdirectory(engine/net)  # Phase 20 Day 267
  add_subdirectory(tests/net)
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
  add_subdirectory(engine/asset)  # Phase 22 Day 313
  add_subdirectory(engine/animation)  # Phase 22 Day 321
  add_subdirectory(tools/asset_mint)  # Phase 22 Day 316
  add_subdirectory(tests/asset)
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
  add_subdirectory(tools/net_access_check)  # Phase 20 Day 268 (its own line: editing the gate list above would widen scripts/test-phase.sh --affected)
  add_subdirectory(tools/net_probe)  # Phase 20 Day 281
  add_subdirectory(tests/phase_labels)
  add_subdirectory(tests/test_phase_script)
  message(STATUS "Windows (Phase 13): building the foundation, jobs, the platform layer, the RHI with its Vulkan backend, the renderer, "
                 "hello_window, the demos and the benchmarks, docgen, crash_extract, shader_mint and the shaders, the gates and their tests. Not yet "
                 "ported, so not configured: DX12 (Days 164-172). See plans/phase-13-windows-dx12.md.")
  return()
endif()

# macOS (Phase 21, a side phase on the Mac): everything the Linux build has, ported one layer a day in dependency order (ARCHITECTURE.md), as
# Windows was in Phase 13; complete on Day 311 (plans/phase-21-macos-metal.md). Not built here: Vulkan (decision 8) and Direct3D 12.
if(ENGINE_PLATFORM STREQUAL "macos")
  include(cmake/EngineTestPhases.cmake)
  # Third-party code first, before the engine's warning options, so it keeps its own flags (as on Linux). libbacktrace: Mach-O, Day 290.
  add_subdirectory(third_party/libbacktrace)
  # sentry-native for the crash handler (Day 291), configured as on Linux: the Breakpad backend (Mach exception ports on macOS), no transport.
  include(cmake/EngineSentry.cmake)
  engine_add_sentry(C11)
  option(ENGINE_UNITY_BUILD "Compile each target's sources as merged unity translation units" ON)
  set(CMAKE_UNITY_BUILD ${ENGINE_UNITY_BUILD})
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
  add_compile_options(${ENGINE_WARNING_COMPILE_OPTIONS} ${ENGINE_MACOS_COMPILE_OPTIONS})
  add_compile_definitions(${ENGINE_MACOS_DEFINITIONS})
  add_subdirectory(engine/foundation)
  add_subdirectory(engine/jobs)  # Day 289
  add_subdirectory(tests/jobs)
  # Day 294: the modules above the foundation with no platform code (layers 10-11), and their tests; engine/net's macOS backend is
  # Day 310. Day 295: engine/platform builds the Cocoa window (input and the rest on Days 296-297).
  add_subdirectory(engine/platform)
  add_subdirectory(engine/text)
  add_subdirectory(tests/text)
  add_subdirectory(engine/ecs)
  add_subdirectory(tests/ecs)
  add_subdirectory(engine/spatial)
  add_subdirectory(tests/spatial)
  add_subdirectory(engine/database)
  add_subdirectory(tests/database)
  add_subdirectory(engine/storage)
  add_subdirectory(tests/storage)
  add_subdirectory(engine/net)  # Day 310: the POSIX backend with the macOS hooks and the kqueue poller
  add_subdirectory(tests/net)
  add_subdirectory(engine/ui)
  add_subdirectory(engine/ui_window)  # Day 298, before tests/ui, which builds its window-bound tests when the target exists
  add_subdirectory(tests/ui)
  # The RHI with its null and Metal backends (no Vulkan on macOS: Phase 21 decision 8; Metal from Day 300), the renderer, the scene bridge,
  # the UI window adapter and every program of game/.
  add_subdirectory(engine/rhi)
  add_subdirectory(engine/renderer)
  add_subdirectory(engine/scene)
  add_subdirectory(engine/asset)
  add_subdirectory(engine/animation)
  add_subdirectory(tools/asset_mint)  # Phase 22 Day 316
  add_subdirectory(tests/asset)
  add_subdirectory(tests/rhi)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/scene)
  add_subdirectory(game)  # Day 294's ecs_bench, Day 295's hello_window and Day 296's input_probe, and from Day 298 the demos
  add_subdirectory(tools/metal_mint)  # Day 299: the Metal library minter (macOS only), before the shaders that use it
  add_subdirectory(tools/shader_mint)  # Day 299: the shader minter, with the Metal library (tools/metal_mint)
  add_subdirectory(tests/shader_mint)
  add_subdirectory(shaders)  # the `shaders` target and shaders_check / shaders_portability, when slangc is found
  add_subdirectory(tests/foundation)  # the foundation's tests (the base layer from Day 285, the file layer and the window from Days 287-297)
  add_subdirectory(tests/foundation_sanitized)
  add_subdirectory(tests/io)  # the file layer, Days 287-289
  add_subdirectory(tools/crash_extract)  # Day 290: the dump reader (the resolver's macOS branch is Day 292)
  include(cmake/EngineDeathTests.cmake)
  add_subdirectory(tests/debug)  # Day 290: stack traces and the crash context; the rest of the backend on Days 291-292
  add_subdirectory(tests/settings)  # Day 293: the settings layer and the user defaults store
  add_subdirectory(tools/docgen)  # Day 293: the reference and the documentation checks; the samples from Day 311
  add_subdirectory(tests/docgen)
  add_subdirectory(tests/types)
  add_subdirectory(tests/lang)
  add_subdirectory(tests/sync)
  add_subdirectory(tests/cstr)
  add_subdirectory(tests/macos)  # the macOS-only checks (phase 21): build_defs.h on Apple clang, the headers beside AppKit and Metal
  # The source gates are CMake scripts over the tree, the same on every platform (Day 286, as Windows on Phase 13 Day 150).
  foreach(gate rhi_boundary_check raw_new_check file_access_check std_usage_check settings_fixtures_check config_access_check
               signal_safety_check platform_neutral_check module_deps_check ui_widget_style_check sqlite_boundary_check database_sql_check)
    add_subdirectory(tools/${gate})
  endforeach()
  add_subdirectory(tools/net_access_check)  # Phase 20 Day 268 (its own line: editing the gate list above would widen scripts/test-phase.sh --affected)
  add_subdirectory(tools/net_probe)  # Phase 21 Day 310
  add_subdirectory(tests/phase_labels)
  add_subdirectory(tests/test_phase_script)
  message(STATUS "macOS (Phase 21): building everything the Linux build has, with the Metal backend in place of Vulkan (Day 311)")
  return()
endif()

add_subdirectory(third_party/libbacktrace)

# sentry-native (third_party/sentry_native, PLAN.md assumption #20): crash capture and minidumps only, Breakpad backend, no
# transport (nothing can be uploaded). Configured here, before add_subdirectory, so its options are not left to defaults.
# (cmake/EngineSentry.cmake: the same configuration on every platform; C11 for the library's C, as glibc's macros need.)
include(cmake/EngineSentry.cmake)
engine_add_sentry(C11)
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
add_subdirectory(engine/net)  # Phase 20 Day 267
add_subdirectory(engine/ui)  # Phase 15 Day 184
add_subdirectory(engine/ui_window)  # Phase 15 Day 194
add_subdirectory(engine/rhi)
add_subdirectory(engine/renderer)
add_subdirectory(engine/scene)  # Phase 17 Day 233
add_subdirectory(engine/asset)  # Phase 22 Day 313
add_subdirectory(engine/animation)  # Phase 22 Day 321
add_subdirectory(tools/asset_mint)  # Phase 22 Day 316
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
add_subdirectory(tools/net_access_check)  # Phase 20 Day 268
add_subdirectory(tools/net_probe)  # Phase 20 Day 281
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
add_subdirectory(tests/net)  # Phase 20 Day 267
add_subdirectory(tests/asset)  # Phase 22 Day 313
add_subdirectory(tests/animation)  # Phase 22 Day 321
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
