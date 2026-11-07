cmake_minimum_required(VERSION 3.21)  # 3.21: presets that name a toolchain file (CMakePresets.json)
# C is enabled alongside CXX only to compile the generated xdg-shell
# protocol bindings (platform/linux/wayland/generated/xdg-shell-protocol.c,
# wayland-scanner's own output format) — the engine itself is C++20
# throughout; see PLAN.md assumption #2.
project(engine_placeholder CXX C)

# Supported targets: x86-64 Linux, and x86-64 Windows (being ported in Phase 13). Anything else names the phase that ports it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(ENGINE_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(ENGINE_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "macOS is ported in Phase 14 (plans/phase-14-macos-metal.md)")
elseif(CMAKE_SYSTEM_NAME MATCHES "^(Android|iOS)$")
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is ported in Phase 22 (plans/phase-22-android-ios.md)")
else()
  message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} is not a supported target (x86-64 Linux, x86-64 Windows; consoles: Phase 23)")
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
add_subdirectory(third_party/catch2)
# Catch2's REQUIRE/CHECK throw: exceptions on for Catch2 and, through PUBLIC, every test executable that links it.
if(ENGINE_EXCEPTIONS_COMPILE_OPTIONS)
  target_compile_options(Catch2 PUBLIC ${ENGINE_EXCEPTIONS_COMPILE_OPTIONS})
endif()

# Windows (Phase 13) builds one layer more each day, in dependency order (ARCHITECTURE.md): today the base layer of the
# foundation and the Windows-only tests. Each later day moves its directories from the skipped list into the build.
if(ENGINE_PLATFORM STREQUAL "windows")
  # The engine's own Windows options, for every target created from here on (the vendored libraries above keep their own).
  add_compile_options(${ENGINE_WINDOWS_COMPILE_OPTIONS})
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
  add_subdirectory(tests/windows)
  # tests/phase_labels joins on Day 150, when the phase map can say which tests each platform has (every other phase's tests are
  # absent here, so its flavor and unused-line checks would fail today); engine_label_tests() already refuses an unmapped test.
  message(STATUS "Windows (Phase 13): building foundation_base and tests/windows. Not yet ported, so not configured: the file layer "
                 "(Days 151-152), jobs (153), debugging and sentry-native (154-156; libbacktrace is Linux-only), settings (157), platform (158-159), "
                 "RHI and renderer (160-172), tools, game and the other tests (as their layers arrive). See plans/phase-13-windows-dx12.md.")
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
add_subdirectory(third_party/sentry_native)
include(cmake/EngineTestPhases.cmake)
include(cmake/EngineDeathTests.cmake)

# Unity (jumbo) build: each target compiles its sources in batches merged into a few translation units, which cuts
# full-build time. Set after the vendored Catch2 so only our own targets are batched. Turn it off
# (-DENGINE_UNITY_BUILD=OFF) to check that every source still compiles on its own, or for clearer per-file errors.
option(ENGINE_UNITY_BUILD "Compile each target's sources as merged unity translation units" ON)
set(CMAKE_UNITY_BUILD ${ENGINE_UNITY_BUILD})
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)

add_subdirectory(engine/foundation)
add_subdirectory(engine/platform)
add_subdirectory(engine/jobs)
add_subdirectory(engine/rhi)
add_subdirectory(engine/renderer)
add_subdirectory(tools/docgen)
add_subdirectory(tools/shader_mint)
add_subdirectory(tools/rhi_boundary_check)
add_subdirectory(tools/raw_new_check)
add_subdirectory(tools/file_access_check)
add_subdirectory(tools/std_container_check)
add_subdirectory(tools/settings_fixtures_check)
add_subdirectory(tools/config_access_check)
add_subdirectory(tools/signal_safety_check)
add_subdirectory(tools/platform_neutral_check)
add_subdirectory(tools/module_deps_check)
add_subdirectory(tools/crash_extract)
add_subdirectory(tests/foundation)
add_subdirectory(tests/jobs)
add_subdirectory(tests/io)
add_subdirectory(tests/types)
add_subdirectory(tests/settings)
add_subdirectory(tests/foundation_sanitized)
add_subdirectory(tests/rhi)
add_subdirectory(tests/renderer)
add_subdirectory(tests/debug)
add_subdirectory(tests/shader_mint)
add_subdirectory(shaders)
add_subdirectory(game)
add_subdirectory(tests/docgen)
add_subdirectory(tests/phase_labels)
add_subdirectory(tests/test_phase_script)
