# Phase 13 / Day 161 — Renderer, Demos and Baselines on Windows through Vulkan

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 160.

The rest of the stack on Windows through Vulkan: the renderer suite, the RHI reset tests, both demos, and the benchmark numbers DX12 will be compared against on
the same machine.

## Tasks

- [ ] **The Phase 5 renderer suite** (`tests/renderer`) and `vulkan_renderer_demo_test` on Windows: render graph, materials, permutations, G-buffer, lighting,
      `renderer_demo.ppm` within tolerance (same rule as Day 160: investigate, never re-bless).
- [ ] **The Phase 8 reset tests** on Windows through Vulkan: `lifecycle_conformance_test` (Vulkan factory), `device_host_test`, `swapchain_reconfigure_test`,
      `vulkan_reset_golden_test`, `device_loss_recovery_test`, `reset_soak_test` (100 resets: counts and device memory unchanged); record the reset time on this
      machine beside the Linux 40 ms.
- [ ] **Demos:** `rhi_demo` and `renderer_demo` are windowed programs (`engine_windowed_program`, Day 158: the Windows subsystem, entered
      through `main`; `--help` and `--list-settings` from a terminal print through the attached console) and build and run on Windows (POSIX includes in `game/*/main.cpp` gone, through `os::`); 1000 frames with
      `--validation` exit 0; `--list-settings`, `--settings`, `--save-settings` and `--set` work; the `demo` ctests (golden screenshot, bounded runs, settings round
      trip) pass on Windows.
- [ ] **Window interplay:** resize by dragging and minimize/restore while the demo runs (the swapchain reconfigures; a minimized window does not spin the CPU);
      fullscreen toggle through the setting; record what was checked by hand.
- [ ] **Baselines:** run `rhi_bench` and `renderer_bench` on Windows through Vulkan and record the numbers (SetBindGroup cost, instanced-draw cost, frame time
      of the demo scene) in the spec: these are what Days 166, 171 and 173 compare DX12 with.
- [ ] **Documentation:** `docs/guide/getting-started.md` names the Windows commands to build and run the demos; reference regenerated if headers changed.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (the demos' sources are shared) plus asan and tsan; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: swapchain recreation during the modal resize loop, and a minimized window's zero-size extent.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the renderer, the reset machinery and both demos work on Windows through Vulkan with validation silent, and the Windows Vulkan baselines are recorded.
