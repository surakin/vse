# Phase 23 Day 347: writes abi/engine_api.snapshot from the generator (the target engine_abi_snapshot_update). -DGENERATOR=<program> -DSNAPSHOT=<file>.
execute_process(COMMAND "${GENERATOR}" OUTPUT_VARIABLE generated RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "abi_snapshot_update: the generator failed (${rc})")
endif()
# Windows' C runtime ends the generator's lines with CR LF (Day 354): the snapshot is LF on every system.
string(REPLACE "\r" "" generated "${generated}")
file(CONFIGURE OUTPUT "${SNAPSHOT}" CONTENT "${generated}" @ONLY NEWLINE_STYLE UNIX)  # file(WRITE) writes CR LF on Windows
