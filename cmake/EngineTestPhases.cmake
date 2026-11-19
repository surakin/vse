# Phase labels for ctest (Phase 6). Every test carries exactly one `phaseN` label so a run can select a phase
# (`ctest -L '^phase5$'`). tests/phases.txt is the only place a test's phase is decided; see
# cmake/EnginePhaseMap.cmake for its format and docs/guide/building-and-testing.md for the workflow.
#
# Adding a Catch2 test file: add a line for it to tests/phases.txt. The build stops, naming the file, if it has none.
# Adding any other test (add_test): add a `test:<name>` line and call engine_label_tests(<name>) after add_test.
include(${CMAKE_CURRENT_LIST_DIR}/EnginePhaseMap.cmake)

set(ENGINE_PHASE_MAP ${CMAKE_SOURCE_DIR}/tests/phases.txt)

# engine_discover_tests(<target> [catch_discover_tests arguments...])
# catch_discover_tests() that also labels every discovered test with its phase, from the test's source file. Use this
# instead of calling catch_discover_tests directly. The phase label is added to the LABELS the caller passes. Pass labels
# in lower case: in a PROPERTIES list, the elements after LABELS up to the next upper-case name (SKIP_RETURN_CODE, ...)
# are labels, so an upper-case label would be read as a property name.
function(engine_discover_tests target)
  if(ENGINE_COMPILE_ONLY)
    return()  # discovery runs the test program after it links, which a compile-only build cannot (Phase 21 Day 311)
  endif()
  catch_discover_tests(${target} ${ARGN} PHASE_MAP "${ENGINE_PHASE_MAP}" PHASE_ROOT "${CMAKE_SOURCE_DIR}")
endfunction()

# engine_label_tests(<test name>...)
# Gives tests that are not Catch2 test cases their phase label, from their `test:<name>` lines in tests/phases.txt,
# keeping any labels they already have. Call it in the directory that defines the tests, after add_test.
function(engine_label_tests)
  engine_phase_map_load("${ENGINE_PHASE_MAP}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ENGINE_PHASE_MAP}")
  foreach(name IN LISTS ARGN)
    engine_phase_lookup(test "${name}" phase found)
    if(NOT found)
      message(FATAL_ERROR "tests/phases.txt has no 'test:' line for the test '${name}'")
    endif()
    engine_phase_label("${phase}" label)
    get_test_property(${name} LABELS labels)
    if(labels STREQUAL "NOTFOUND")
      set(labels "")
    endif()
    list(APPEND labels ${label})
    set_tests_properties(${name} PROPERTIES LABELS "${labels}")
  endforeach()
endfunction()

# engine_soak_tests(<test name>...)
# Adds the `soak` label to tests whose length is their point (a benchmark's long run, a stress run of a million steps): they run only
# with scripts/test-phase.sh --all or --soak, never in a scoped run. A test that is long only because of its count gets a short form
# for the ordinary run instead (docs/guide/building-and-testing.md, "Long tests"). Call it after add_test and engine_label_tests.
function(engine_soak_tests)
  foreach(name IN LISTS ARGN)
    get_test_property(${name} LABELS labels)
    if(labels STREQUAL "NOTFOUND")
      set(labels "")
    endif()
    list(APPEND labels soak)
    set_tests_properties(${name} PROPERTIES LABELS "${labels}")
  endforeach()
endfunction()

# engine_soak_catch_test(<target> <test case name> SOURCE <repo-relative source file> [SUFFIX <suffix>] [LABELS <label>...]
#                        [ENVIRONMENT <VAR=value>...])
# Registers the soak form of one Catch2 test case: the test program run on that case alone with ENGINE_TEST_SOAK=1
# (tests/support/soak.h), named "<case> [soak]", with the phase of its source file and the `soak` label. The ordinary form, which
# catch_discover_tests registers, runs the short count. ENVIRONMENT adds to the test's environment (give it what the discovered
# tests of <target> get, with its own scratch directories). SUFFIX goes after the case name, before " [soak]" (a sanitized
# target's " [asan]"), and LABELS are added (its flavor). Call it in the directory that defines <target>.
function(engine_soak_catch_test target name)
  cmake_parse_arguments(S "" "SOURCE;SUFFIX" "ENVIRONMENT;LABELS" ${ARGN})
  if(ENGINE_COMPILE_ONLY)
    return()
  endif()
  engine_phase_map_load("${ENGINE_PHASE_MAP}")
  engine_phase_lookup(file "${S_SOURCE}" phase found)
  if(NOT found)
    message(FATAL_ERROR "engine_soak_catch_test: tests/phases.txt has no line for '${S_SOURCE}'")
  endif()
  engine_phase_label("${phase}" label)
  # Catch2's command line reads a comma as a separator and brackets as a tag: escape them so the name selects this one case.
  string(REPLACE "\\" "\\\\" spec "${name}")
  string(REPLACE "," "\\," spec "${spec}")
  string(REPLACE "[" "\\[" spec "${spec}")
  set(test_name "${name}${S_SUFFIX} [soak]")
  add_test(NAME "${test_name}" COMMAND ${target} "${spec}")
  set(labels ${label} soak ${S_LABELS})
  set(environment ENGINE_TEST_SOAK=1 ${S_ENVIRONMENT})
  set_tests_properties("${test_name}" PROPERTIES ENVIRONMENT "${environment}" LABELS "${labels}" SKIP_RETURN_CODE 4)
endfunction()
