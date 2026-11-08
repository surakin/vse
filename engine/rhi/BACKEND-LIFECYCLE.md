# Backend lifecycle checklist (Phase 8)

What a backend (DX12 in Phase 13, Metal in Phase 14) must provide so the device can be destroyed and created again at run time, a
setting can change without a restart, and a lost device can be replaced. The Vulkan backend (`engine/rhi/vulkan`) and the null
backend (`engine/rhi/null`) are the references; `tests/rhi/lifecycle_conformance_test.cpp` is the conformance suite: a new backend
runs it by adding one factory function to the list at the top of that file. `tests/rhi/check_lifecycle_list.cmake` (the ctest
`rhi_lifecycle_lists`) fails when a function named below is no longer declared in the interface headers or has lost its section here.

The rules that apply to the backend as a whole:

- **Handle epochs.** Every device takes a serial from `rhi::AllocateDeviceSerial()` when it is known to be usable and seeds the
  generation of every handle pool with `rhi::GenerationSeed(serial)`, so a handle of a destroyed device is never valid on the next
  one. Handles stay 8 bytes.
- **Orderly shutdown.** The destructor waits for the GPU, releases deferred destruction, staging, rings and descriptor pools, saves the
  pipeline cache, then destroys the device (and the instance, factory or adapter objects the device created itself).
- **Tolerant destruction after loss.** Destroying every resource, the swapchain and the device must work on a lost device and must
  not hang; waits on a lost device return as soon as the API reports the loss.
- **Pipeline cache identity.** The cache file name carries vendor, device and driver identity; a different adapter writes its own file.
- **Thread rule.** `DeviceHost::Recreate` runs on one thread between frames; the backend needs no extra locking for it.
- **Registration (Phase 13 Day 162).** `rhi::CreateDevice`, `rhi::EnumerateAdapters` and `rhi::DeviceHost` live in the neutral `rhi`
  library and reach a backend through the registry (`rhi/backend_registry.h`): the backend library exports one
  `Register<Name>Backend()` that fills a `BackendDescriptor` (its factory, adapter lister, availability probe, shader format and `Auto`
  rank) and calls `RegisterBackend`, and `engine/rhi/backends/backends.cpp` (`rhi::RegisterCompiledBackends`) calls it. The two
  sections below are those two function pointers.

### `CreateDevice`
Creates the backend's device from `DeviceCreateInfo`. Honors `adapter` (an id from `EnumerateAdapters`, an index or part of the name)
before any environment override, and fails with a log line (no fallback) if the adapter does not exist or cannot present to the window.
DX12: `IDXGIFactory6::EnumAdapterByGpuPreference` / `EnumAdapterByLuid`, `D3D12CreateDevice`, the debug layer when `validation` is on.
Metal: `MTLCopyAllDevices` (macOS) or `MTLCreateSystemDefaultDevice` (iOS), `MTLCommandQueue`; there is no validation switch at run time
(the Metal API validation is an environment setting), so `validation` is advisory there.

### `EnumerateAdapters`
Lists the adapters without creating a device: stable id, name, kind, device-local memory, driver text, whether it can present to the
window, whether it is suitable and why not. DX12: a throwaway `IDXGIFactory6`; id from vendor, device, subsystem and revision ids (the
LUID is valid only until reboot, so it goes in `AdapterInfo::luid`, which `MatchAdapter` accepts since Day 162), `DXGI_ADAPTER_FLAG_SOFTWARE` is `Software` (WARP), UMA from
`D3D12_FEATURE_DATA_ARCHITECTURE`. Metal: `MTLCopyAllDevices`; id from `registryID`; `hasUnifiedMemory` / `isLowPower` give the kind.

### `Adapter`
`Device::Adapter()` reports the adapter the device was created on (the same struct, so a saved setting round-trips).

### `Epoch`
`Device::Epoch()` is the device serial (unique in the process, constant for the life of the device, never 0).

### `Resources`
`Device::Resources()` returns the live object counts per kind, device memory in bytes and internal objects; thread-safe; counts only
live objects (call `Quiesce` first). DX12: from the same handle pools; device memory from the allocator. Metal: likewise.

### `InFrame`
`Device::InFrame()` is true between `BeginFrame` and `EndFrame`; the device host refuses a reset inside a frame.

### `Quiesce`
Waits for the GPU and drains deferred destruction; the device stays usable. A lost device returns as soon as the API reports the
loss. DX12: signal a fence and wait. Metal: `waitUntilCompleted` on a last command buffer.

### `WaitIdle`
As `Quiesce` (the default `Quiesce` calls it). DX12: fence wait, bounded; `DXGI_ERROR_DEVICE_REMOVED` ends it. Metal: as above.

### `DeviceLost`
Sticky once set. DX12: `DXGI_ERROR_DEVICE_REMOVED` / `DEVICE_RESET` from `Present`, `Signal` or `GetDeviceRemovedReason`. Metal has no loss concept
except external-GPU removal (`MTLDeviceRemovalRequestedNotification`) and command buffer errors; map those, and keep the contract.

### `InjectDeviceLoss`
Development aid: make the device behave as lost now (later submissions ignored, `DeviceLost` true). Also the fault injection fields
`lose_device_at_frame`, `lose_device_at_submit`, `lose_device_at_present` and the shared `strikes_left` limit.

### `RecreateSurface`
Replaces the presentation surface while keeping the device, when the adapter can present to the new surface; otherwise fails and the
host does a full reset. Requires no swapchain and no open frame. DX12: a new swap chain target (`CreateSwapChainForHwnd`) needs no
separate surface object, so this can be a cheap check plus bookkeeping. Metal: a new `CAMetalLayer`.

### `Reconfigure`
`Swapchain::Reconfigure(SwapchainDesc)` changes present mode, size, format and HDR mode in one call, reusing the old swapchain where
possible, returning `Ok`, `Minimized` or `Lost`; a zero extent is remembered and applied when the window has an area. DX12:
`ResizeBuffers` (flip model; present mode maps to the sync interval and `DXGI_PRESENT_ALLOW_TEARING`), format and color space through
`SetColorSpace1`. Metal: `drawableSize`, `pixelFormat`, `colorspace`, `wantsExtendedDynamicRangeContent`.

### `ActualPresentMode`
The present mode really in use after fallbacks.

### `GetColorSpace`
The color space of the images (`SrgbNonlinear`, `ScRgbLinear`, `Hdr10`).

### `Capabilities`
`Swapchain::Capabilities()` reports supported present modes, formats and HDR, truthfully, so a settings screen can grey out what is
unavailable. `Hdr10` stays unsupported until the RHI has a 10-bit format.
