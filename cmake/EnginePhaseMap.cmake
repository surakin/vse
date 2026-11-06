# Reads tests/phases.txt, the one place a test's phase is decided (Phase 5; docs/superpowers/specs/2026-11-05-test-scoping-design.md).
#
# Lines are `<pattern>  <phase>`; `#` starts a comment. A pattern is
#   - a source path relative to the repo root: `tests/foundation/log_test.cpp`; `*` matches within one path segment, and a
#     trailing `/` means every file below that directory;
#   - `test:<glob>`: the name of a test that is not a Catch2 test case (`*` matches any characters).
# A phase is 0, 1, 2, 2.5, 3, ...; its ctest label is `phase<N>` with `.` written `_` (`phase2_5`), because label
# filters are regular expressions. A subject matching no pattern, or patterns with different phases, is an error.
#
# Used by the vendored CatchAddTests.cmake (via `PHASE_MAP`) and by engine_label_tests() in EngineTestPhases.cmake.

# Loads the map into the lists _EPM_KINDS, _EPM_PATTERNS, _EPM_REGEXES and _EPM_PHASES in the caller's scope.
function(engine_phase_map_load map_file)
  set(kinds "")
  set(patterns "")
  set(regexes "")
  set(phases "")
  file(STRINGS "${map_file}" lines)
  foreach(line IN LISTS lines)
    string(REGEX REPLACE "#.*$" "" line "${line}")
    string(STRIP "${line}" line)
    if(line STREQUAL "")
      continue()
    endif()
    if(NOT line MATCHES "^([^ \t]+)[ \t]+([0-9]+(\\.[0-9]+)?)$")
      message(FATAL_ERROR "${map_file}: cannot read the line '${line}' (want '<pattern>  <phase>')")
    endif()
    set(pattern "${CMAKE_MATCH_1}")
    set(phase "${CMAKE_MATCH_2}")
    if(pattern MATCHES "^test:(.*)$")
      set(kind test)
      set(pattern "${CMAKE_MATCH_1}")
      set(any ".*")
    else()
      set(kind file)
      set(any "[^/]*")
    endif()
    string(REGEX REPLACE "([][+.?()^$\\\\|{}])" "\\\\\\1" regex "${pattern}")
    string(REPLACE "*" "${any}" regex "${regex}")
    if(kind STREQUAL "file" AND regex MATCHES "/$")
      set(regex "${regex}.*")
    endif()
    list(APPEND kinds ${kind})
    list(APPEND patterns "${pattern}")
    list(APPEND regexes "^${regex}$")
    list(APPEND phases ${phase})
  endforeach()
  set(_EPM_KINDS "${kinds}" PARENT_SCOPE)
  set(_EPM_PATTERNS "${patterns}" PARENT_SCOPE)
  set(_EPM_REGEXES "${regexes}" PARENT_SCOPE)
  set(_EPM_PHASES "${phases}" PARENT_SCOPE)
endfunction()

# Sets <out> to the phase of <subject> (a repo-relative path for kind `file`, a test name for kind `test`), using the
# lists engine_phase_map_load() left in the caller's scope. Sets it empty and returns false in <found> when no
# pattern matches; stops with an error when patterns disagree.
function(engine_phase_lookup kind subject out found)
  set(result "")
  list(LENGTH _EPM_REGEXES count)
  if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
      list(GET _EPM_KINDS ${i} k)
      list(GET _EPM_REGEXES ${i} re)
      if(k STREQUAL kind AND subject MATCHES "${re}")
        list(GET _EPM_PHASES ${i} p)
        if(result STREQUAL "")
          set(result "${p}")
        elseif(NOT result STREQUAL p)
          message(FATAL_ERROR "tests/phases.txt gives '${subject}' two phases (${result} and ${p})")
        endif()
      endif()
    endforeach()
  endif()
  set(${out} "${result}" PARENT_SCOPE)
  if(result STREQUAL "")
    set(${found} FALSE PARENT_SCOPE)
  else()
    set(${found} TRUE PARENT_SCOPE)
  endif()
endfunction()

# The ctest label for a phase: 2.5 -> phase2_5.
function(engine_phase_label phase out)
  string(REPLACE "." "_" p "${phase}")
  set(${out} "phase${p}" PARENT_SCOPE)
endfunction()
