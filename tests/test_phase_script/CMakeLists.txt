# scripts/test-phase.sh: its choice of phases, and that its "X of Y" is what ctest selects (Phase 6). Every case runs the
# script with --dry-run against this build, reading the changed paths and the current phase from fixture files instead
# of git and CURRENT_PHASE (see check.cmake).
find_program(BASH_PROGRAM bash)
if(NOT BASH_PROGRAM)
  return()
endif()
set(TEST_PHASE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/test-phase.sh)
set(FIX ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# phase_script_case(<name> PHASES <expected> [DISPLAY yes|no] ARGS <script arguments...> [CTEST <ctest -N arguments...>])
function(phase_script_case name)
  cmake_parse_arguments(C "" "PHASES;DISPLAY" "ARGS;CTEST" ${ARGN})
  set(display "")
  if(C_DISPLAY)
    set(display "-DEXPECT_DISPLAY=${C_DISPLAY}")
  endif()
  add_test(NAME test_phase_script_${name}
    COMMAND ${CMAKE_COMMAND} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} "-DARGS=${C_ARGS}"
      "-DEXPECT_PHASES=${C_PHASES}" "-DCTEST_ARGS=${C_CTEST}" ${display} -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_${name})
  set(PHASE_SCRIPT_TESTS ${PHASE_SCRIPT_TESTS} PARENT_SCOPE)
endfunction()

# phase_script_refuses(<name> <message regex> <script arguments...>)
function(phase_script_refuses name text)
  add_test(NAME test_phase_script_${name}
    COMMAND ${CMAKE_COMMAND} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} "-DARGS=${ARGN}"
      "-DEXPECT_FAIL_TEXT=${text}" -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_${name})
  set(PHASE_SCRIPT_TESTS ${PHASE_SCRIPT_TESTS} PARENT_SCOPE)
endfunction()

# Every scoped run leaves out the tests labelled `crash` (the Phase 7 suite crashes processes on purpose); --all and --crashes keep them.
set(PLAIN -LE "^(asan|tsan|crash)$")
set(PLAIN_NO_DISPLAY -LE "^(asan|tsan|display|crash)$")

# One explicit phase: all of its tests, in the plain flavor by default.
phase_script_case(explicit_plain PHASES 5 DISPLAY included ARGS 5 --flavor plain CTEST -L "^(phase5)$" ${PLAIN})
phase_script_case(explicit_asan PHASES 4 ARGS 4 --flavor asan CTEST -L "^(phase4)$" -L "^asan$" -LE "^crash$")
phase_script_case(explicit_tsan PHASES 2 ARGS 2 --flavor tsan CTEST -L "^(phase2)$" -L "^tsan$" -LE "^crash$")
phase_script_case(explicit_all_flavors PHASES 3 ARGS 3 --flavor all CTEST -L "^(phase3)$" -LE "^crash$")
phase_script_case(everything PHASES all ARGS --all --flavor all)
phase_script_case(everything_without_windows PHASES all DISPLAY excluded ARGS --all --no-display CTEST -LE "^(asan|tsan|display)$")
phase_script_case(current_with_crashes PHASES 5 DISPLAY excluded ARGS --current --crashes --current-phase-file ${FIX}/phase5.txt
  CTEST -L "^(phase5)$" -LE "^(asan|tsan|display)$")
# Without --diff-from-file the script asks git; any result is fine, it only has to work.
phase_script_case(affected_from_git PHASES ANY ARGS --affected --current-phase-file ${FIX}/phase5.txt)
# The current phase: window tests are left out, unless the phase is 1.
phase_script_case(current PHASES 5 DISPLAY excluded ARGS --current --current-phase-file ${FIX}/phase5.txt
  CTEST -L "^(phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(current_platform PHASES 1 DISPLAY included ARGS --current --current-phase-file ${FIX}/phase1.txt
  CTEST -L "^(phase1)$" ${PLAIN})
phase_script_case(current_forced_display PHASES 5 DISPLAY included ARGS --current --display --current-phase-file ${FIX}/phase5.txt
  CTEST -L "^(phase5)$" ${PLAIN})
# --affected: the current phase plus the phases of the touched modules.
phase_script_case(affected_rhi PHASES "3 4 5" DISPLAY excluded ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_rhi.txt CTEST -L "^(phase3|phase4|phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_jobs PHASES "2 3 4 5" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_jobs.txt CTEST -L "^(phase2|phase3|phase4|phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_runner PHASES "5 6" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_runner.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_docgen_and_shaders PHASES "3 4 5" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_docgen_shaders.txt CTEST -L "^(phase3|phase4|phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_notes_only PHASES 5 ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_notes.txt CTEST -L "^(phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_foundation_source PHASES "0 1 2 3 4 5 7 9 10" DISPLAY included ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_foundation_source.txt CTEST -L "^(phase0|phase1|phase2|phase3|phase4|phase5|phase7|phase9|phase10)$" ${PLAIN})
# Escalations to the whole suite.
phase_script_case(affected_foundation_header PHASES all DISPLAY included ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_foundation_header.txt CTEST ${PLAIN})
phase_script_case(affected_unknown_directory PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_unknown.txt CTEST ${PLAIN})
phase_script_case(affected_cmake_file PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_cmake.txt CTEST ${PLAIN})
phase_script_case(affected_phase_map PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_map.txt CTEST ${PLAIN})
# Refusals.
phase_script_refuses(bad_current_phase "not a phase number" --current --current-phase-file ${FIX}/bad.txt)
phase_script_refuses(missing_current_phase "is missing" --current --current-phase-file ${FIX}/nonexistent.txt)
phase_script_refuses(unknown_phase "not a known phase" 99)
phase_script_refuses(bad_flavor "--flavor must be" 5 --flavor sanitized)
phase_script_refuses(unknown_option "unknown option" --everything)

# Every path in the module map exists, so a renamed directory cannot leave a stale line behind.
add_test(NAME test_phase_script_module_map_paths
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DMAP=${CMAKE_SOURCE_DIR}/tests/module_map.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/module_map_paths.cmake)
list(APPEND PHASE_SCRIPT_TESTS test_phase_script_module_map_paths)
# The three flavors of a phase add up to all of its tests: the label expressions neither overlap nor leave one out.
foreach(phase 2 4 5)
  add_test(NAME test_phase_script_flavors_partition_${phase}
    COMMAND ${CMAKE_COMMAND} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} -DPHASE=${phase}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/partition.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_flavors_partition_${phase})
endforeach()

engine_label_tests(${PHASE_SCRIPT_TESTS})
