# Phase 6 / Day 102 — Documentation Sweep, Closing Full Run & Wrap-up

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 91-101.

Finish the guide, run everything, measure, and close the phase.

## Tasks

- [x] Guide: `docs/guide/foundation-debugging.md` read end to end as a new developer would: install the handler, read a report, resolve file and line, enable and open a core dump, read a minidump, use `ENSURE` versus `ASSERT`, write a test that expects a crash; every tagged sample compiles and runs; `docs/guide/README.md` lists the page; a short "Debugging a crash" checklist at the top.
- [x] Update `CLAUDE.md` (the `Fails:`/`Aborts:` rule now names the macro family) and `STATUS.md` build/test commands (the new `tests/debug` suite, the scripts); remove any `KNOWN-ISSUES.md` entry this phase fixed; make sure every deferral is filed (PDB support for Windows builds, the Breakpad symbol path if not exercised, the debugger stop-location check if not done, the console crash libraries for Phase 20, and the Windows, macOS, Android and iOS backends listed in Phases 10, 11 and 19); filing anything that is not done.
- [x] Readiness re-check against what was built: re-run the neutral-header and library-boundary gate, the cross-target syntax checks, the backend-list check and the conformance listing of Day 101; confirm `engine/foundation/os/windows/debug/BACKEND.md`, `os/macos/debug/BACKEND.md` and the Phase 10 and 11 plan files still match the final headers; confirm the guide `docs/guide/porting-debugging.md` matches the code.
- [x] Measurements, recorded in this file: the per-log-line cost of the ring buffer, the time and size of the vendored libraries' build (files, lines, build seconds, binary size growth) and of a crash (time from fault to dump on disk), the size of a typical `.dmp`, and the cost of capturing and resolving a trace.
- [x] **Planted regressions**, to prove the tests catch breakage (restored afterwards): make our crash-path code call `malloc` (the safety gate fails); drop the original-fault re-raise or break the chaining (the exit-status test fails); include a library header from a second file (the boundary gate fails); link `libcurl` into the demo (the no-upload test fails); skip the ring buffer's torn-slot check (the TSan test fails); make the report hook allocate (the zero-allocation test fails). Record which were caught and which were not; uncaught ones go to `KNOWN-ISSUES.md`.
- [x] **Closing full run (CLAUDE.md, required for a phase's last day):** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, and both demos for 1000 frames on Wayland and X11 with validation on and silent (a locked session breaks that check, see `KNOWN-ISSUES.md`); record the final `ran X of Y tests (phases ...)` line in this file; nothing left out.
- [x] **End-of-phase review:** `/code-review high` over the whole phase's diff (Days 91-102 as committed), fix or decline each finding with a reason, and record it under "Post-implementation code review".
- [x] Update `plans/phase-06-debugging-facilities.md` (status complete, the days list checked), `PLAN.md` (roadmap row, progress log entry), `STATUS.md`; `CURRENT_PHASE` stays 6: it changes to 7 only in the commit that starts Phase 7, whose day files do not exist yet (policy); commit.

**Done:** the readiness re-check passes, the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing, docs 4/4 or more, both demos clean on both display backends, the planted regressions' results recorded, every deferral filed in `KNOWN-ISSUES.md`, and Phase 6 marked complete.


## Result

**Closing full run (final code):** `ran 2371 of 2371 tests (phases: all; flavors: all; display: included) in 22 s`, `100% tests passed out of 2371`, with 9
tests skipped by design (listed in `KNOWN-ISSUES.md`: the stripped-binary trace test in the normal entries, core-dump and throughput
tests in the sanitizer flavors, one TSan allocation test, the X11 layout test). `ctest -L docs`: 4 of 4. Phase 6 alone: 495 tests,
also passing in the non-unity, clang, Release and Release-with-checks builds and 10 loaded repeats. `docgen lint` 2700/2700.

**Demos (1000 frames, `--validation --no-vsync`, final build):** `rhi_demo` and `renderer_demo` on Wayland and on X11: exit 0, no
validation, error or warning lines, resident memory flat (rhi_demo 62324 to 62380 KiB on Wayland and 59068 to 59088 KiB on X11;
renderer_demo 70300 to 70332 and 65376 to 65404 KiB). The first log line of each run is the new `[LogCore] INFO: build <id> ...`.

**Readiness re-check:** the neutral-header and library-boundary gate (`platform_neutral_check`), `debug_cross_syntax_windows` and
`debug_cross_syntax_macos`, `debug_backend_lists` and the conformance file all pass on the final tree; `BACKEND.md` for both
platforms, the Phase 10, 11, 19 and 20 files and `docs/guide/porting-debugging.md` were re-read against the final headers.

**Measurements** (this machine: AMD Ryzen AI 7 350, Linux 7.2, GCC; Release unless stated):
- Ring buffer: one `Log` call to a discarding sink costs 33-50 ns including the ring append (49.9 ns after the single-writer claim was added; target was under 100 ns).
- Trace: `InitDebugFacilities` 1.2 ms once; capturing a 5-frame trace 430 ns; resolving it with names and lines, warm, 0.4 us; formatting 0.5 us. (A 5-frame trace is what the benchmark's call chain gave; resolution of cold frames reads debug information once and costs more.)
- A crash: from process start to the process being dead with the report printed, the dump on disk and the original signal, 63-112 ms (`rhi_bench --crash-test=segv`: 63-84 ms Release, 78-112 ms Debug), 10 runs each. A `.envelope` with the minidump and the attached report is 67-71 KB.
- Vendored libraries: libbacktrace 12 source files, 14,179 lines (516 KB tree); sentry-native with Breakpad and libunwind 642 source and header files, about 144,600 lines (6.8 MB tree). Clean Release build of `backtrace`, `sentry`, `breakpad_client`, `unwind`: 15.2 s on one core (2.6 s with 16). Static libraries: libbacktrace 151 KB, libsentry 1,002 KB, libbreakpad_client 279 KB, libunwind 176 KB.
- Binary growth: Release `rhi_bench` was 751 KB at the commit before Phase 6 and is 1,689 KB now (+938 KB).

**Planted regressions** (each restored and verified identical afterwards):
1. `malloc` in the report writer: caught by `signal_safety_check` and by `Writing a crash report allocates nothing...`.
2. The chain to the library dropped: caught (the abort and assert program tests fail; no dump, original signal lost).
3. `<sentry.h>` included from a second file: caught by `platform_neutral_check`.
4. `libcurl` linked into `rhi_bench`: caught by `debug_no_network_rhi_bench` ("links a network library: libcurl.so.4").
5. The ring reader's torn-slot re-check removed: caught by the 8-thread test in all three flavors.
6. `malloc` in `OnCrashEvent`: caught by `signal_safety_check` only (no allocation-count test runs that function; filed in `KNOWN-ISSUES.md`).

## Post-implementation code review (end of phase)

`/code-review high` over `4e9fce6..HEAD` (Days 91-101 as committed, third-party and generated files skipped) found 10 issues.

1. The report resolved the whole trace before writing a frame, against its comment. **Fixed**: frame by frame, numbered, 2 KB buffer (this also removes the 8 KB stack buffer noted on Day 97); the report test checks the numbering.
2. A blocked write to a full pipe could hold a crashing process forever. **Fixed**: `os::WriteAllWithTimeout` (poll, small chunks, 2 s); test with a full pipe.
3. After an uninstall with another handler installed over the recorder, the recorder's chain pointed at the closed library. **Fixed**: the saved chain is reset to the default action.
4. The "already reported" flag was never cleared. **Fixed**: reset on install and remove.
5. A signal sent with `kill` or `raise` while the previous handler was the default let the program carry on and dropped the recorder. **Fixed**: a signal that is not a hardware fault is raised again with the default action.
6. An `ENGINE_ENSURE` inside the Vulkan validation callback can run a trace inside driver code. **Declined**: the plan asks for an ensure-style report on validation errors; it reports once, only with validation on (a development setting), and the log keeps every error.
7. Uninstalling while another thread reports. **Fixed**: uninstall waits (bounded) for a report in progress before closing the library.
8. A child finishing just before the death-test deadline could be reported as both exited and timed out. **Fixed**: `timed_out` stays set only if our kill ended it.
9. The bounded-copy and digit-formatting helpers exist in several files. **Declined again** (also Day 96): each copy is a few lines in a different layer; a shared signal-safe text helper would be a refactor of its own.
10. Crash state kept the caller's name pointer. **Fixed**: the name is copied (31 characters) and the provider uses the copy; test. The reset helper detaching a cached counter stays a test-only limitation, documented.

Not done by this review, and recorded: the second half of finding 8's second scenario (a normal exit leaves grandchildren in the process group unkilled) is a known limit of the death-test helper.
