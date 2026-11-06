# Phase 6 / Day 102 — Documentation Sweep, Closing Full Run & Wrap-up

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 91-101.

Finish the guide, run everything, measure, and close the phase.

## Tasks

- [ ] Guide: `docs/guide/foundation-debugging.md` read end to end as a new developer would: install the handler, read a report, resolve file and line, enable and open a core dump, read a minidump, use `ENSURE` versus `ASSERT`, write a test that expects a crash; every tagged sample compiles and runs; `docs/guide/README.md` lists the page; a short "Debugging a crash" checklist at the top.
- [ ] Update `CLAUDE.md` (the `Fails:`/`Aborts:` rule now names the macro family) and `STATUS.md` build/test commands (the new `tests/debug` suite, the scripts); remove any `KNOWN-ISSUES.md` entry this phase fixed; make sure every deferral is filed (PDB support for Windows builds, the Breakpad symbol path if not exercised, the debugger stop-location check if not done, the console crash libraries for Phase 18, and the Windows, macOS, Android and iOS backends listed in Phases 8, 9 and 17); filing anything that is not done.
- [ ] Readiness re-check against what was built: re-run the neutral-header and library-boundary gate, the cross-target syntax checks, the backend-list check and the conformance listing of Day 101; confirm `engine/foundation/os/windows/debug/BACKEND.md`, `os/macos/debug/BACKEND.md` and the Phase 8 and 9 plan files still match the final headers; confirm the guide `docs/guide/porting-debugging.md` matches the code.
- [ ] Measurements, recorded in this file: the per-log-line cost of the ring buffer, the time and size of the vendored libraries' build (files, lines, build seconds, binary size growth) and of a crash (time from fault to dump on disk), the size of a typical `.dmp`, and the cost of capturing and resolving a trace.
- [ ] **Planted regressions**, to prove the tests catch breakage (restored afterwards): make our crash-path code call `malloc` (the safety gate fails); drop the original-fault re-raise or break the chaining (the exit-status test fails); include a library header from a second file (the boundary gate fails); link `libcurl` into the demo (the no-upload test fails); skip the ring buffer's torn-slot check (the TSan test fails); make the report hook allocate (the zero-allocation test fails). Record which were caught and which were not; uncaught ones go to `KNOWN-ISSUES.md`.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, and both demos for 1000 frames on Wayland and X11 with validation on and silent (a locked session breaks that check, see `KNOWN-ISSUES.md`); record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [ ] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 91-102 as committed), fix or decline each finding with a reason, and record it under "Post-implementation code review".
- [ ] Update `plans/phase-06-debugging-facilities.md` (status complete, the days list checked), `PLAN.md` (roadmap row, progress log entry), `STATUS.md`; change `CURRENT_PHASE` to `7` only in the commit that starts Phase 7, and make sure Phase 7's day files are written first (policy); commit.

**Done:** the readiness re-check passes, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, docs 4/4 or more, both demos clean on both display backends, the planted regressions' results recorded, every deferral filed in `KNOWN-ISSUES.md`, and Phase 6 marked complete.
