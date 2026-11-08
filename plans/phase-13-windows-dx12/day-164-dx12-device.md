# Phase 13 / Day 164 — DX12 I: Factory, Adapters, Device, Debug Layer, Fences

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Days 162-163; the Graphics Tools feature installed (D3D12 debug layer).

The DX12 backend's skeleton: library, COM handling, adapters, device creation, the debug layer routed to the RHI log, capabilities, queues and fences. New
directory `engine/rhi/dx12/`, library `rhi_dx12` (Windows only), registered with Day 162's registry.

## Tasks

- [x] **Library and boundary:** `engine/rhi/dx12/CMakeLists.txt`, `d3d12_api.h` (the one place `d3d12.h`/`dxgi1_6.h` are included, through `os/windows/win32.h`),
      linking `d3d12.lib`, `dxgi.lib`, `dxguid.lib`; `ComRef<T>` (add-ref/release, move-only, no WRL) with tests; `rhi_boundary_check` and
      `platform_neutral_check` clean.
      *(`rhi_dx12` with `d3d12_api.h`, `com_ref.h`, `dx12_result`, `dx12_debug`, `dx12_adapters`, `dx12_device`, `dx12_backend`; the parts of later
      days are placeholders in `dx12_device_later.cpp` that log "not implemented yet (Day N)". `platform_neutral_check` needed one fix: its
      pattern took the quoted engine include `"rhi/dx12/d3d12_api.h"` for an SDK header; the wrapper is now treated like `win32.h`.)*
- [x] **Errors:** `dx12_result.h/.cpp` maps `HRESULT`s to RHI errors and log text (`E_OUTOFMEMORY`, `E_INVALIDARG`, `DXGI_ERROR_DEVICE_REMOVED`,
      `DXGI_ERROR_DEVICE_RESET`, `DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_UNSUPPORTED`, ...); `GetDeviceRemovedReason` is read whenever a removed code appears.
- [x] **`EnumerateAdapters`** exactly as the phase file's "Adapter enumeration" input says: a throwaway `IDXGIFactory6`, `EnumAdapterByGpuPreference`
      (high performance), id from vendor/device/subsystem/revision as lowercase hex, LUID as the second form, name from `Description` (UTF-8), `kind` (WARP is
      `Software`; UMA from `D3D12_FEATURE_DATA_ARCHITECTURE` with a temporary device only where unavoidable — the spec checks whether enumeration may create one),
      `dedicated_memory_bytes`, `driver` from `CheckInterfaceSupport`, `can_present`, `suitable` and `unsuitable_reason` (feature level and binding tier).
      Tests on the PC: the Radeon 860M and WARP both listed, ids stable across two runs.
      *(Decided: a temporary device per adapter at enumeration, to read the binding tier and UMA (D3D12 returns the existing device when the
      process has one on that adapter); about 90 ms per enumeration for the two adapters here. Listed: "AMD Radeon(TM) 860M Graphics"
      (`dx12-1002-1114-50071d05-c2`, Integrated) and WARP ("Microsoft Basic Render Driver", Software); ids equal across runs.)*
- [x] **`CreateDevice`:** honors `DeviceCreateInfo::adapter` first; `D3D12CreateDevice` at the baseline feature level; when `validation` is on, the debug layer
      (`D3D12GetDebugInterface` → `EnableDebugLayer`, GPU-based validation when `sync_validation`/a separate setting asks — mapping decided here) and
      `IDXGIInfoQueue`/`ID3D12InfoQueue1::RegisterMessageCallback` routing messages to the RHI log with severity; break-on-error only under a debugger. A missing
      debug layer is a clear error when validation is requested, not a silent run without it.
      *(Decided: `sync_validation` (or `ENGINE_RHI_VALIDATION_SYNC=1`) turns on GPU-based validation; `Auto` follows `ENGINE_RHI_VALIDATION`,
      else Debug builds; `On` without the layer fails creation. The layer is process-wide once enabled. Live objects are counted from
      `ReportLiveDeviceObjects` through the info-queue callback.)*
- [x] **Validation-silence helper for tests:** the DX12 equivalent of the Vulkan tests' message counter (`gpu_fixture.h` gains a backend parameter; a test fails on
      any warning-or-worse message, with an allow list that starts empty).
      *(Deviation: a separate `DebugLayerSilence` guard in `dx12_device_test.cpp` instead of a backend parameter on `gpu_fixture.h`, which owns a
      Vulkan instance; it fails on any warning, error or live object.)*
- [x] **Capabilities:** `DeviceCaps` from `CheckFeatureSupport` (options, shader model, root signature version, format support for the formats the RHI exposes)
      and the D3D12 constants (copy pitch 256, placement 512, CBV 256), each documented against its source.
      *(Format support arrives with the format table on Day 165.)*
- [x] **Handle pools, serials, epochs:** pools seeded with `GenerationSeed(AllocateDeviceSerial())`; `Epoch`, `Adapter` implemented.
      *(The serial and `Epoch`; the pools arrive with their resources on Days 165-167.)*
- [x] **Queues and fences:** a direct queue (compute and copy queues only if the RHI exposes them), one fence per queue with a monotonically increasing value,
      `WaitIdle`/frame waits through `SetEventOnCompletion` with **bounded** waits (`WaitForSingleObject` with a timeout, then a device-removed check), per-frame
      deferred destruction keyed by fence value.
      *(One direct queue, one fence; waits in 100 ms slices with a removed-device check; deferred destruction keyed by frame fences, collected
      from Day 165.)*
- [x] **Tests** (`tests/rhi/dx12_device_test.cpp`, phase 13): enumerate, create on each adapter (including WARP), create with validation and see zero messages,
      fence wait with a timeout on a never-signaled value returns, destroy cleanly; the null and Vulkan registries unaffected.
      *(10 tests, among them a deliberate D3D12 error that must be counted, so a silent run cannot pass with the layer off.)*
- [x] **Documentation:** `docs/guide/rhi.md` "The DX12 backend" (requirements, debug layer, adapters); reference unchanged unless a public header changed.
- [x] **Runs:** Windows `--affected` plain and asan (only Windows files changed: say so).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: COM reference counts (a `ComRef` copied or released twice; live-object report at device
      destruction through `ID3D12DebugDevice::ReportLiveDeviceObjects` must be empty), the info-queue callback's thread and lifetime, unbounded waits.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `rhi_dx12` is registered, lists the Radeon and WARP adapters with stable ids, creates and destroys a device with the debug layer silent and no live
objects left, and fence waits are bounded.

## Notes

- The Graphics Tools feature was installed by the developer before this day (`d3d12SDKLayers.dll` present); every DX12 test here ran with the
  debug layer on except the fence-timeout test.
- **Registering DX12 changed two things outside the backend.** `Auto` falls through the registry when Vulkan is unavailable, so with any rank
  above 0 a machine without Vulkan would land on the unfinished backend (`rhi_demo_no_vulkan` did, then failed in `CreateSwapchain`): the rank
  stays 0 until Day 172 (review). The demos and benchmarks pin Vulkan until Day 173 gives them a choice. The Day 162 test of a refused backend
  asked for `dx12`, which is registered now; it empties the registry to the null backend and asks for Vulkan instead.
- A Debug-build `ENGINE_ASSERT` in a test without the crash handler shows the C runtime's abort dialog and hangs the run (`KNOWN-ISSUES.md`,
  Day 161): the renderer suite hung once today that way (the test above, before its fix). Runs use `ctest --timeout 300` from here on.

## For the Linux run

Shared: `engine/rhi/backends/backends.cpp` (the `ENGINE_RHI_HAVE_DX12` table entry, not defined on Linux), `engine/rhi/CMakeLists.txt`,
`tests/rhi/backend_registry_test.cpp` (the count per platform), `tests/rhi/CMakeLists.txt`, `tests/renderer/apply_settings_test.cpp`, the four
program `main.cpp` (Vulkan pinned), `tools/platform_neutral_check/check.cmake`, `tools/module_deps_check/modules.txt`,
`tools/config_access_check/allowlist.txt`, `tools/docgen/CMakeLists.txt` (Windows branch only), `docs/guide/rhi.md` (the backends sample),
`docs/reference/` (the new `rhi-dx12` page must regenerate identically: it lists Windows-only headers) and the lint baseline. Run: both unity
modes, `--affected`, `ctest -L docs`.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1222 of 2389 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 138 s; the last
  full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 902 of 2389 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 74 s;
  the last full run took 13 s`.
- All three trees build clean; MSVC `ctest -L "^rhi$|^renderer$|demo|docs|boundary|gate|shaders"` 630 of 630; `windows-clang-cl` phase 0 and RHI 403
  of 403. `cmake --build build-msvc --target docs`: 51 samples, 0 failed; lint 4285 of 4285 documented (`rhi-dx12` 112 of 112).
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 4 findings, 2 fixed, 2 declined.

- **Fixed (with a regression test):** `Auto` could pick the unfinished DX12 backend on a machine without Vulkan (rank 5); the rank is 0 until Day
  172. The new check (a registry holding only DX12 gives `Auto` nothing) fails with rank 5 (seen) and passes with 0.
- **Fixed:** the demos and benchmarks relied on `Auto` meaning Vulkan; they pin Vulkan until Day 173 (the demo test that showed it passes again).
- **Declined:** enumeration creates a temporary device per adapter (about 90 ms here); the alternative leaves `kind` and `suitable` unknown until a
  device exists. Recorded in the guide.
- **Declined:** a validated device enables the debug layer for the whole process, and D3D12 removes a device that was created before it; the
  engine destroys the old device before creating the next one (`DeviceHost`), so only two live devices of different validation settings would
  meet it. Recorded in the guide.
