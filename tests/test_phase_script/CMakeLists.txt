# scripts/test-phase.sh: its choice of phases, and that its "X of Y" is what ctest selects (Phase 6). Every case runs the
# script with --dry-run against this build, reading the changed paths and the current phase from fixture files instead
# of git and CURRENT_PHASE (see check.cmake).
# On Windows the script runs under Git for Windows' bash, found next to git itself (a plain shell has no bash on PATH, and System32's
# bash.exe would be WSL's).
if(CMAKE_HOST_WIN32)
  find_package(Git QUIET)
  if(GIT_EXECUTABLE)
    # git.exe is in cmd/ (a plain shell's PATH), bin/ or mingw64/bin/ (Git Bash's PATH); bash.exe is in the Git folder's bin/ and usr/bin/.
    get_filename_component(_git_dir "${GIT_EXECUTABLE}" DIRECTORY)
    find_program(BASH_PROGRAM bash HINTS "${_git_dir}" "${_git_dir}/../bin" "${_git_dir}/../../bin" "${_git_dir}/../usr/bin"
      "${_git_dir}/../../usr/bin" NO_DEFAULT_PATH)
  endif()
else()
  find_program(BASH_PROGRAM bash)
endif()
# bash is a required prerequisite (BUILD.md): without it these tests would vanish from the build unnoticed (found by the Day 150 review).
if(NOT BASH_PROGRAM)
  message(FATAL_ERROR "tests/test_phase_script: no bash found (Windows: Git for Windows' bash, next to git.exe); BUILD.md lists it as required")
endif()
set(TEST_PHASE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/test-phase.sh)
set(FIX ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# phase_script_case(<name> PHASES <expected> [DISPLAY yes|no] [SIDE <side phases file>] ARGS <script arguments...> [CTEST <ctest -N arguments...>])
function(phase_script_case name)
  cmake_parse_arguments(C "" "PHASES;DISPLAY;SIDE" "ARGS;CTEST" ${ARGN})
  set(display "")
  if(C_DISPLAY)
    set(display "-DEXPECT_DISPLAY=${C_DISPLAY}")
  endif()
  if(C_SIDE)
    list(APPEND display "-DSIDE_FILE=${C_SIDE}")
  endif()
  add_test(NAME test_phase_script_${name}
    COMMAND ${CMAKE_COMMAND} -DBASH=${BASH_PROGRAM} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} "-DARGS=${C_ARGS}"
      "-DEXPECT_PHASES=${C_PHASES}" "-DCTEST_ARGS=${C_CTEST}" ${display} -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_${name})
  set(PHASE_SCRIPT_TESTS ${PHASE_SCRIPT_TESTS} PARENT_SCOPE)
endfunction()

# phase_script_refuses(<name> <message regex> <script arguments...>)
function(phase_script_refuses name text)
  add_test(NAME test_phase_script_${name}
    COMMAND ${CMAKE_COMMAND} -DBASH=${BASH_PROGRAM} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} "-DARGS=${ARGN}"
      "-DEXPECT_FAIL_TEXT=${text}" -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_${name})
  set(PHASE_SCRIPT_TESTS ${PHASE_SCRIPT_TESTS} PARENT_SCOPE)
endfunction()

# Every scoped run leaves out the tests labelled `crash` (the Phase 7 suite crashes processes on purpose); --all and --crashes keep them.
set(PLAIN -LE "^(asan|tsan|crash)$")
set(PLAIN_NO_DISPLAY -LE "^(asan|tsan|display|crash)$")

# One explicit phase: all of its tests, in the plain flavor by default.
phase_script_case(explicit_plain PHASES 5 DISPLAY included ARGS 5 --flavor plain CTEST -L "^(phase5)$" ${PLAIN})
# The sanitizer cases use phases that have those flavors on the platform (Windows: plain and asan only, in phases 0, 10 and 13 so far).
if(ENGINE_PLATFORM STREQUAL "windows")
  phase_script_case(explicit_asan PHASES 13 ARGS 13 --flavor asan CTEST -L "^(phase13)$" -L "^asan$" -LE "^crash$")
  phase_script_refuses(explicit_tsan "MSVC has no ThreadSanitizer" 2 --flavor tsan)
  phase_script_case(explicit_all_flavors PHASES 13 ARGS 13 --flavor all CTEST -L "^(phase13)$" -LE "^crash$")
elseif(ENGINE_PLATFORM STREQUAL "macos")
  # macOS builds one layer more each day (Phase 26): the phases with sanitizer tests there so far are 0, 10, 16 and 26.
  phase_script_case(explicit_asan PHASES 16 ARGS 16 --flavor asan CTEST -L "^(phase16)$" -L "^asan$" -LE "^crash$")
  phase_script_case(explicit_tsan PHASES 10 ARGS 10 --flavor tsan CTEST -L "^(phase10)$" -L "^tsan$" -LE "^crash$")
  phase_script_case(explicit_all_flavors PHASES 26 ARGS 26 --flavor all CTEST -L "^(phase26)$" -LE "^crash$")
else()
  phase_script_case(explicit_asan PHASES 4 ARGS 4 --flavor asan CTEST -L "^(phase4)$" -L "^asan$" -LE "^crash$")
  phase_script_case(explicit_tsan PHASES 2 ARGS 2 --flavor tsan CTEST -L "^(phase2)$" -L "^tsan$" -LE "^crash$")
  phase_script_case(explicit_all_flavors PHASES 3 ARGS 3 --flavor all CTEST -L "^(phase3)$" -LE "^crash$")
endif()
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
phase_script_case(affected_jobs PHASES "2 3 4 5 17 18 19 20" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_jobs.txt CTEST -L "^(phase2|phase3|phase4|phase5|phase17|phase18|phase19|phase20)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_runner PHASES "5 6" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_runner.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_docgen_and_shaders PHASES "3 4 5" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_docgen_shaders.txt CTEST -L "^(phase3|phase4|phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_notes_only PHASES 5 ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_notes.txt CTEST -L "^(phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_foundation_source PHASES "0 1 2 3 4 5 7 9 10 11" DISPLAY included ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_foundation_source.txt CTEST -L "^(phase0|phase1|phase2|phase3|phase4|phase5|phase7|phase9|phase10|phase11)$" ${PLAIN})
# Escalations to the whole suite.
phase_script_case(affected_foundation_header PHASES all DISPLAY included ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_foundation_header.txt CTEST ${PLAIN})
phase_script_case(affected_unknown_directory PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_unknown.txt CTEST ${PLAIN})
phase_script_case(affected_cmake_file PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_cmake.txt CTEST ${PLAIN})
phase_script_case(affected_phase_map PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_map.txt CTEST ${PLAIN})
# A build or map file whose change only adds lines does not widen the run (a new module or test must not cost a full run); the same file
# without the additive mark does (affected_cmake_file, affected_phase_map above). tools/docgen/CMakeLists.txt is never a reason.
phase_script_case(affected_additive_phase_map PHASES "5 6" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_map.txt --additive-from-file ${FIX}/additive_map.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_additive_cmake_file PHASES "4 5" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_cmake.txt --additive-from-file ${FIX}/additive_cmake.txt CTEST -L "^(phase4|phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_additive_root_cmake PHASES "5 6" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_root_cmake.txt --additive-from-file ${FIX}/diff_root_cmake.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_root_cmake_not_additive PHASES all ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_root_cmake.txt CTEST ${PLAIN})
phase_script_case(affected_docgen_cmake PHASES "3 4 5" ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_docgen_cmake.txt CTEST -L "^(phase3|phase4|phase5)$" ${PLAIN_NO_DISPLAY})
# Side phases (Phase 26 Day 302): --affected adds a side phase this build has tests of, marked "(side)"; one it has none of is not added; one
# already selected is not marked; --current does not add them. Phase 6 stands in for the side phase: every platform builds its tests.
phase_script_case(affected_side_phase PHASES "5 6 (side)" SIDE ${FIX}/side_6.txt ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_notes.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
# A side phase with no tests in the run's flavor is not added (phase 6 has plain tests only; Day 302 review). The flavor is asan, which every
# platform runs (Windows refuses tsan).
phase_script_case(affected_side_phase_wrong_flavor PHASES 5 SIDE ${FIX}/side_6.txt ARGS --affected --flavor asan --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_notes.txt CTEST -L "^(phase5)$" -L "^asan$" -LE "^(display|crash)$")
phase_script_case(affected_side_phase_not_built PHASES 5 SIDE ${FIX}/side_99.txt ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_notes.txt CTEST -L "^(phase5)$" ${PLAIN_NO_DISPLAY})
phase_script_case(affected_side_phase_already_selected PHASES "5 6" SIDE ${FIX}/side_6.txt ARGS --affected --current-phase-file ${FIX}/phase5.txt
  --diff-from-file ${FIX}/diff_runner.txt CTEST -L "^(phase5|phase6)$" ${PLAIN_NO_DISPLAY})
phase_script_case(current_ignores_side_phases PHASES 5 DISPLAY excluded SIDE ${FIX}/side_6.txt ARGS --current --current-phase-file ${FIX}/phase5.txt
  CTEST -L "^(phase5)$" ${PLAIN_NO_DISPLAY})
add_test(NAME test_phase_script_bad_side_phases
  COMMAND ${CMAKE_COMMAND} -DBASH=${BASH_PROGRAM} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} -DSIDE_FILE=${FIX}/bad.txt
    "-DARGS=--affected;--current-phase-file;${FIX}/phase5.txt;--diff-from-file;${FIX}/diff_notes.txt" "-DEXPECT_FAIL_TEXT=not a phase number"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
list(APPEND PHASE_SCRIPT_TESTS test_phase_script_bad_side_phases)
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
# The flavors of a phase add up to all of its tests: the label expressions neither overlap nor leave one out. On Windows the flavors
# are plain and asan, over the phases that have asan tests there.
if(ENGINE_PLATFORM STREQUAL "windows")
  set(_partition_phases 0 10 13)
  set(_partition_flavors plain,asan)
elseif(ENGINE_PLATFORM STREQUAL "macos")
  set(_partition_phases 0 16 26)
  set(_partition_flavors plain,asan,tsan)
else()
  set(_partition_phases 2 4 5)
  set(_partition_flavors plain,asan,tsan)
endif()
foreach(phase IN LISTS _partition_phases)
  add_test(NAME test_phase_script_flavors_partition_${phase}
    COMMAND ${CMAKE_COMMAND} -DBASH=${BASH_PROGRAM} -DSCRIPT=${TEST_PHASE_SCRIPT} -DBUILD=${CMAKE_BINARY_DIR} -DPHASE=${phase}
      -DFLAVORS=${_partition_flavors} -P ${CMAKE_CURRENT_SOURCE_DIR}/partition.cmake)
  list(APPEND PHASE_SCRIPT_TESTS test_phase_script_flavors_partition_${phase})
endforeach()

engine_label_tests(${PHASE_SCRIPT_TESTS})
