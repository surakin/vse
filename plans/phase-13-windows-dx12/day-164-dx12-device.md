# Phase 13 / Day 164 — DX12 I: Factory, Adapters, Device, Debug Layer, Fences

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Days 162-163; the Graphics Tools feature installed (D3D12 debug layer).

The DX12 backend's skeleton: library, COM handling, adapters, device creation, the debug layer routed to the RHI log, capabilities, queues and fences. New
directory `engine/rhi/dx12/`, library `rhi_dx12` (Windows only), registered with Day 162's registry.

## Tasks

- [ ] **Library and boundary:** `engine/rhi/dx12/CMakeLists.txt`, `d3d12_api.h` (the one place `d3d12.h`/`dxgi1_6.h` are included, through `os/windows/win32.h`),
      linking `d3d12.lib`, `dxgi.lib`, `dxguid.lib`; `ComRef<T>` (add-ref/release, move-only, no WRL) with tests; `rhi_boundary_check` and
      `platform_neutral_check` clean.
- [ ] **Errors:** `dx12_result.h/.cpp` maps `HRESULT`s to RHI errors and log text (`E_OUTOFMEMORY`, `E_INVALIDARG`, `DXGI_ERROR_DEVICE_REMOVED`,
      `DXGI_ERROR_DEVICE_RESET`, `DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_UNSUPPORTED`, ...); `GetDeviceRemovedReason` is read whenever a removed code appears.
- [ ] **`EnumerateAdapters`** exactly as the phase file's "Adapter enumeration" input says: a throwaway `IDXGIFactory6`, `EnumAdapterByGpuPreference`
      (high performance), id from vendor/device/subsystem/revision as lowercase hex, LUID as the second form, name from `Description` (UTF-8), `kind` (WARP is
      `Software`; UMA from `D3D12_FEATURE_DATA_ARCHITECTURE` with a temporary device only where unavoidable — the spec checks whether enumeration may create one),
      `dedicated_memory_bytes`, `driver` from `CheckInterfaceSupport`, `can_present`, `suitable` and `unsuitable_reason` (feature level and binding tier).
      Tests on the PC: the Radeon 860M and WARP both listed, ids stable across two runs.
- [ ] **`CreateDevice`:** honors `DeviceCreateInfo::adapter` first; `D3D12CreateDevice` at the baseline feature level; when `validation` is on, the debug layer
      (`D3D12GetDebugInterface` → `EnableDebugLayer`, GPU-based validation when `sync_validation`/a separate setting asks — mapping decided here) and
      `IDXGIInfoQueue`/`ID3D12InfoQueue1::RegisterMessageCallback` routing messages to the RHI log with severity; break-on-error only under a debugger. A missing
      debug layer is a clear error when validation is requested, not a silent run without it.
- [ ] **Validation-silence helper for tests:** the DX12 equivalent of the Vulkan tests' message counter (`gpu_fixture.h` gains a backend parameter; a test fails on
      any warning-or-worse message, with an allow list that starts empty).
- [ ] **Capabilities:** `DeviceCaps` from `CheckFeatureSupport` (options, shader model, root signature version, format support for the formats the RHI exposes)
      and the D3D12 constants (copy pitch 256, placement 512, CBV 256), each documented against its source.
- [ ] **Handle pools, serials, epochs:** pools seeded with `GenerationSeed(AllocateDeviceSerial())`; `Epoch`, `Adapter` implemented.
- [ ] **Queues and fences:** a direct queue (compute and copy queues only if the RHI exposes them), one fence per queue with a monotonically increasing value,
      `WaitIdle`/frame waits through `SetEventOnCompletion` with **bounded** waits (`WaitForSingleObject` with a timeout, then a device-removed check), per-frame
      deferred destruction keyed by fence value.
- [ ] **Tests** (`tests/rhi/dx12_device_test.cpp`, phase 13): enumerate, create on each adapter (including WARP), create with validation and see zero messages,
      fence wait with a timeout on a never-signaled value returns, destroy cleanly; the null and Vulkan registries unaffected.
- [ ] **Documentation:** `docs/guide/rhi.md` "The DX12 backend" (requirements, debug layer, adapters); reference unchanged unless a public header changed.
- [ ] **Runs:** Windows `--affected` plain and asan (only Windows files changed: say so).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: COM reference counts (a `ComRef` copied or released twice; live-object report at device
      destruction through `ID3D12DebugDevice::ReportLiveDeviceObjects` must be empty), the info-queue callback's thread and lifetime, unbounded waits.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `rhi_dx12` is registered, lists the Radeon and WARP adapters with stable ids, creates and destroys a device with the debug layer silent and no live
objects left, and fence waits are bounded.
