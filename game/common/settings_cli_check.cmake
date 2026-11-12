# Command-line checks of the demos' settings options (Phase 11 Day 138). Run as:
#   cmake -DEXE=<program> -DMODE=<golden|regex|reproduce> -DARGS="a;b" [-DENV="K=V;..."] [-DGOLDEN=<file>] [-DEXPECT=<regex>] [-DWORK=<dir>] -P settings_cli_check.cmake
# The program runs with an empty environment (env -i), a private empty XDG_CONFIG_HOME, and only the variables in ENV, so the developer's own settings and
# variables cannot change the output. WORK is a prefix: the checks of one program run in parallel, so each uses WORK_<hash of what it runs>.
# On Windows (Day 161) every variable is removed except the few a process needs to start, APPDATA (the user's settings) and LOCALAPPDATA (the crash
# handler's directory) point into WORK, and the output's line ends are compared as \n.
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

if(CMAKE_HOST_WIN32)
  # `cmake -E env` cannot start from an empty environment: remove each variable but these, by name (names starting with `=` are the per-drive
  # directories Windows keeps, not variables).
  set(keep "SystemRoot;SystemDrive;windir;TEMP;TMP;NUMBER_OF_PROCESSORS;PROCESSOR_ARCHITECTURE")
  execute_process(COMMAND ${CMAKE_COMMAND} -E environment OUTPUT_VARIABLE environment_text)
  string(REPLACE ";" "\\;" environment_text "${environment_text}")
  string(REPLACE "\n" ";" environment_lines "${environment_text}")
  set(unset_args "")
  foreach(line IN LISTS environment_lines)
    if(line MATCHES "^([^=]+)=")
      set(name "${CMAKE_MATCH_1}")
      string(TOUPPER "${name}" upper)
      set(kept FALSE)
      foreach(k IN LISTS keep)
        string(TOUPPER "${k}" k)
        if(upper STREQUAL k)
          set(kept TRUE)
        endif()
      endforeach()
      if(NOT kept)
        list(APPEND unset_args "--unset=${name}")
      endif()
    endif()
  endforeach()
  file(TO_NATIVE_PATH "${EXE}" native_exe)
endif()

function(run out_stdout out_code)
  if(CMAKE_HOST_WIN32)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env ${unset_args} "APPDATA=${WORK}/xdg" "LOCALAPPDATA=${WORK}/local" "PATH=$ENV{SystemRoot}/System32" ${ENV}
                            "${EXE}" ${ARGN}
                    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE rc)
    string(REPLACE "\r\n" "\n" so "${so}")
    string(REPLACE "\r\n" "\n" se "${se}")
    string(REPLACE "${native_exe}" "PROGRAM" so "${so}")
  else()
    execute_process(COMMAND /usr/bin/env -i "XDG_CONFIG_HOME=${WORK}/xdg" "HOME=${WORK}" "PATH=/usr/bin:/bin" ${ENV} "${EXE}" ${ARGN}
                    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE rc)
  endif()
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
    # Where the program looks for it: $XDG_CONFIG_HOME on Linux, %APPDATA% on Windows (both ${WORK}/xdg here), and on macOS
    # $HOME/Library/Application Support, with HOME=${WORK} (Phase 26 Day 314).
    set(user_dir "${WORK}/xdg/${APP}")
    if(CMAKE_HOST_APPLE)
      set(user_dir "${WORK}/Library/Application Support/${APP}")
    endif()
    file(MAKE_DIRECTORY "${user_dir}")
    file(WRITE "${user_dir}/settings.cfg" "${USER_FILE}")
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
