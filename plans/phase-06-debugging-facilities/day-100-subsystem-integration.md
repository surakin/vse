# Phase 6 / Day 100 — Wiring the Engine In

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 93-99.

Connect the facilities to the code that already exists: the logger, the job system, the RHI and renderer, and the
programs that ship (`game/` demos and benches).

## Tasks

- [x] Logger: the first line carries the build id (`LogCore` info line `build <id> <platform> <config>`), and `Error` lines optionally attach a short trace (option `LogErrorTraces`, off by default, on in Debug); no change to the output format of other lines.
- [x] Job system: every worker thread and the I/O thread call the neutral `RegisterThreadForCrashHandling(name)` at thread start (`worker_pool.cpp`, `async_io.cpp`; it sets the thread name for the report and, if Day 97 found it necessary, an alternate stack; a no-op on a platform that needs nothing); a job that asserts reports the worker's name and a trace that includes the job function (death test in `tests/jobs`, phase 6 label because it tests this phase's feature).
- [x] RHI and renderer: `DeviceLost` (injected and real) and validation errors that the layer reports as errors produce an `ENGINE_ENSURE`-style report with a trace (once per site) through the existing validation capture hook; the adapter name, driver version and frame index are registered as crash state providers; the device-loss injection tests assert that the trace appears and the frame still ends as `DeviceLost`. No Vulkan names outside `engine/rhi/vulkan` (the boundary gate still passes).
- [x] Platform layer: the window backend name registers as a state provider; a fault in a window callback names the thread correctly.
- [x] Programs: `engine_main`, `rhi_demo`, `renderer_demo` and the benches call `InitCrashContext`, `InitDebugFacilities` and `InstallCrashHandler` at startup, controlled by `--crash-handler=on|off` and by the same keys in the engine config or environment (mobile and console programs have no command line); default on in Debug and RelWithDebInfo, off in Release unless `ENGINE_CRASH_HANDLER=1` (the plan's "off by default in release builds"). A `--crash-test=segv|abort|assert|ensure` flag in the demos for manual checks; ctests run each in a child and check the exit signal, the report and the dump (GPU-free where possible; the GPU ones carry the `gpu` label and skip without a GPU as the other demo tests do).
- [x] No-upload check on the shipped programs: every demo, bench and game executable that installs the crash handler gets a `debug_no_network_<name>` ctest (`tests/debug/check_no_network.cmake`, added on Day 95 for the test binaries only), so the guarantee covers the programs that ship and not only the tests.
- [x] Release-build behavior: build a Release configuration once and run the crash tests against it; record what a Release trace looks like (symbols present or not) and what the guide tells a developer to do (RelWithDebInfo, or the symbol store of Day 99).
- [x] Sanitizer flavors: the demos' crash flags run in the ASan flavor of the target that already runs demos, or are documented as plain-only if a sanitizer swallows the fault, per Day 97's policy.
- [x] **Documentation:** the existing guides (`jobs.md`, `rhi.md`, `renderer.md`, `platform-os.md`) get a short paragraph each on what is reported and how to add a state provider; `Fails:`/`Aborts:` comments on touched headers name the macros; `docs/DOC-CONVENTIONS.md` is consistent with Day 93; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, then asan and tsan flavors; the day touches jobs, rhi, renderer and foundation, so expect the whole suite) and quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the logger stamps the build id, worker threads and demos install the handler, RHI device loss and validation errors and an assert inside a job each produce a report with the right context, the demos' `--crash-test` flags are covered by ctests, Release builds behave as documented, and the whole suite still passes.


## Result

Phase 6 has 483 tests. Scoped runs: `ran 1054 of 2357 tests (phases: all; flavors: plain; display: included)` (no failures this run; the
X11 window test passed), `ran 784 of 2357 ... asan`, `ran 519 of 2357 ... tsan`; also the non-unity, clang, Release and
Release-with-checks builds, 8 repeats, `ctest -L docs` and the boundary, raw-new, signal-safety and neutral gates; `docgen lint`
2699/2699. The gdb core test (Day 98) flaked once more under load (`KNOWN-ISSUES.md`).

What changed, against the task list:
- **Logger.** `StartProgramDebugging` logs `[LogCore] INFO: build <id> <os> <arch> <config>` first (through a channel of the same name that is always at Info, since `LogCore` itself defaults to Warn). `SetLogErrorTraces(true)` puts a trace under every `Error` line, in the same write; the programs turn it on in Debug builds. `LogErrorTraces` is off by default.
- **Jobs.** Workers register as `worker-N` and the I/O thread as `io` (`RegisterThreadForCrashHandling(name)`, which also sets the name when the handler is not installed). A death test (`job_crash_linux_test.cpp`, phase 6) asserts inside a job: the assert's trace and the crash report both include the job's function, and the report names the worker.
- **RHI and renderer.** The Vulkan device registers `gpu: <adapter>, driver ..., api ...`; the renderer keeps a `frame` counter; a lost device (real and injected) and the first validation error each print an ensure trace. Two demo ctests (`*_device_loss_trace`) check the trace while the frame still ends as lost (exit 3). The boundary gate passes.
- **Platform layer.** The window backend is the `window` line of the report.
- **Programs.** All four (`rhi_demo`, `rhi_bench`, `renderer_demo`, `renderer_bench`) call `StartProgramDebugging` and `RunCrashTest`; `--crash-handler=on|off`, `ENGINE_CRASH_HANDLER`, and the build default (on in Debug and RelWithDebInfo, off in Release); `--crash-test=segv|abort|assert|ensure`; ctests per program and kind (plain flavor), and `debug_no_network_<program>` for each.
- **Release.** Run against a Release build: with `--crash-handler=on` the report, dump and signal are there; the trace has function names but no file and line, and the handler is off by default (a plain segfault). Recorded in the guide.
- **Sanitizers.** The programs are not sanitized; their crash tests run in the plain flavor only. The sanitizer flavors of the job death test pass (the signal stack registration works under ASan and TSan).

Pages touched: `docs/guide/foundation-debugging.md` ("Starting a program", "Release builds", the short way to add state),
`jobs.md`, `rhi.md`, `renderer.md`, `platform-os.md`, `foundation-logging-and-assert.md`.

## Post-implementation code review

`/code-review high` found 9 issues.

1. The build line was never printed (`LogCore` is at Warn). **Fixed** (own channel at Info; the test now matches `[LogCore] INFO: build `, which would have failed before).
2. Error traces cost a resolve per line and were written in two writes. **Partly fixed**: one write; the cost stays (Debug-only, opt-in; in `KNOWN-ISSUES.md`), and the trace is not in the ring.
3. `ResetCrashStateForTesting` resets the whole provider registry; a cached counter pointer is not re-registered. **Declined**: it is a test helper (documented), and production code never resets.
4. A failed registration left a half-written entry. **Fixed**. The unsynchronized text copy against a crash-time read is accepted and documented ("may show a mix").
5. An ensure in the validation callback can break into a debugger from driver code and traces only the first error. **Declined as designed** (the plan asks for an ensure-style report once per site); the comment now says "first".
6. Threads allocated a signal stack even with no crash handler. **Fixed**: only when the handler is installed, otherwise only the name is set. (Fixing it exposed a real bug: the installer registered its own thread before marking itself installed, leaving the main thread on the library's 16 KiB stack, so the report overflowed it and the nested fault deadlocked on the library's lock. Fixed, with a test of the stack size, and a fault inside the library's handler now ends the process instead of waiting.)
7. A mistyped `--crash-test=` or `--crash-handler=` value was swallowed. **Fixed** (a warning; tests).
8. A RelWithDebInfo build was labelled `release`. **Fixed** (CMake passes the configuration).
9. Every debug test flavor depended on the four programs and re-ran them. **Fixed**: the program tests and the dependency are in the plain flavor only. The bookkeeping files are part of this commit.
