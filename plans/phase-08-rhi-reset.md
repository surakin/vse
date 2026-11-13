# Phase 8 — RHI Reset

**Depends on:** Phase 5 (Renderer core). Runs after Phases 6-7 in the roadmap so its days re-run only their own tests
and can use the debugging facilities. Must complete before the DX12 and Metal ports (Phases 13 and 21): the reset contract
is part of the RHI interface those backends implement.
**Goal:** a user can change **any** rendering setting without restarting the application, which means re-initializing
the RHI (and everything built on it) at run time has to be supported as a normal operation, not a recovery hack. Today a
lost device is terminal (`FrameStatus::DeviceLost` and the caller gives up); after this phase it is recoverable, and
the same path serves deliberate setting changes.
**Definition of done:** a `RenderSettings` description of everything that is user-changeable (GPU/adapter choice,
validation on/off, present mode and vsync, resolution and fullscreen, swapchain format and HDR, anti-aliasing and
quality options as they appear, shader/material quality) can be applied to a running app with
`Renderer::ApplySettings(...)`; each setting is classified by what it costs (no work, swapchain recreation, pipeline
rebuild, full device reset) and a full reset tears down and rebuilds the device, the swapchain, the transient pool,
meshes, textures, materials, pipelines and passes, with the scene intact; a reset loop of 100 iterations leaves no
leaks, the rendered picture before and after matches the golden image, and a lost device recovers through the same
path.

Every day of this phase has its own file under `plans/phase-08-rhi-reset/` (see `CLAUDE.md`, planning policy).
**Status: COMPLETE (2026-11-06).** Days 103-114 are done. Closing run: `ran 2737 of 2737 tests (phases: all; flavors: all; display: included; crash tests: included)`, all passing; docs 4/4; both demos clean on Wayland and X11 with validation; 100 scripted resets on each. `CURRENT_PHASE` stays 7 until Phase 13 has its day files and starts.

## Days

Each day has its own file of individually checkable sub-tasks, a docs task, an end-of-day `/code-review high` task, and a
concrete Done condition. Day numbers continue from Phase 7 (which ended at Day 102).

103. [Design Spec, Reset Spike & Phase Setup](phase-08-rhi-reset/day-103-design-spec-and-spike.md)
104. [The Settings Model: `RenderSettings`, Cost Classes, Diff](phase-08-rhi-reset/day-104-settings-model.md)
105. [Adapter Enumeration & Selection](phase-08-rhi-reset/day-105-adapter-enumeration.md)
106. [Device Lifecycle: `DeviceHost`, Epochs, Leak Accounting](phase-08-rhi-reset/day-106-device-lifecycle.md)
107. [Swapchain & Presentation Changes](phase-08-rhi-reset/day-107-swapchain-and-presentation.md)
108. [The Resource Registry; Recreatable Meshes & Textures](phase-08-rhi-reset/day-108-resource-registry.md)
109. [Recreatable Materials, Passes, Frame Data & Transient Pool](phase-08-rhi-reset/day-109-recreatable-renderer-state.md)
110. [`Renderer::ApplySettings`](phase-08-rhi-reset/day-110-apply-settings.md)
111. [Device-Loss Recovery](phase-08-rhi-reset/day-111-device-loss-recovery.md)
112. [Reset Loops, Golden Equality & Sanitizers](phase-08-rhi-reset/day-112-reset-soak-and-golden.md)
113. [Demo Seams, Settings File & Cost Measurements](phase-08-rhi-reset/day-113-demo-seams-and-measurements.md)
114. [Portability, Docs, Closing Full Run & Wrap-up](phase-08-rhi-reset/day-114-portability-docs-and-closing-run.md)

Order reasoning: Day 103 settles the architecture by spike, because the cost of an in-process device re-creation (driver
leaks, surface re-creation, pipeline cache reuse) decides the rest. The settings model (104) and adapter enumeration
(105) are pure data and come before anything that applies them. The lifecycle (106) and the swapchain changes (107) are the
RHI half; the resource registry and the recreatable owners (108-109) are the renderer half; `ApplySettings` (110) joins
them; device-loss recovery (111) reuses that path; 112 proves it under load and in the sanitizers; 113 gives the demos
the live controls and measures what a reset costs; 114 prepares DX12/Metal and closes the phase.

## Design decisions made while planning (2026-11-06; Day 103 confirms or amends them in the spec)

1. **A reset destroys the `Device` object and creates a new one.** No backend gets an in-place `Reset()`: re-creating is
   the only way to change the adapter, the instance (validation layers) or the backend, so one mechanism serves every
   case. A small owner, `rhi::DeviceHost`, holds the device and swapchain and performs the sequence.
2. **Handles from a dead device are detectable.** A fresh device's handle pools would otherwise reuse generation 1 and
   alias valid handles. Each device is created with a process-wide serial that seeds its generation counters (Day 106).
3. **Owners rebuild from CPU-side sources**, through a renderer-level registry that rebuilds in tier order (Days 108-109).
   Pipelines are rebuilt from descriptors, never serialized.
4. **`backend` is a setting** classified as a full reset, so Null to Vulkan is tested here. Vulkan to DX12 or Metal is
   decided out of this phase (there is no second real backend yet) and filed for Phases 13 and 21.
5. **Fullscreen needs a platform window API that does not exist** (`os::Window` has resize serial only). Day 107 adds
   `SetFullscreen` for Wayland and X11 with the Windows and macOS checklists, or records it as a capability the window
   reports as unsupported; the settings model treats it as a capability either way.
6. **A failed reset rolls back.** If the new device cannot be created, the previous settings are recreated; if that fails
   too, the frame reports `DeviceLost` and the app is told (Days 110-111).
7. **Settings are open for extension.** Later phases register a setting with its cost class; the diff never switches over
   a closed enum of today's settings (Day 104).

## Key deliverables (the day files hold the sub-tasks)

- **Settings model.** One `RenderSettings` struct (versioned, comparable, serializable) with a function that diffs two
  settings into the *cheapest sufficient action*: none, dynamic (change a parameter), swapchain recreation, pipeline
  rebuild, device re-creation. Settings that exist only on some backends are expressed as capabilities, so the same
  struct serves Vulkan, DX12 and Metal.
- **Device-dependent resources can be recreated.** Today a `GpuMesh` or texture holds only GPU handles. Each owner needs
  the CPU-side source (or a reload callback) to rebuild after a reset: meshes keep their `MeshData` (or the asset
  handle), textures keep pixels or their asset path, materials keep parameters and texture bindings by name, pipelines
  are rebuilt on demand from their descriptors. A **resource registry** in the renderer records every
  device-dependent object and rebuilds them in dependency order after a reset.
- **RHI lifecycle API.** Explicit `Device::Shutdown`/re-create semantics: wait idle, destroy deferred-destruction
  queues, release the swapchain, destroy the device and instance if the GPU changes, create again with the new
  settings, report which handles are now invalid. Handle generations make stale handles detectable after a reset.
- **Device loss recovery.** `DeviceLost` triggers the same reset (with the saved settings and a retry limit), emitting a
  log and a callback so the app can show a message; fault-injection tests (`lose_device_at_frame`) drive it.
- **Swapchain and presentation changes** (vsync, present mode, format, HDR, resize, fullscreen) without a full reset
  where the API allows; window re-creation when a surface must change.
- **Application seams.** `renderer_demo` and `rhi_demo` gain keys or command-line toggles that apply settings live
  (GPU selection on multi-GPU machines, validation, vsync, resolution), used by the milestone run.
- **Tests.** A reset loop with leak counts and device statistics at zero; golden-image equality before and after a
  reset; a reset during a frame sequence with objects, lights and materials; recovery from injected device loss;
  ASan/TSan clean; the null device implements the same lifecycle so most tests need no GPU.
- **Closing run.** The phase's last day runs `scripts/test-phase.sh --all --flavor all` (every test in plain, ASan and
  TSan), `ctest -L docs` and the demos on both display backends, and records the `ran X of Y tests` line in its day
  file (Phase 6 policy; day-to-day runs are scoped to the current phase).

## Notes / open questions

- **What is a "setting"?** The phase must not hard-code today's list: new settings added by later phases (UI scale,
  shadow quality, upscaling) have to plug into the diff-and-apply mechanism by declaring their cost class.
- **Cost of a reset.** Rebuilding every pipeline and texture can take seconds; measure it (pipeline cache on disk
  helps) and decide what the app shows meanwhile.
- **State that survives.** Scene data, camera, input and the asset system stay; the job scheduler is untouched; frames
  in flight are drained first.
- **Multi-threading.** A reset happens at a frame boundary on the owning thread; any worker still recording is joined
  first. Define this in the RHI thread-safety contract.
- **Interaction with Phase 13/13.** DX12 and Metal must implement the same lifecycle; the portability reviews should be
  re-read for adapter enumeration (DXGI factory, `MTLCopyAllDevices`) and window-surface re-creation.
- Backend switch at run time (Vulkan to DX12 on Windows): decided on Day 103 — not in this phase; Null to Vulkan is tested, the rest waits for Phases 13 and 21.
- Day 103 answers (spec `docs/superpowers/specs/2026-11-06-rhi-reset-design.md`): a device cycle costs about 210 ms for a trivial scene with no fd and near-zero memory drift over 100 iterations; the instance and surface are already per device; the development machine has one Vulkan adapter, so adapter switching is tested on the null backend only; the pipeline-set rebuild cost is measured on Day 113.
