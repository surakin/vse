# Phase 16 / Day 217 — Port the Tests I: foundation, debug, jobs, io, settings, types

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 210-216.

Tests are part of the scope. Convert the test directories that exercise the foundation first; they were also the older `std_container_check` allow-list entries (`tests/debug/` 279 lines, `tests/foundation/` 10, `tests/jobs/` 33).

## Tasks

- [x] Port `tests/foundation`, `tests/mem`, `tests/log`, `tests/containers`, `tests/string`, `tests/math`, `tests/io`, `tests/types`, `tests/settings`, `tests/os`: threads, atomics, mutexes, memory and string functions, formatting, `std::string` in the old tests (use `String`/`StringBuilder`, `tests/support/test_text.h`).
- [x] Port `tests/debug` (279 offending lines, includes the death-test child protocol) and `tests/jobs` (33): the child protocol's use of `std::` goes through `os::`; keep the death tests' behavior (run the crash tests: `--flavor all` includes them).
- [x] Where a test deliberately compares against the standard library as an oracle (the new `tests/lang`, `sync`, `cstr`, `mem`, math and time tests), the oracle include is allowed in that test only, listed per file in the gate's allow list with the reason "oracle for the wrapper".
- [x] Lower and remove the matching `std_container_check` allow-list entries in this commit (`tests/debug/`, `tests/foundation/`, `tests/jobs/`).
- [x] Run these directories plain, asan, tsan; the test count does not drop (record `ran X of Y` before and after).
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the listed test directories contain no standard include or `std::` except the documented oracle files; allow-list entries removed; test counts unchanged.

## Result

- Ported by four agents in parallel (git worktrees, merged): `tests/debug` (Linux and neutral files; support header `tests/debug/support/test_support_linux.h`), `tests/rhi`, `tests/jobs`, `tests/foundation`, `tests/support`, `tests/shader_mint`. Test count unchanged (5054 before and after the ports); the Windows-only test files wait for Day 219.
