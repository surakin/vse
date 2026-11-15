# Phase 23 Day 347: the ABI snapshot test. Runs the generator (abi_snapshot_gen, compiled against engine_c.h) and compares its output with the committed
# abi/engine_api.snapshot.
#   - identical: pass.
#   - different: pass only if the minor version went up (same major) and the change is an append: every `status` and `field` line of the snapshot is still in the
#     output unchanged (a moved or resized field changes its offset or size and so its line), only `struct ... size=` lines may differ. The message says to
#     regenerate the snapshot (the target `engine_abi_snapshot_update`).
#   - anything else (a removed or changed line, a lower or equal minor, another major): fail, naming the lines.
# Arguments: -DGENERATOR=<program> -DSNAPSHOT=<file>.
execute_process(COMMAND "${GENERATOR}" OUTPUT_VARIABLE generated RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "abi_snapshot: the generator failed (${rc})")
endif()
file(READ "${SNAPSHOT}" recorded)
# Windows' C runtime ends the generator's lines with CR LF, and a checkout may too (Day 354): compared without carriage returns.
string(REPLACE "\r" "" generated "${generated}")
string(REPLACE "\r" "" recorded "${recorded}")
if(generated STREQUAL recorded)
  message(STATUS "abi_snapshot: the snapshot matches")
  return()
endif()
string(REPLACE "\n" ";" new_lines "${generated}")
string(REPLACE "\n" ";" old_lines "${recorded}")
set(problems "")
string(REGEX MATCH "version ([0-9]+)\\.([0-9]+)" _ "${recorded}")
set(old_major ${CMAKE_MATCH_1})
set(old_minor ${CMAKE_MATCH_2})
string(REGEX MATCH "version ([0-9]+)\\.([0-9]+)" _ "${generated}")
set(new_major ${CMAKE_MATCH_1})
set(new_minor ${CMAKE_MATCH_2})
if(NOT new_major EQUAL old_major)
  list(APPEND problems "the major version changed (${old_major} to ${new_major}): a new major is a new snapshot, decided by hand")
elseif(NOT new_minor GREATER old_minor)
  list(APPEND problems "the ABI changed but the minor version is still ${new_minor}: bump ENGINE_ABI_VERSION_MINOR (abi/engine.exports and engine_abi_version.h)")
endif()
foreach(line IN LISTS old_lines)
  if(line MATCHES "^(status|field) " AND NOT line IN_LIST new_lines)
    list(APPEND problems "changed or removed: ${line}")
  endif()
endforeach()
if(problems)
  list(JOIN problems "\n  " text)
  message(FATAL_ERROR "abi_snapshot: the C API changed incompatibly:\n  ${text}")
endif()
message(STATUS "abi_snapshot: an append under a higher minor version (${old_major}.${old_minor} to ${new_major}.${new_minor}): regenerate abi/engine_api.snapshot with the target engine_abi_snapshot_update")
