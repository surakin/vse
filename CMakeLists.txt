cmake_minimum_required(VERSION 3.20)
# C is enabled alongside CXX only to compile the generated xdg-shell
# protocol bindings (os/linux/wayland/generated/xdg-shell-protocol.c,
# wayland-scanner's own output format) — the engine itself is C++20
# throughout; see PLAN.md assumption #2.
project(engine_placeholder CXX C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  message(FATAL_ERROR "Only x86-64 Linux is supported (Phase 0)")
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

# Exceptions/RTTI/stack-protector are disabled for the engine's
# own production code (see CLAUDE.md's hard architectural constraints).
# Applied per-target (engine/foundation/CMakeLists.txt), not build-wide,
# so vendored test tooling (Catch2, which needs exceptions for its
# REQUIRE/CHECK macros) and the test executable that links it aren't
# affected.
set(ENGINE_PRODUCTION_COMPILE_OPTIONS -fno-exceptions -fno-rtti -fno-stack-protector)

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
