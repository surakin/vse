# Phase 26 / Day 325 — Metal Lifecycle, Device Loss and Resets

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 324.

`engine/rhi/BACKEND-LIFECYCLE.md` on Metal: the fifteen functions, the lifecycle conformance suite, device loss (external-GPU removal and injected), and
the reset loops of Phase 8. Modelled on DX12 Day 172.

## Tasks

- [ ] **The fifteen functions:** `CreateDevice`, `EnumerateAdapters`, `Adapter`, `Epoch`, `Resources`, `InFrame`, `Quiesce`, `WaitIdle`, `DeviceLost`,
      `InjectDeviceLoss`, `RecreateSurface`, `Reconfigure`, `ActualPresentMode`, `GetColorSpace`, `Capabilities`, each checked against its section and
      marked "Metal, as built (Day 325)".
- [ ] **Device loss:** `MTLRegisterDevicesObserver` (or `MTLCopyAllDevicesWithObserver`) for `MTLDeviceRemovalRequestedNotification` and
      `WasRemoved` feeding `DeviceLost`; command-buffer errors that mean the device is gone (`MTLCommandBufferErrorDeviceRemoved`) likewise;
      `InjectDeviceLoss` makes every later submission fail as a real loss would; destruction after loss never waits forever.
- [ ] **Conformance:** a Metal factory in `tests/rhi/lifecycle_conformance_test.cpp`; the whole suite passes.
- [ ] **Resets:** the 100-reset loops (counts, memory, pipeline archive) and the pixel-identical-after-reset test on Metal; `device_host_test`,
      `swapchain_reconfigure_test`, `device_loss_recovery`, `reset_soak` with Metal; measure one device reset (Linux: about 40 ms on its GPU).
- [ ] **GPU switch:** with one adapter there is nothing to switch to on this Mac: `adapter_switch_gpu_test` skips with that reason; the switch logic is
      covered by the null backend as on Linux (recorded).
- [ ] **Validation settings:** `validation`/`sync_validation` documented as advisory on Metal; changing them reports "takes effect at next start".
- [ ] **Documentation:** `BACKEND-LIFECYCLE.md` Metal notes; `rhi.md` and `renderer.md` ("Device loss and recovery") Metal notes; `rhi_lifecycle_lists`
      passes; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the device observer's lifetime and thread, tolerant destruction after loss, ARC objects
      surviving a reset that should not.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Metal backend passes the lifecycle conformance suite and the reset loops with unchanged counts and pixel-identical output, recovers from an
injected loss, the reset time is recorded, and the lifecycle checklist is marked as built for Metal.
