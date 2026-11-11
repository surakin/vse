# Phase 26 / Day 300 — Metal I: Backend Library, Adapters, Device, Validation, Events

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 299.

The `rhi_metal` library: registration with the backend registry, adapter enumeration, device creation with a command queue, validation, the frame fence,
and the device capabilities. Modelled on DX12 Day 164.

## Tasks

- [ ] **Library and boundary:** `engine/rhi/metal/` (`.mm` files, ARC), library `rhi_metal` linking `-framework Metal -framework QuartzCore`, registered
      by `rhi_backends` on macOS; `rhi_boundary_check` allows Metal headers only there (planted fixture); `module_deps_check` row for `rhi/metal`
      (`ARCHITECTURE.md` updated in the same commit: `rhi/metal -> rhi, jobs, platform, foundation`).
- [ ] **Adapters** (`metal_adapters.mm`): `MTLCopyAllDevices()`; `AdapterInfo` per the phase file's input list (id `registryID` as lowercase hex, name,
      kind, `dedicated_memory_bytes`, driver as the OS version, `can_present`, `suitable` on `MTLGPUFamilyMac2` plus Metal 3); enumeration creates no
      queue; `MatchAdapter` by id and name; `tests/rhi/adapter_test.cpp` runs with a Metal factory; record what this Iris Plus 645 reports
      (`hasUnifiedMemory`, `isLowPower`, `isRemovable`, `recommendedMaxWorkingSetSize`, argument-buffer tier, `readWriteTextureSupport`).
- [ ] **Device** (`metal_device.mm/.h`): `CreateDevice` for the chosen adapter, one `MTLCommandQueue`, the device serial and generation seed
      (`AllocateDeviceSerial`/`GenerationSeed`), handle pools as the other backends; `Adapter()`, `Epoch()`, `Resources()` counts.
- [ ] **Events:** `MTLSharedEvent` as the device timeline (frames in flight, `WaitIdle` with a bounded wait through `waitUntilSignaledValue:timeoutMS:`
      or a notification listener), command-buffer completion handlers counting finished work; never block forever after a failure.
- [ ] **Validation:** the `validation` setting is advisory on Metal (API validation is an environment setting read when the process starts the Metal
      framework); the GPU test ctests set `MTL_DEBUG_LAYER=1`, `MTL_DEBUG_LAYER_ERROR_MODE=assert`, `MTL_SHADER_VALIDATION=1`; a planted invalid call in a
      test proves a validation error fails the test (and is removed from the passing set: run as an expected-failure check).
- [ ] **Capabilities:** `DeviceCaps` filled from the device (limits, `max_color_attachments` 8, copy alignments, `object_index_stream` true, formats asked
      with the family tables); `CheckGBufferFormats` passes.
- [ ] **Errors:** command-buffer errors (`MTLCommandBufferStatusError`, `error.code`) logged through `rhi_log.h` with the buffer's label; a helper maps
      `NSError` to the RHI's error text without leaking Objective-C into neutral code.
- [ ] **Tests:** `smoke_test`, `backend_registry_test`, `adapter_test`, a new `tests/rhi/metal_device_test.mm` (phase 26: creation, queue, events, caps,
      destruction order leaving nothing alive: ARC plus the `Resources()` counts).
- [ ] **Documentation:** `docs/guide/rhi.md` "Metal" section (adapters, validation environment, what is advisory); `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote; GPU tests under ASan and TSan too, as on Linux).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: ARC ownership of the device and queue against the C++ handle pools, completion
      handlers running on Metal's threads (TSan), bounded waits.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `--rhi=metal` creates a device on the Iris Plus 645 with validation on and silent, the adapter, smoke and Metal device tests pass in three
flavors, the adapter's reported properties are recorded, and `ARCHITECTURE.md` has the `rhi/metal` row.
