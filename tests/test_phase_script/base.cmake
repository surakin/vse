# The base scripts/test-phase.sh --affected compares with (maintenance "shorter test runs", Day 2). Builds a scratch git repository
# and checks the rule the script names for each case:
#   1. no day start, no upstream: the last commit that changed CURRENT_PHASE;
#   2. an upstream branch: the merge-base with it;
#   3. a day start recorded with --start-day: that commit;
#   4. a recorded day start that is not an ancestor of HEAD: ignored with a message, the upstream rule instead;
#   5. a recorded day start older than the phase's start: ignored with a message;
#   6. --since: wins over the recorded day start;
#   7. a worktree's record stays in the worktree's own git directory;
#   8. a pushed HEAD with no day start: the upstream base is HEAD, and the script warns that pushed commits are left out;
#   9. --start-day with something that is not a commit: refused.
#
#   cmake -DBASH=<bash> -DSCRIPT=<test-phase.sh> -DBUILD=<build dir> -DGIT=<git> -DWORK=<scratch dir> -P base.cmake
cmake_minimum_required(VERSION 3.20)

file(REMOVE_RECURSE "${WORK}" "${WORK}-wt")
file(MAKE_DIRECTORY "${WORK}")
set(PHASE_FILE "${WORK}.phase")  # the current phase, read by the script (the repository's CURRENT_PHASE is only for git log)
file(WRITE "${PHASE_FILE}" "6\n")

function(git)
  # The user's global hooks, templates and signing stay out of the scratch repository.
  execute_process(COMMAND "${GIT}" -c user.name=test -c user.email=test@invalid -c commit.gpgsign=false
      -c "core.hooksPath=${WORK}/.no-hooks" -c init.templateDir= ${ARGN}
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "git ${ARGN} failed: ${err}")
  endif()
  set(git_out "${out}" PARENT_SCOPE)
endfunction()

function(commit name file text)
  get_filename_component(dir "${WORK}/${file}" DIRECTORY)
  file(MAKE_DIRECTORY "${dir}")
  file(APPEND "${WORK}/${file}" "${text}\n")
  git(add "${file}")
  git(commit -q -m "${name}")
  git(rev-parse HEAD)
  set(${name} "${git_out}" PARENT_SCOPE)
endfunction()

git(init -q)
git(symbolic-ref HEAD refs/heads/master)  # whatever init.defaultBranch says (git init -b needs 2.28)
commit(c_old engine/jobs/a.cpp "old")
commit(c_phase CURRENT_PHASE "6")
commit(c1 engine/jobs/a.cpp "one")
git(checkout -q -b side)
commit(c_side engine/jobs/a.cpp "side")
git(checkout -q master)
commit(c2 engine/jobs/a.cpp "two")

# Runs the script; checks that its output matches each regex in EXPECT (a list), or, with FAIL, that it fails with that text.
function(run_case name)
  cmake_parse_arguments(C "FAIL" "" "ARGS;EXPECT" ${ARGN})
  execute_process(COMMAND "${BASH}" "${SCRIPT}" --repo-root "${WORK}" --build-dir "${BUILD}" --current-phase-file "${PHASE_FILE}"
      ${C_ARGS}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(all "${out}${err}")
  if(C_FAIL AND rc EQUAL 0)
    message(FATAL_ERROR "case ${name}: expected a failure, got:\n${all}")
  elseif(NOT C_FAIL AND NOT rc EQUAL 0)
    message(FATAL_ERROR "case ${name}: the script failed (${rc}):\n${all}")
  endif()
  foreach(re IN LISTS C_EXPECT)
    if(NOT all MATCHES "${re}")
      message(FATAL_ERROR "case ${name}: expected '${re}' in:\n${all}")
    endif()
  endforeach()
endfunction()

string(SUBSTRING "${c_phase}" 0 12 phase12)
string(SUBSTRING "${c1}" 0 12 c1_12)
string(SUBSTRING "${c2}" 0 12 c2_12)
string(SUBSTRING "${c_old}" 0 12 old12)
string(SUBSTRING "${c_side}" 0 12 side12)
set(AFFECTED --affected --dry-run)

run_case(phase_start ARGS ${AFFECTED} EXPECT "compares with ${phase12}: the last commit that changed CURRENT_PHASE")

git(branch -q up ${c1})
git(config branch.master.remote .)
git(config branch.master.merge refs/heads/up)
run_case(upstream ARGS ${AFFECTED} EXPECT "compares with ${c1_12}: the merge-base with the upstream branch")

run_case(record ARGS --start-day ${c2} EXPECT "the day starts at ${c2_12}")
run_case(recorded ARGS ${AFFECTED} EXPECT "compares with ${c2_12}: the day start recorded by --start-day")

run_case(record_side ARGS --start-day ${c_side} EXPECT "the day starts at ${side12}")
run_case(not_ancestor ARGS ${AFFECTED}
  EXPECT "recorded day start ${side12} is not an ancestor of HEAD.*ignored" "compares with ${c1_12}: the merge-base")

run_case(record_old ARGS --start-day ${c_old} EXPECT "the day starts at ${old12}")
run_case(older_than_phase ARGS ${AFFECTED}
  EXPECT "recorded day start ${old12} is older than the phase's start.*ignored" "compares with ${c1_12}: the merge-base")

run_case(record_again ARGS --start-day EXPECT "the day starts at ${c2_12}")
run_case(since ARGS ${AFFECTED} --since ${c_old} EXPECT "compares with ${old12}: --since")

# A worktree keeps its own record: one made there does not move the main checkout's base.
git(worktree add -q --detach "${WORK}-wt" ${c1})
execute_process(COMMAND "${BASH}" "${SCRIPT}" --repo-root "${WORK}-wt" --start-day ${c1} OUTPUT_VARIABLE wt_out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT wt_out MATCHES "recorded in [^\n]*/worktrees/")
  message(FATAL_ERROR "case worktree: the record is not in the worktree's own git directory:\n${wt_out}")
endif()
run_case(main_unmoved ARGS ${AFFECTED} EXPECT "compares with ${c2_12}: the day start recorded by --start-day")
git(worktree remove --force "${WORK}-wt")

# Pushed with no day start: the upstream base is HEAD itself, and the script says the day's pushed commits are left out.
file(REMOVE "${WORK}/.git/test-phase-base")
git(branch -q -f up ${c2})
run_case(pushed_warns ARGS ${AFFECTED} EXPECT "HEAD is pushed and no day start is recorded" "compares with ${c2_12}: the merge-base")

run_case(bad_ref FAIL ARGS --start-day no-such-commit EXPECT "--start-day: 'no-such-commit' is not a commit")

message(STATUS "test-phase base: every rule chose as listed")
