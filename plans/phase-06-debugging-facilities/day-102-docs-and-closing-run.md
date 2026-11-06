# Phase 6 / Day 102 — Documentation Sweep, Closing Full Run & Wrap-up

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 91-101.

Finish the guide, run everything, measure, and close the phase.

## Tasks

- [ ] Guide: `docs/guide/foundation-debugging.md` read end to end as a new developer would: install the handler, read a report, resolve file and line, enable and open a core dump, read a minidump, use `ENSURE` versus `ASSERT`, write a test that expects a crash; every tagged sample compiles and runs; `docs/guide/README.md` lists the page; a short "Debugging a crash" checklist at the top.
- [ ] Update `CLAUDE.md` (the `Fails:`/`Aborts:` rule now names the macro family) and `STATUS.md` build/test commands (the new `tests/debug` suite, the scripts); remove any `KNOWN-ISSUES.md` entry this phase fixed; make sure every deferral is filed: the own DWARF line reader, other threads' stacks in minidumps, a `.eh_frame` fallback unwinder, the Windows (`MiniDumpWriteDump`, SEH) and macOS (Mach exceptions) backends for Phases 8 and 9, and any manual check no human has done (debugger stop location; `minidump-stackwalk`, if not tried).
- [ ] Windows/macOS design check: list, in the design spec, which interfaces a backend implements (`stack_walk`, `module_map`, `crash_handler`, `core_dump`, `death_test`, minidump context filler) and confirm none of the public headers names a Linux type (`ucontext_t`, signal numbers) outside `os/linux/`; amend the Phase 8 and Phase 9 files with the backend list.
- [ ] Measurements, recorded in this file: the per-log-line cost of the ring buffer, the cost of `-fno-omit-frame-pointer` on `renderer_bench`, the time to write a text report and a minidump (stack cap 256 KB), the size of a typical `.dmp`, and the walk cost per frame.
- [ ] **Planted regressions**, to prove the tests catch breakage (restored afterwards): make the handler call `malloc` (the safety gate fails); drop the re-raise (the exit-status test fails); remove the bounds check in the walker (the corrupted-frame test faults); shift one offset in the minidump writer by 4 bytes (the reader or the structure test fails); skip the ring buffer's torn-slot check (the TSan test fails). Record which were caught and which were not; uncaught ones go to `KNOWN-ISSUES.md`.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, and both demos for 1000 frames on Wayland and X11 with validation on and silent (a locked session breaks that check, see `KNOWN-ISSUES.md`); record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [ ] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 91-102 as committed), fix or decline each finding with a reason, and record it under "Post-implementation code review".
- [ ] Update `plans/phase-06-debugging-facilities.md` (status complete, the days list checked), `PLAN.md` (roadmap row, progress log entry), `STATUS.md`; change `CURRENT_PHASE` to `7` only in the commit that starts Phase 7, and make sure Phase 7's day files are written first (policy); commit.

**Done:** the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, docs 4/4 or more, both demos clean on both display backends, the planted regressions' results recorded, every deferral filed in `KNOWN-ISSUES.md`, and Phase 6 marked complete.
