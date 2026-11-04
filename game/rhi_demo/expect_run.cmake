# Runs a command and checks its exit code and output. Run as:
#   cmake -DCMD=<exe;args...> -DEXPECT_CODE=<n> -DEXPECT_OUT=<regex> [-DSKIP_IF_OUT=<regex>] -P expect_run.cmake
# SKIP_IF_OUT: if the output matches (no Vulkan device on this machine) the test is skipped, exit code 77 --
# unless ENGINE_RHI_REQUIRE_GPU is set, in which case it fails.
execute_process(COMMAND ${CMD} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(all "${out}${err}")
if(SKIP_IF_OUT AND all MATCHES "${SKIP_IF_OUT}")
  if("$ENV{ENGINE_RHI_REQUIRE_GPU}" MATCHES "^1") # same rule as gpu_test::GpuRequired
    message(FATAL_ERROR "no Vulkan device, but ENGINE_RHI_REQUIRE_GPU is set:\n${all}")
  endif()
  message(STATUS "skipped: no Vulkan device")
  cmake_language(EXIT 77)
endif()
if(NOT code STREQUAL "${EXPECT_CODE}")
  message(FATAL_ERROR "exit code ${code}, expected ${EXPECT_CODE}:\n${all}")
endif()
if(NOT all MATCHES "${EXPECT_OUT}")
  message(FATAL_ERROR "output does not match '${EXPECT_OUT}':\n${all}")
endif()
