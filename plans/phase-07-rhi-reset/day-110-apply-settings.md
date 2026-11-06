# Phase 7 / Day 110 — `Renderer::ApplySettings`

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Days 104-109.

Join the pieces: settings in, the cheapest sufficient action out, with a defined moment, a defined thread, and a way back.

## Tasks

- [ ] `Renderer::ApplySettings(const RenderSettings&)` returns an `ApplyResult` (the `SettingsChange` actually performed, success, the reason on failure, elapsed time per stage); `Renderer::Settings()` returns the current ones. The renderer owns (or is given) the `DeviceHost`; `RendererDesc` gains it, keeping the borrowed-device path for tests.
- [ ] Execution by class: `None` records the value; `Dynamic` stores the parameter for the next frame; `Swapchain` calls `Reconfigure` (Day 107) and rebuilds what depends on the extent or format; `PipelineRebuild` releases and recreates the material and pass pipelines through the registry (tiers 3-4 only); `DeviceReset` runs the full sequence of the spec through `DeviceHost::Recreate` and the registry.
- [ ] **When it runs:** `ApplySettings` called mid-frame is queued and applied at the start of the next `RenderFrame` (the frame boundary); called between frames it applies at once; a pending request is replaced by a newer one. Documented as part of the thread-safety contract: owner thread only, workers joined and frames in flight drained (`Quiesce`) before a swapchain, pipeline or device action; the job scheduler itself is untouched.
- [ ] **Failure and rollback:** if the new device or swapchain cannot be created, restore the previous settings (second `Recreate`); if that also fails, the renderer enters the `DeviceLost` state with the reason in `LastError` and the app callback fires. The `generation` counter changes only on success. Tested with injected create failures at each stage (device, swapchain, one owner's `Recreate`).
- [ ] Callbacks: `SetResetCallbacks(before, after, user)` so the app learns a reset started/ended (to show a message, pause audio, drop its own GPU caches); a progress stage name is passed to `before`.
- [ ] `FrameStatus` gains `Resetting`? Decide in the spec: either `RenderFrame` performs a pending reset inline and returns `Ok`/`Failed`, or the app drives it; implement the chosen one and document what the app draws meanwhile (the window keeps its last picture; the first frame after a swapchain recreation is a clear frame, never garbage).
- [ ] Stats: `RendererStats` counts applies per class, resets, rollbacks and the last reset's duration; crash-state provider shows the settings generation.
- [ ] Tests (null backend): each class performs exactly its action and no more (counted through device statistics: no device re-created for a `Swapchain` change, no swapchain re-created for a `Dynamic` change); a `DeviceReset` with a live scene renders the same recorded stream afterwards; queued-while-in-frame behavior; a newer request replaces a pending one; rollback after each injected failure; double failure ends `DeviceLost`; workers (with a scheduler) are idle during the reset (TSan); a settings round trip through the text format applies equal settings as `None`.
- [ ] GPU test (skips without a GPU): validation on→off, present mode change, resize, and a `DeviceReset`, each followed by an offscreen frame and the frame counters.
- [ ] **Documentation:** labels; `renderer.md` "Changing settings at run time" with the table of classes, the sequence, the rollback rule and a tagged sample; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` plus asan and tsan; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`, extra scrutiny on the failure paths and on thread boundaries; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** `ApplySettings` performs the cheapest sufficient action for every built-in setting, applies at a frame boundary on the owning thread, rolls back when creation fails, reports what it did, and the guide documents the contract.
