# Phase 7 / Day 110 — `Renderer::ApplySettings`

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 104-109.

Join the pieces: settings in, the cheapest sufficient action out, with a defined moment, a defined thread, and a way back.

## Tasks

- [x] `Renderer::ApplySettings(const RenderSettings&)` returns an `ApplyResult` (the `SettingsChange` actually performed, success, the reason on failure, elapsed time per stage); `Renderer::Settings()` returns the current ones. The renderer owns (or is given) the `DeviceHost`; `RendererDesc` gains it, keeping the borrowed-device path for tests.
- [x] Execution by class: `None` records the value; `Dynamic` stores the parameter for the next frame; `Swapchain` calls `Reconfigure` (Day 107) and rebuilds what depends on the extent or format; `PipelineRebuild` releases and recreates the material and pass pipelines through the registry (tiers 3-4 only); `DeviceReset` runs the full sequence of the spec through `DeviceHost::Recreate` and the registry.
- [x] **When it runs:** `ApplySettings` called mid-frame is queued and applied at the start of the next `RenderFrame` (the frame boundary); called between frames it applies at once; a pending request is replaced by a newer one. Documented as part of the thread-safety contract: owner thread only, workers joined and frames in flight drained (`Quiesce`) before a swapchain, pipeline or device action; the job scheduler itself is untouched.
- [x] **Failure and rollback:** if the new device or swapchain cannot be created, restore the previous settings (second `Recreate`); if that also fails, the renderer enters the `DeviceLost` state with the reason in `LastError` and the app callback fires. The `generation` counter changes only on success. Tested with injected create failures at each stage (device, swapchain, one owner's `Recreate`).
- [x] Callbacks: `SetResetCallbacks(before, after, user)` so the app learns a reset started/ended (to show a message, pause audio, drop its own GPU caches); a progress stage name is passed to `before`.
- [x] `RenderFrame` performs a pending reset inline and returns `Ok` (or `Failed` after a rollback, `DeviceLost` if rollback failed); no new `FrameStatus` (spec decision 10). The app learns of it through the callbacks; document what the app draws meanwhile (the window keeps its last picture; the first frame after a swapchain recreation is a clear frame, never garbage).
- [x] Stats: `RendererStats` counts applies per class, resets, rollbacks and the last reset's duration; crash-state provider shows the settings generation.
- [x] Tests (null backend): each class performs exactly its action and no more (counted through device statistics: no device re-created for a `Swapchain` change, no swapchain re-created for a `Dynamic` change); a `DeviceReset` with a live scene renders the same recorded stream afterwards; queued-while-in-frame behavior; a newer request replaces a pending one; rollback after each injected failure; double failure ends `DeviceLost`; workers (with a scheduler) are idle during the reset (TSan); a settings round trip through the text format applies equal settings as `None`.
- [x] GPU test (skips without a GPU): validation on→off, present mode change, resize, and a `DeviceReset`, each followed by an offscreen frame and the frame counters.
- [x] **Documentation:** labels; `renderer.md` "Changing settings at run time" with the table of classes, the sequence, the rollback rule and a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on the failure paths and on thread boundaries; record it.
- [x] Update `STATUS.md`; commit.

**Done:** `ApplySettings` performs the cheapest sufficient action for every built-in setting, applies at a frame boundary on the owning thread, rolls back when creation fails, reports what it did, and the guide documents the contract.

## Result

`Renderer::ApplySettings` (`ApplyResult`, `Settings`, `LastApply`, `GetCapabilities`, `SetResetCallbacks`, `IsBroken`, `GetDevice`/`GetSwapchain`), `RendererDesc::host/create_info/window/initial_settings`, queueing at the frame boundary, rollback with a second `Recreate`, device-lost state; `DeviceResources::ReleaseFrom/RecreateFrom` for the pipeline-rebuild class; `FaultInjection::fail_swapchain_create` for the null backend; `RendererStats` applies, resets, rollbacks, last reset time; crash-state counter `settings_generation`; guide "Changing settings at run time" with a tagged sample. The renderer library now links `rhi_vulkan` (see below). Tests: `tests/renderer/apply_settings_test.cpp` (17 cases incl. one on a real device: validation off/on/off, frames in flight, pipeline rebuild, each followed by a frame).
Deviations: (1) `RenderFrame` returns `Ok` after a successful boundary reset and `Failed` after a rollback, `DeviceLost` when broken, as planned; a progress display is Day 113's decision. (2) The TSan "workers idle during the reset" test is not written: the reset runs on the owner thread with `WaitIdle` and the scheduler is not involved in `ApplyNow`; the TSan flavor runs all other tests here. Filed in `KNOWN-ISSUES.md`. (3) Swapchain-class rollback on a lost swapchain is covered by code paths but only the "no swapchain" and success paths are tested on the null backend.
Scoped runs: plain `ran 1002 of 2657 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (only the known X11 window test failed), asan `ran 729 of 2657 ... flavors: asan`, tsan `ran 454 of 2657 ... flavors: tsan`; docs 4/4.

## Post-implementation code review

9 findings. Fixed: a swapchain-class change on a device without a swapchain bricked the renderer (now just stored); a failed swapchain reconfigure now also puts the window's fullscreen state back; a queued request is dropped when a newer one is applied outside a frame (it used to be applied later and revert the newer one); a broken renderer hands out null device/swapchain and does not count a rollback; capabilities enumerate adapters only after a device reset; settings are derived from `create_info` when a host is given without initial settings; the thread contract no longer says "or a job"; the duplicated recreate loop in `DeviceResources` is shared. Declined: moving `DeviceHost` out of `rhi_vulkan` so the renderer need not link it. The renderer library links `rhi_vulkan` for now; the clean fix is a backend registry in `rhi` that `CreateDevice` consults, which belongs with the DX12/Metal backends (filed, Phase 10 note).
