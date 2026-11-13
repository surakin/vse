# Runs the reset loop with OBJC_DEBUG_MISSING_POOLS=YES (the ctest metal_autorelease_pools, Phase 26 Day 325) and
# fails when an object of Metal or a GPU driver is autoreleased with no pool on the test's own thread: that is an engine
# call made by a caller without a pool. Metal's own threads (the command buffers' completion) release what Metal held
# after the engine let go and report too; those are not the engine's and are not counted. Called with
# -DPROGRAM=<rhi_tests>; the environment comes from the test's ENVIRONMENT.
execute_process(COMMAND "${PROGRAM}" "Resets in a loop on a real device*" RESULT_VARIABLE result OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
set(all "${out}${err}")
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "the reset loop failed (${result}):\n${all}")
endif()
if(NOT all MATCHES "test thread \\((0x[0-9a-f]+)\\)")
  message(FATAL_ERROR "the test did not print its thread (ENGINE_RHI_PRINT_TEST_THREAD):\n${all}")
endif()
set(thread "${CMAKE_MATCH_1}")
string(REGEX MATCHALL "MISSING POOLS: \\(${thread}\\)[^\n]*class (MTL|_MTL|CAMetal|AGX|AMD|IOGPU|IOAccel)[A-Za-z0-9_]*" hits "${all}")
list(LENGTH hits count)
if(count GREATER 0)
  list(GET hits 0 first)
  message(FATAL_ERROR "${count} Metal object(s) autoreleased with no pool on the test's thread, the first: ${first}")
endif()
string(REGEX MATCHALL "MISSING POOLS: [^\n]*class (MTL|_MTL|CAMetal|AGX|AMD|IOGPU|IOAccel)" others "${all}")
list(LENGTH others other_count)
message("metal_autorelease_pools: no Metal object autoreleased without a pool on the test's thread ${thread} (${other_count} on Metal's own threads)")
