# Phase 6 / Day 100 — Wiring the Engine In

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 93-99.

Connect the facilities to the code that already exists: the logger, the job system, the RHI and renderer, and the
programs that ship (`game/` demos and benches).

## Tasks

- [ ] Logger: the first line carries the build id (`LogCore` info line `build <id> <platform> <config>`), and `Error` lines optionally attach a short trace (option `LogErrorTraces`, off by default, on in Debug); no change to the output format of other lines.
- [ ] Job system: every worker thread and the I/O thread call the neutral `RegisterThreadForCrashHandling(name)` at thread start (`worker_pool.cpp`, `async_io.cpp`; it sets the thread name for the report and, if Day 97 found it necessary, an alternate stack; a no-op on a platform that needs nothing); a job that asserts reports the worker's name and a trace that includes the job function (death test in `tests/jobs`, phase 6 label because it tests this phase's feature).
- [ ] RHI and renderer: `DeviceLost` (injected and real) and validation errors that the layer reports as errors produce an `ENGINE_ENSURE`-style report with a trace (once per site) through the existing validation capture hook; the adapter name, driver version and frame index are registered as crash state providers; the device-loss injection tests assert that the trace appears and the frame still ends as `DeviceLost`. No Vulkan names outside `engine/rhi/vulkan` (the boundary gate still passes).
- [ ] Platform layer: the window backend name registers as a state provider; a fault in a window callback names the thread correctly.
- [ ] Programs: `engine_main`, `rhi_demo`, `renderer_demo` and the benches call `InitCrashContext`, `InitDebugFacilities` and `InstallCrashHandler` at startup, controlled by `--crash-handler=on|off` and by the same keys in the engine config or environment (mobile and console programs have no command line); default on in Debug and RelWithDebInfo, off in Release unless `ENGINE_CRASH_HANDLER=1` (the plan's "off by default in release builds"); each links `crashpad_handler` through `engine_add_crash_handler`. A `--crash-test=segv|abort|assert|ensure` flag in the demos for manual checks; ctests run each in a child and check the exit signal, the report and the dump (GPU-free where possible; the GPU ones carry the `gpu` label and skip without a GPU as the other demo tests do).
- [ ] Release-build behavior: build a Release configuration once and run the crash tests against it; record what a Release trace looks like (symbols present or not) and what the guide tells a developer to do (RelWithDebInfo, or the symbol store of Day 99).
- [ ] Sanitizer flavors: the demos' crash flags run in the ASan flavor of the target that already runs demos, or are documented as plain-only if a sanitizer swallows the fault, per Day 97's policy.
- [ ] **Documentation:** the existing guides (`jobs.md`, `rhi.md`, `renderer.md`, `platform-os.md`) get a short paragraph each on what is reported and how to add a state provider; `Fails:`/`Aborts:` comments on touched headers name the macros; `docs/DOC-CONVENTIONS.md` is consistent with Day 93; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, then asan and tsan flavors; the day touches jobs, rhi, renderer and foundation, so expect the whole suite) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the logger stamps the build id, worker threads and demos install the handler, RHI device loss and validation errors and an assert inside a job each produce a report with the right context, the demos' `--crash-test` flags are covered by ctests, Release builds behave as documented, and the whole suite still passes.
