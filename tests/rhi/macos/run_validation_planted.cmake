# Runs metal_validation_planted (Phase 21 Day 300) and passes only if Metal's validation stopped it: the output holds
# "failed assertion" and the program did not exit normally. A plain ctest cannot say this: it counts any abort as a
# failure, whatever the output (the TSan canary's note in tests/jobs/CMakeLists.txt). Called with -DPROGRAM=<path>;
# the validation variables come from the test's ENVIRONMENT.
execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if(result STREQUAL "0")
  message(FATAL_ERROR "metal_validation_planted exited 0: the planted call was not caught")
endif()
if(NOT "${out}${err}" MATCHES "failed assertion")
  message(FATAL_ERROR "metal_validation_planted ended (${result}) without Metal's failed assertion")
endif()
message("metal_validation_planted: stopped by Metal's validation (${result}), as planted")
