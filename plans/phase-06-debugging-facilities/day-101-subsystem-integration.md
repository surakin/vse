# Phase 6 / Day 101 — Wiring the Engine In

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 93-100.

Connect the facilities to the code that already exists: the logger, the job system, the RHI and renderer, and the
programs that ship (`game/` demos and benches).

## Tasks

- [ ] Logger: every line carries the build id once at start (`LogCore` info line `build <id> <platform> <config>`), and `Error` lines optionally attach a short trace (option `LogErrorTraces`, off by default, on in Debug); no change to the output format of other lines.
- [ ] Job system: every worker thread and the I/O thread register for crash handling (alternate stack, thread name) at thread start in `worker_pool.cpp` and `async_io.cpp`; a job that asserts reports the job's name if it has one; test: an assert inside a job reports the worker's name and a trace that includes the job function (death test; `tests/jobs` gets the test, phase 6 label because it tests this phase's feature).
- [ ] RHI and renderer: `DeviceLost` (injected and real) and validation errors that the layer reports as errors attach `ENGINE_ENSURE`-style reports with a trace (once per site) through the existing validation capture hook; the adapter name, driver version and frame index are registered as crash state providers; the device-loss injection tests assert the trace appears and the frame still ends as `DeviceLost`. No Vulkan names outside `engine/rhi/vulkan` (the boundary gate still passes).
- [ ] Platform layer: the window backend name registers as a state provider; a fault in a window callback reports the thread correctly.
- [ ] Programs: `engine_main`, `rhi_demo`, `renderer_demo` and the benches call `InitCrashContext` and `InstallCrashHandler` at startup, controlled by `--crash-handler=on|off` (default on in Debug and RelWithDebInfo, off in Release unless `ENGINE_CRASH_HANDLER=1` or the flag is given: the plan's "off by default in release builds"); a `--crash-test=segv|abort|assert|ensure` flag in the demos for manual checks. Demo ctests run each crash flag in a child and check the exit signal, the report and the dump file (GPU-free where possible; the GPU ones carry the `gpu` label and skip without a GPU, as the other demo tests do).
- [ ] Release-build behavior: build a Release configuration once and run the crash tests against it; record what a Release trace looks like (frame pointers on or off, symbols present or not) and what the guide tells a developer to do (RelWithDebInfo).
- [ ] Sanitizer flavors: the demos' crash flags run in the ASan flavor of the test target that already runs demos (or are documented as plain-only if a sanitizer swallows the fault, per the Day 97 policy table).
- [ ] **Documentation:** the existing guides (`jobs.md`, `rhi.md`, `renderer.md`, `platform-os.md`) get one short paragraph each on what is reported and how to add a state provider; `Fails:`/`Aborts:` doc comments on the touched headers are updated to name the macros; `docs/DOC-CONVENTIONS.md` is consistent with Day 93; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, then asan and tsan flavors; the day touches jobs, rhi, renderer and foundation, so expect the whole suite) and quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the logger stamps the build id, worker threads and demos install the handler, RHI device-loss and validation errors and an assert inside a job each produce a report with the right context, the demos' `--crash-test` flags are covered by ctests, Release builds behave as documented, and the whole suite still passes.
