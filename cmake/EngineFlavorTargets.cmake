# One build target per sanitizer flavor (maintenance "shorter test runs", Day 1): engine_tests_plain, engine_tests_asan and
# engine_tests_tsan. scripts/test-phase.sh builds only the flavors a run tests, so a plain run compiles no `_asan` or `_tsan`
# object. The root CMakeLists.txt includes this file early (for engine_flavor_needs and engine_flavor_label). The include
# defers engine_add_flavor_targets() to the end of the top-level directory (the last lines of this file), so it runs after
# every directory has made its targets on every platform branch, including those that end in return() (macOS and Windows:
# an explicit call at the root file's end missed them; shorter test runs, Day 5).
#
# A target belongs to a flavor by its name: one with `asan` or `tsan` as a word of it (`ecs_tests_asan`, `tsan_canary`) is that flavor's,
# every other target of ours is plain's. Only the targets under our own directories count (third_party/ comes in through the
# links). The list of every target with its flavor and its file is written to <build>/engine_flavor_targets.txt, which the ctest
# test_phase_script_flavor_targets reads to check that every test's program is built by its flavor's target.

# A program a target's POST_BUILD step makes beside it (a stripped copy) is listed with the target's property
# ENGINE_FLAVOR_EXTRA_FILES, so the check knows which target builds it:
#   set_property(TARGET debug_tests APPEND PROPERTY ENGINE_FLAVOR_EXTRA_FILES "$<TARGET_FILE:debug_tests>.stripped")

# engine_flavor_needs(<flavor> <target>...)
# Adds targets of another flavor to <flavor>'s build target: a program an asan or tsan test runs that is not itself sanitized (a
# minter, a plain helper). The ctest test_phase_script_flavor_targets names the ones missing.
function(engine_flavor_needs flavor)
  set_property(GLOBAL APPEND PROPERTY ENGINE_FLAVOR_NEEDS_${flavor} ${ARGN})
endfunction()

# engine_flavor_label(<target> <out> <label>...)
# The labels, plus `asan` or `tsan` when <target> is of that flavor: for a test that inspects a sanitized program without being
# one, so it runs in the flavor whose build target makes the program.
function(engine_flavor_label target out)
  engine_flavor_of(${target} flavor)
  set(labels ${ARGN})
  if(NOT flavor STREQUAL "plain")
    list(APPEND labels ${flavor})
  endif()
  set(${out} "${labels}" PARENT_SCOPE)
endfunction()

function(engine_flavor_collect_targets dir out)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    engine_flavor_collect_targets("${sub}" more)
    list(APPEND targets ${more})
  endforeach()
  set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(engine_flavor_of target out)
  if(target MATCHES "(^|_)asan(_|$)")
    set(${out} asan PARENT_SCOPE)
  elseif(target MATCHES "(^|_)tsan(_|$)")
    set(${out} tsan PARENT_SCOPE)
  else()
    set(${out} plain PARENT_SCOPE)
  endif()
endfunction()

function(engine_add_flavor_targets)
  set(plain_targets "")
  set(asan_targets "")
  set(tsan_targets "")
  set(lines "")
  engine_flavor_collect_targets("${CMAKE_SOURCE_DIR}" everything)
  foreach(t IN LISTS everything)
    get_target_property(src_dir ${t} SOURCE_DIR)
    string(FIND "${src_dir}/" "${CMAKE_SOURCE_DIR}/third_party/" at)  # a prefix test with no regex: paths may hold metacharacters
    if(at EQUAL 0)
      continue()
    endif()
    get_target_property(type ${t} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    get_target_property(excluded ${t} EXCLUDE_FROM_ALL)
    if(excluded)
      continue()  # not part of `all` either (docs and the like are built on request)
    endif()
    engine_flavor_of(${t} flavor)
    list(APPEND ${flavor}_targets ${t})
    if(type STREQUAL "EXECUTABLE")
      string(APPEND lines "${flavor}|${t}|$<TARGET_FILE:${t}>\n")
    endif()
    get_target_property(extra ${t} ENGINE_FLAVOR_EXTRA_FILES)
    if(extra)
      foreach(file IN LISTS extra)
        string(APPEND lines "${flavor}|${t}|${file}\n")
      endforeach()
    endif()
  endforeach()
  foreach(flavor plain asan tsan)
    get_property(needs GLOBAL PROPERTY ENGINE_FLAVOR_NEEDS_${flavor})
    foreach(t IN LISTS needs)
      list(APPEND ${flavor}_targets ${t})
      get_target_property(type ${t} TYPE)
      if(type STREQUAL "EXECUTABLE")
        string(APPEND lines "${flavor}|${t}|$<TARGET_FILE:${t}>\n")
      endif()
    endforeach()
    add_custom_target(engine_tests_${flavor})
    if(${flavor}_targets)
      add_dependencies(engine_tests_${flavor} ${${flavor}_targets})
    endif()
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/engine_flavor_targets.txt" CONTENT "${lines}")
endfunction()

# Runs when the top-level CMakeLists.txt is done, however it ends (a platform branch's return() included).
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL engine_add_flavor_targets)
