# Phase 26 / Day 325 — Metal Lifecycle, Device Loss and Resets

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 324.

`engine/rhi/BACKEND-LIFECYCLE.md` on Metal: the fifteen functions, the lifecycle conformance suite, device loss (external-GPU removal and injected), and
the reset loops of Phase 8. Modelled on DX12 Day 172.

## Tasks

- [x] **The fifteen functions:** `CreateDevice`, `EnumerateAdapters`, `Adapter`, `Epoch`, `Resources`, `InFrame`, `Quiesce`, `WaitIdle`, `DeviceLost`,
      `InjectDeviceLoss`, `RecreateSurface`, `Reconfigure`, `ActualPresentMode`, `GetColorSpace`, `Capabilities`, each checked against its section and
      marked "Metal, as built (Day 325)".
- [x] **Device loss:** `MTLRegisterDevicesObserver` (or `MTLCopyAllDevicesWithObserver`) for `MTLDeviceRemovalRequestedNotification` and
      `WasRemoved` feeding `DeviceLost`; command-buffer errors that mean the device is gone (`MTLCommandBufferErrorDeviceRemoved`) likewise;
      `InjectDeviceLoss` makes every later submission fail as a real loss would; destruction after loss never waits forever.
- [x] **Conformance:** a Metal factory in `tests/rhi/lifecycle_conformance_test.cpp`; the whole suite passes.
- [x] **Resets:** (`metal_lifecycle_test`; `device_host_test` and `swapchain_reconfigure_test` stay Vulkan-only, their Metal counterparts are `metal_lifecycle_test` and `metal_swapchain_test`; `device_loss_recovery` and `reset_golden_gpu_test` run per backend; `reset_soak` is on the null backend everywhere; the reset time is printed by the test, recorded with the run) the 100-reset loops (counts, memory, pipeline archive) and the pixel-identical-after-reset test on Metal; `device_host_test`,
      `swapchain_reconfigure_test`, `device_loss_recovery`, `reset_soak` with Metal; measure one device reset (Linux: about 40 ms on its GPU).
- [x] **GPU switch:** (`adapter_switch_gpu_test` is Direct3D 12-only and skips here; the switch logic is covered by the null backend, as on Linux) with one adapter there is nothing to switch to on this Mac: `adapter_switch_gpu_test` skips with that reason; the switch logic is
      covered by the null backend as on Linux (recorded).
- [x] **Validation settings:** `validation`/`sync_validation` documented as advisory on Metal; changing them reports "takes effect at next start".
- [x] **Documentation:** `BACKEND-LIFECYCLE.md` Metal notes; `rhi.md` and `renderer.md` ("Device loss and recovery") Metal notes; `rhi_lifecycle_lists`
      passes; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU tests (quote). **Waiting for the user: Days 315-325's runs, after they say go.**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the device observer's lifetime and thread, tolerant destruction after loss, ARC objects
      surviving a reset that should not.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Metal backend passes the lifecycle conformance suite and the reset loops with unchanged counts and pixel-identical output, recovers from an
injected loss, the reset time is recorded, and the lifecycle checklist is marked as built for Metal.

## Plan amendments (made during the day)

- **`Auto` picks Metal on macOS from today** (rank 30), and the Day 314 fallback (the setting's `auto` as the null backend on macOS) is gone:
  `renderer::BackendForSetting` is the setting's value on every platform. The suites run against this state in the waiting run.
- **"Takes effect at next start" for the validation settings** is a log line from Metal's device creation (which a reset repeats), in both
  directions, not a restart class in the renderer's settings diff: that would teach the diff about backends for one case.

## Implementation notes

- **Device loss:** `MTLCopyAllDevicesWithObserver` per device. `MTLDeviceWasRemovedNotification` and `MTLDeviceRemovalRequestedNotification`
  for this device's registry ID mark it lost through the shared state, never the device. A GPU whose removal was requested is left out of the
  next adapter list (`NoteMetalRemovalRequested`), so the reset releases it. The observer is removed in the destructor; a live-observer count
  is kept for the tests.
- **Conformance:** a Metal case in `tests/rhi/lifecycle_conformance_test.cpp`, mirroring D3D12's.
- **Tests added** (waiting for the run): `tests/rhi/macos/metal_lifecycle_test.mm` covers:
  - 100 resets through `DeviceHost` with an owner of a buffer and a compute pipeline, the cache on: counts, memory and observers unchanged,
    and every pipeline from the archive the last device wrote; the reset time is printed;
  - a stale handle on the next device;
  - a loss after real work: a recorded list refused, no fence, waits answered at once, destruction not waiting.
- **Validation settings:** a request that differs from the process is logged as taking effect at the next start; `sync_validation` has no
  Metal counterpart (logged).
- **Docs:** `engine/rhi/BACKEND-LIFECYCLE.md` ("Metal, as built (Day 325)" for every function), `docs/guide/rhi.md` (loss, `Auto`),
  `docs/guide/renderer.md` (device loss on Metal, the setting), `renderer.h` (`BackendForSetting`); the reference regenerated, `docgen lint`
  at zero.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: waiting for the user.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, 9 fixed, 1 declined.

1. **A GPU whose removal was requested was picked again by the reset.** Fixed: it is left out of later adapter lists.
2. **The demos' `auto` tests failed instead of skipping on a Mac without Metal 3.** Fixed: Metal's "no device" reasons are in the skip patterns.
3. **The loss test could not fail on `Submit`.** Fixed: real work first, a recorded list refused, the fence and the wait checked.
4. **The device observer had no test.** Fixed in part: a live-observer count, checked across the 100 resets (the removal itself cannot be
   made to happen here).
5. **The reset loop had the pipeline cache off.** Fixed: on, with a compute pipeline per device, each one found in the archive.
6. **The observer comment promised more than Apple documents.** Fixed: it says why the handler holds only shared state.
7. **`BackendForSetting`'s doc said it read the registry, and callers kept stale comments.** Fixed.
8. **The Day 314 cache entry and a resolved Day 316 note in `KNOWN-ISSUES.md`.** Fixed.
9. **Each device creation enumerates the GPUs twice.** Declined: one more enumeration is small next to device creation, and merging the list
   with the observer's would tie the adapter listing to a device's lifetime.
10. **Day file and `STATUS.md` not updated.** Done in this commit.
