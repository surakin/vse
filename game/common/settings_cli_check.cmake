# Command-line checks of the demos' settings options (Phase 11 Day 138). Run as:
#   cmake -DEXE=<program> -DMODE=<golden|regex|reproduce> -DARGS="a;b" [-DENV="K=V;..."] [-DGOLDEN=<file>] [-DEXPECT=<regex>] [-DWORK=<dir>] -P settings_cli_check.cmake
# The program runs with an empty environment (env -i), a private empty XDG_CONFIG_HOME, and only the variables in ENV, so the developer's own settings and
# variables cannot change the output. WORK is a prefix: the checks of one program run in parallel, so each uses WORK_<hash of what it runs>.
cmake_minimum_required(VERSION 3.20)
if(NOT WORK)
  set(WORK "${CMAKE_CURRENT_BINARY_DIR}/settings_cli_work")
endif()
# Tests of one program run in parallel under ctest: each gets a directory of its own, named by what it runs.
string(MD5 work_hash "${EXE}|${MODE}|${ARGS}|${ENV}|${USER_FILE}|${GOLDEN}|${EXPECT}")
string(SUBSTRING "${work_hash}" 0 8 work_hash)
set(WORK "${WORK}_${work_hash}")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/xdg")

function(run out_stdout out_code)
  execute_process(COMMAND /usr/bin/env -i "XDG_CONFIG_HOME=${WORK}/xdg" "HOME=${WORK}" "PATH=/usr/bin:/bin" ${ENV} "${EXE}" ${ARGN}
                  OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE rc)
  string(REPLACE "${EXE}" "PROGRAM" so "${so}") # the program's path differs between machines
  string(REGEX REPLACE "\\[LogBuild\\][^\n]*\n" "" so "${so}") # and the build line carries the commit
  set(${out_stdout} "${so}" PARENT_SCOPE)
  set(${out_code} "${rc}" PARENT_SCOPE)
  set(last_stderr "${se}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "golden")
  run(out code ${ARGS})
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "exit code ${code}: ${last_stderr}")
  endif()
  file(READ "${GOLDEN}" expected)
  if(NOT out STREQUAL expected)
    file(WRITE "${WORK}/actual.txt" "${out}")
    message(FATAL_ERROR "the output differs from ${GOLDEN} (see ${WORK}/actual.txt; regenerate the golden if the change is intended)")
  endif()
elseif(MODE STREQUAL "regex")
  if(DEFINED USER_FILE) # a user settings file written before the run: USER_FILE is its text, APP the program's settings name
    file(MAKE_DIRECTORY "${WORK}/xdg/${APP}")
    file(WRITE "${WORK}/xdg/${APP}/settings.cfg" "${USER_FILE}")
  endif()
  run(out code ${ARGS})
  if(DEFINED EXPECT_CODE AND NOT code EQUAL EXPECT_CODE)
    message(FATAL_ERROR "exit code ${code}, expected ${EXPECT_CODE}: ${last_stderr}")
  endif()
  set(all "${out}\n${last_stderr}")
  if(NOT all MATCHES "${EXPECT}")
    message(FATAL_ERROR "the output does not match [${EXPECT}]:\n${all}")
  endif()
elseif(MODE STREQUAL "reproduce")
  # Run once with settings given on the command line and save them; run again reading only the saved file: the listing must show the same values,
  # now from the file.
  run(out code ${ARGS} "--save-settings=${WORK}/saved.cfg")
  if(NOT code EQUAL 0 OR NOT EXISTS "${WORK}/saved.cfg")
    message(FATAL_ERROR "--save-settings failed (${code}): ${last_stderr}")
  endif()
  file(READ "${WORK}/saved.cfg" saved)
  if(NOT saved MATCHES "${EXPECT}")
    message(FATAL_ERROR "the saved file does not match [${EXPECT}]:\n${saved}")
  endif()
  run(listing code "--settings" "${WORK}/saved.cfg" "--list-settings")
  if(NOT code EQUAL 0 OR NOT listing MATCHES "${EXPECT_LISTING}")
    message(FATAL_ERROR "the listing of the saved file does not match [${EXPECT_LISTING}]:\n${listing}")
  endif()
else()
  message(FATAL_ERROR "unknown MODE '${MODE}'")
endif()
