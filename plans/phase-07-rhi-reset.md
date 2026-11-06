# Phase 7 — RHI Reset

**Depends on:** Phase 4 (Renderer core). Runs after Phases 5-6 in the roadmap so its days re-run only their own tests
and can use the debugging facilities. Must complete before the DX12 and Metal ports (Phases 8-9): the reset contract
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

Before it starts, every day of this phase needs its own file under `plans/phase-07-rhi-reset/` (see `CLAUDE.md`,
planning policy).

## Key deliverables (to be expanded into day-by-day tasks when started)

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
  file (Phase 5 policy; day-to-day runs are scoped to the current phase).

## Notes / open questions

- **What is a "setting"?** The phase must not hard-code today's list: new settings added by later phases (UI scale,
  shadow quality, upscaling) have to plug into the diff-and-apply mechanism by declaring their cost class.
- **Cost of a reset.** Rebuilding every pipeline and texture can take seconds; measure it (pipeline cache on disk
  helps) and decide what the app shows meanwhile.
- **State that survives.** Scene data, camera, input and the asset system stay; the job scheduler is untouched; frames
  in flight are drained first.
- **Multi-threading.** A reset happens at a frame boundary on the owning thread; any worker still recording is joined
  first. Define this in the RHI thread-safety contract.
- **Interaction with Phase 8/9.** DX12 and Metal must implement the same lifecycle; the portability reviews should be
  re-read for adapter enumeration (DXGI factory, `MTLCopyAllDevices`) and window-surface re-creation.
- Backend switch at run time (Vulkan to DX12 on Windows) is a stretch goal of the same mechanism; decide in the day
  files whether to include it.
