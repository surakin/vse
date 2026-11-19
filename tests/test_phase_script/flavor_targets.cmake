# Checks that every test's programs are built by its flavor's target (maintenance "shorter test runs", Day 1).
# scripts/test-phase.sh builds only engine_tests_<flavor> for the flavors a run tests (cmake/EngineFlavorTargets.cmake), so a test
# that runs a program another flavor's target builds would run a stale binary, or none.
#
#   cmake -DLISTING=<ctest -N -V output> -DTARGETS=<engine_flavor_targets.txt> [-DBUILD=<dir>] -P flavor_targets.cmake
#   cmake -DBUILD=<build dir> -DCTEST=<ctest> -P flavor_targets.cmake      (makes the listing itself)
#
# A test's flavor is its `asan` or `tsan` label, else plain. Every word of its command that is the file of one of our executable
# targets must be a target of that flavor, or one engine_flavor_needs() added to it. A command whose program is in the build
# directory and is no target's file (nor one a target lists in ENGINE_FLAVOR_EXTRA_FILES) is reported too.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED LISTING)
  set(TARGETS "${BUILD}/engine_flavor_targets.txt")
  execute_process(COMMAND "${CTEST}" --test-dir "${BUILD}" -N -V OUTPUT_VARIABLE listing RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ctest -N -V failed in ${BUILD}")
  endif()
else()
  file(READ "${LISTING}" listing)
endif()
if(NOT EXISTS "${TARGETS}")
  message(FATAL_ERROR "no ${TARGETS}: configure the build first")
endif()

# flavor|target|file, one per line (a target may be listed under more than one flavor: engine_flavor_needs).
file(STRINGS "${TARGETS}" target_lines)
foreach(line IN LISTS target_lines)
  if(line MATCHES "^([a-z]+)\\|([^|]+)\\|(.+)$")
    set(file "${CMAKE_MATCH_3}")
    string(MAKE_C_IDENTIFIER "${file}" key)
    list(APPEND "flavors_${key}" "${CMAKE_MATCH_1}")
    set("target_${key}" "${CMAKE_MATCH_2}")
  endif()
endforeach()

string(REPLACE ";" "\\;" listing "${listing}")
string(REPLACE "\n" ";" lines "${listing}")
set(problems "")
set(count 0)
set(command "")
set(labels "")
foreach(line IN LISTS lines)
  if(line MATCHES "^[0-9]+: Test command: (.*)$")
    set(command "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^Labels: (.*)$")
    set(labels " ${CMAKE_MATCH_1} ")
  elseif(line MATCHES "^ *Test +#[0-9]+: (.+)$")
    set(name "${CMAKE_MATCH_1}")
    math(EXPR count "${count} + 1")
    set(flavor plain)
    if(labels MATCHES " asan ")
      set(flavor asan)
    elseif(labels MATCHES " tsan ")
      set(flavor tsan)
    endif()
    separate_arguments(words NATIVE_COMMAND "${command}")
    set(first TRUE)
    foreach(word IN LISTS words)
      string(REGEX REPLACE "^-D[A-Za-z0-9_]+=" "" value "${word}")  # -DX=<path> passes a program to a script
      string(MAKE_C_IDENTIFIER "${value}" key)
      if(DEFINED "target_${key}")
        if(NOT flavor IN_LIST "flavors_${key}")
          string(APPEND problems "\n  ${name} (${flavor}) runs ${target_${key}}, which engine_tests_${flavor} does not build")
        endif()
      elseif(first AND DEFINED BUILD)
        string(FIND "${value}" "${BUILD}/" at)  # a prefix test with no regex: the path may hold metacharacters
        if(at EQUAL 0)
          string(APPEND problems "\n  ${name}: its program ${value} is no target's file")
        endif()
      endif()
      set(first FALSE)
    endforeach()
    set(command "")
    set(labels "")
  endif()
endforeach()

if(count EQUAL 0)
  message(FATAL_ERROR "the listing has no tests")
endif()
if(problems)
  message(FATAL_ERROR "tests whose programs their flavor's build target does not build (scripts/test-phase.sh would run them stale; "
    "add the program to the flavor with engine_flavor_needs in cmake/EngineFlavorTargets.cmake's caller):${problems}")
endif()
message(STATUS "flavor targets: ${count} tests, every program built by its flavor's target")
