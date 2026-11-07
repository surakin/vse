# Phase 12 / Day 167 — Guides, Closing Full Runs on Both Machines & Wrap-up

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Days 140-166.

Finish the documentation, run everything on both machines, review the whole phase, close it.

## Tasks

- [ ] **New guide `docs/guide/windows.md`:** prerequisites (with the versions from Day 140), how the build finds the compilers and SDK (and the override
      variables), presets, building, running tests and demos, the debug layer and validation layers, crash dumps and symbols, where settings live, known gaps;
      listed in `docs/guide/README.md`.
- [ ] **Guide pass** (read each as a new developer, fix what is stale): `getting-started.md`, `building-and-testing.md`, `platform-os.md`,
      `platform-windowing-and-input.md`, `io.md`, `settings.md`, `jobs.md`, `foundation-debugging.md`, `porting-debugging.md`, `rhi.md`, `rhi-shaders.md`,
      `renderer.md`, `render-settings.md`; every tagged sample compiles and runs on both machines; `docs/reference/modules.txt` covers every new public header;
      `docgen lint` zero; reference regenerated (identical on both machines) and committed.
- [ ] **Project documents:** `BUILD.md` (Windows section marked supported, every version checked on the PC, the Linux table checked on the Linux machine); `CLAUDE.md` (Windows build and test commands; the Windows directories rule; flavors per platform; the two-machine rule if it stays
      for later phases; the DbgHelp exception to the debugging-library rule; `windows.h` only through `win32.h`); `PLAN.md` (assumption #7 gains Windows x64; #2
      states the UCRT; #18 states DXIL minting; #20 the DbgHelp resolver; roadmap row; progress-log entry); `shaders/README.md` current.
- [ ] **Known-issues sweep:** every Phase 12 deferral filed (enhanced barriers, placed resources, HDR10, exclusive fullscreen, IME composition, ARM64, fast-fail
      crashes, anything left by a day), every entry this phase fixed removed (the backend registry and switch, the HLSL object index, the D3D12 Y flip, `--permute`
      on Windows, the portable `u128` under MSVC, the Breakpad Windows headers, `env_source.cpp`'s `environ`, share modes).
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** Windows `scripts/test-phase.sh --all --flavor all` (plain and asan; the summary names tsan
      as not on Windows), `ctest -L docs`; Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`; both demos 1000 frames with validation on DX12 and
      Vulkan (Windows) and on Wayland and X11 (Linux); record both final `ran X of Y tests (phases ...)` lines in this file; nothing left out on either machine.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 140 to the
      working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review"; fixes re-run on both
      machines.
- [ ] Update `plans/phase-12-windows-dx12.md` (status complete, days done), `PLAN.md`, `STATUS.md`; `CURRENT_PHASE` stays 12 until Phase 13 has its day files and
      starts (policy); commit and push.

**Done:** `docs/guide/windows.md` and every touched guide are current and identical in output on both machines, the closing lines of the full runs on Windows
(plain, asan) and Linux (all flavors) are recorded with every test passing or a filed known issue, the demos run on all four backend and display combinations, the
uncapped review is recorded, and Phase 12 is marked complete.
