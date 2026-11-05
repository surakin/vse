cmake_minimum_required(VERSION 3.20)
# C is enabled alongside CXX only to compile the generated xdg-shell
# protocol bindings (os/linux/wayland/generated/xdg-shell-protocol.c,
# wayland-scanner's own output format) — the engine itself is C++20
# throughout; see PLAN.md assumption #2.
project(engine_placeholder CXX C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  message(FATAL_ERROR "Only x86-64 Linux is supported (Phase 0)")
endif()

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

enable_testing()
add_subdirectory(third_party/catch2)

add_subdirectory(engine/foundation)
add_subdirectory(engine/jobs)
add_subdirectory(engine/rhi)
add_subdirectory(engine/renderer)
add_subdirectory(tools/docgen)
add_subdirectory(tools/shader_cook)
add_subdirectory(tools/rhi_boundary_check)
add_subdirectory(tests/foundation)
add_subdirectory(tests/jobs)
add_subdirectory(tests/rhi)
add_subdirectory(tests/renderer)
add_subdirectory(tests/shader_cook)
add_subdirectory(shaders)
add_subdirectory(game)
add_subdirectory(tests/docgen)
