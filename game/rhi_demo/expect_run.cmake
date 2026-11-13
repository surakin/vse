# Runs a command and checks its exit code and output. Run as:
#   cmake -DCMD=<exe;args...> -DEXPECT_CODE=<n> -DEXPECT_OUT=<regex> [-DSKIP_IF_OUT=<regex>] [-DFORBID_OUT=<regex>] -P expect_run.cmake
# SKIP_NO_DISPLAY_OUT: like SKIP_IF_OUT for a machine with no window system; skipped even when a GPU is required.
# FORBID_OUT: the test fails if the output matches it (for example a validation error that does not change the exit code).
# SKIP_IF_OUT: if the output matches (no Vulkan device on this machine) the test is skipped, exit code 77 --
# unless ENGINE_RHI_REQUIRE_GPU is set, in which case it fails. A backend the platform cannot have is always skipped.
execute_process(COMMAND ${CMD} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(all "${out}${err}")
# A test that names a backend this platform cannot have (`--rhi vulkan` on macOS, `--rhi dx12` off Windows:
# rhi::CreateDevice's reason) is skipped even when a GPU is required: there is no device to require (Phase 21 Day 309).
# Only when the command asked for it by name: `auto` or a default resolving to such a backend is a failure.
string(REPLACE ";" " " cmd_text "${CMD}")
if(cmd_text MATCHES "--rhi (vulkan|dx12)( |$)")
  set(asked "${CMAKE_MATCH_1}")
  if(all MATCHES "the ${asked} backend is not built for (Linux|Windows|macOS)")
    message(STATUS "skipped: the ${asked} backend is not built for this platform")
    cmake_language(EXIT 77)
  endif()
endif()
if(SKIP_IF_OUT AND all MATCHES "${SKIP_IF_OUT}")
  if("$ENV{ENGINE_RHI_REQUIRE_GPU}" MATCHES "^1") # same rule as gpu_test::GpuRequired
    message(FATAL_ERROR "no Vulkan device, but ENGINE_RHI_REQUIRE_GPU is set:\n${all}")
  endif()
  message(STATUS "skipped: no Vulkan device")
  cmake_language(EXIT 77)
endif()
if(SKIP_NO_DISPLAY_OUT AND all MATCHES "${SKIP_NO_DISPLAY_OUT}")
  message(STATUS "skipped: no display session")
  cmake_language(EXIT 77)
endif()
if(NOT code STREQUAL "${EXPECT_CODE}")
  message(FATAL_ERROR "exit code ${code}, expected ${EXPECT_CODE}:\n${all}")
endif()
if(FORBID_OUT AND all MATCHES "${FORBID_OUT}")
  message(FATAL_ERROR "output matches the forbidden '${FORBID_OUT}':\n${all}")
endif()
if(NOT all MATCHES "${EXPECT_OUT}")
  message(FATAL_ERROR "output does not match '${EXPECT_OUT}':\n${all}")
endif()
