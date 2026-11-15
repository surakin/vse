# Phase 23 Day 347: writes abi/engine_api.snapshot from the generator (the target engine_abi_snapshot_update). -DGENERATOR=<program> -DSNAPSHOT=<file>.
execute_process(COMMAND "${GENERATOR}" OUTPUT_FILE "${SNAPSHOT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "abi_snapshot_update: the generator failed (${rc})")
endif()
