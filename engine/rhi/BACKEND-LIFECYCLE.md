# Backend lifecycle checklist (Phase 8)

What a backend (Metal in Phase 14, the consoles later) must provide so the device can be destroyed and created again at run time, a
setting can change without a restart, and a lost device can be replaced. The Vulkan backend (`engine/rhi/vulkan`), the Direct3D 12
backend (`engine/rhi/dx12`, complete since Phase 13 Day 172) and the null backend (`engine/rhi/null`) are the references; `tests/rhi/lifecycle_conformance_test.cpp` is the conformance suite: a new backend
runs it by adding one factory function to the list at the top of that file. `tests/rhi/check_lifecycle_list.cmake` (the ctest
`rhi_lifecycle_lists`) fails when a function named below is no longer declared in the interface headers or has lost its section here.

The rules that apply to the backend as a whole:

- **Handle epochs.** Every device takes a serial from `rhi::AllocateDeviceSerial()` when it is known to be usable and seeds the
  generation of every handle pool with `rhi::GenerationSeed(serial)`, so a handle of a destroyed device is never valid on the next
  one. Handles stay 8 bytes.
- **Orderly shutdown.** The destructor waits for the GPU, releases deferred destruction, staging, rings and descriptor pools, saves the
  pipeline cache, then destroys the device (and the instance, factory or adapter objects the device created itself). DX12: wait for the
  fence, the command lists and their allocators, retired objects, a leaked swap chain, the pipelines (the `ID3D12PipelineLibrary` is
  saved), the descriptor heaps and rings, the resources, then the fence, queue, device, adapter and factory; with validation on,
  `ReportLiveDeviceObjects` runs before the device goes and reports nothing.
- **Tolerant destruction after loss.** Destroying every resource, the swapchain and the device must work on a lost device and must
  not hang; waits on a lost device return as soon as the API reports the loss. DX12: every fence wait is bounded and ends early once
  `GetDeviceRemovedReason` reports the removal (a removed device's fence reports `UINT64_MAX`).
- **Pipeline cache identity.** The cache file name carries vendor, device and driver identity; a different adapter writes its own file.
  DX12: `pipelines_<adapter id>_<driver>.bin`, whose header repeats the identity (`rhi/pipeline_cache_file.h`, shared with Vulkan).
- **Thread rule.** `DeviceHost::Recreate` runs on one thread between frames; the backend needs no extra locking for it.
- **Registration (Phase 13 Day 162).** `rhi::CreateDevice`, `rhi::EnumerateAdapters` and `rhi::DeviceHost` live in the neutral `rhi`
  library and reach a backend through the registry (`rhi/backend_registry.h`): the backend library exports one
  `Register<Name>Backend()` that fills a `BackendDescriptor` (its factory, adapter lister, availability probe, shader format and `Auto`
  rank) and calls `RegisterBackend`, and `engine/rhi/backends/backends.cpp` (`rhi::RegisterCompiledBackends`) calls it. The two
  sections below are those two function pointers.

### `CreateDevice`
Creates the backend's device from `DeviceCreateInfo`. Honors `adapter` (an id from `EnumerateAdapters`, an index or part of the name)
before any environment override, and fails with a log line (no fallback) if the adapter does not exist or cannot present to the window.
DX12: `IDXGIFactory6::EnumAdapterByGpuPreference` (high performance first, WARP last) matched against `adapter`, then `ENGINE_RHI_GPU`;
`D3D12CreateDevice` at feature level 12.0; the debug layer when `validation` is on, GPU-based validation with `sync_validation`. A device
that is already removed when it is made counts as a failure.
Metal: `MTLCopyAllDevices` (macOS) or `MTLCreateSystemDefaultDevice` (iOS), `MTLCommandQueue`; there is no validation switch at run time
(the Metal API validation is an environment setting), so `validation` is advisory there.

### `EnumerateAdapters`
Lists the adapters without creating a device: stable id, name, kind, device-local memory, driver text, whether it can present to the
window, whether it is suitable and why not. DX12: a throwaway `IDXGIFactory6`; the id is `dx12-` and the vendor, device, subsystem and
revision ids (the LUID is valid only until reboot, so it goes in `AdapterInfo::luid`, which `MatchAdapter` accepts since Day 162),
`DXGI_ADAPTER_FLAG_SOFTWARE` is `Software` (WARP), UMA from `D3D12_FEATURE_DATA_ARCHITECTURE`; an adapter is suitable when
`D3D12CreateDevice` succeeds on it at feature level 12.0. Metal: `MTLCopyAllDevices`; id from `registryID`; `hasUnifiedMemory` / `isLowPower` give the kind.

### `Adapter`
`Device::Adapter()` reports the adapter the device was created on (the same struct, so a saved setting round-trips).

### `Epoch`
`Device::Epoch()` is the device serial (unique in the process, constant for the life of the device, never 0).

### `Resources`
`Device::Resources()` returns the live object counts per kind, device memory in bytes and internal objects; thread-safe; counts only
live objects (call `Quiesce` first). DX12: from the same handle pools; device memory is the sum of
`GetResourceAllocationInfo` over the committed resources alive, internal objects every `ID3D12Resource` alive (the staging ring's too). Metal: likewise.

### `InFrame`
`Device::InFrame()` is true between `BeginFrame` and `EndFrame`; the device host refuses a reset inside a frame.

### `Quiesce`
Waits for the GPU and drains deferred destruction; the device stays usable. A lost device returns as soon as the API reports the
loss. DX12: signal the queue's fence, wait for it (bounded), then collect what retired. Metal: `waitUntilCompleted` on a last command buffer.

### `WaitIdle`
As `Quiesce` (the default `Quiesce` calls it). DX12: fence wait, bounded; a removed device ends it. Metal: as above.

### `DeviceLost`
Sticky once set. DX12: `DXGI_ERROR_DEVICE_REMOVED`, `DEVICE_HUNG` or `DEVICE_RESET` from `Present`, `Signal`, a resource creation or
`GetDeviceRemovedReason` (`IsDeviceLostResult`); the reason is logged once, with a trace. Metal has no loss concept
except external-GPU removal (`MTLDeviceRemovalRequestedNotification`) and command buffer errors; map those, and keep the contract.

### `InjectDeviceLoss`
Development aid: make the device behave as lost now (later submissions ignored, `DeviceLost` true). Also the fault injection fields
`lose_device_at_frame`, `lose_device_at_submit`, `lose_device_at_present` and the shared `strikes_left` limit, and `fail_allocations_from`.
DX12: a real removal through `ID3D12Device5::RemoveDevice`, so the driver reports it from then on as it would a hang; the debug layer's
messages about the expected removal are demoted to information. D3D12 hands out one device per adapter in a process, so a removal
loses every live device of that adapter, as a real one would; a device made after they are all released is new. `fail_allocations_from`
counts committed resources.

### `RecreateSurface`
Replaces the presentation surface while keeping the device, when the adapter can present to the new surface; otherwise fails and the
host does a full reset. Requires no swapchain and no open frame. DX12: DXGI has no surface object (a swap chain is made for a window by
`CreateSwapChainForHwnd`), so it waits for the GPU and remembers the new window; it refuses a window that is not Win32. Metal: a new `CAMetalLayer`.

### `Reconfigure`
`Swapchain::Reconfigure(SwapchainDesc)` changes present mode, size, format and HDR mode in one call, reusing the old swapchain where
possible, returning `Ok`, `Minimized` or `Lost`; a zero extent is remembered and applied when the window has an area. DX12:
`ResizeBuffers` (flip discard, three buffers; present mode maps to the sync interval and `DXGI_PRESENT_ALLOW_TEARING`, tearing allowed
from creation since `ResizeBuffers` cannot add it), format and color space through `SetColorSpace1`. A rebuild waits for the GPU and
releases only the swap chain's own images, so it is safe inside a frame. Metal: `drawableSize`, `pixelFormat`, `colorspace`, `wantsExtendedDynamicRangeContent`.

### `ActualPresentMode`
The present mode really in use after fallbacks. DX12: `Mailbox` falls back to `Fifo`; `Immediate` needs tearing.

### `GetColorSpace`
The color space of the images (`SrgbNonlinear`, `ScRgbLinear`, `Hdr10`).

### `Capabilities`
`Swapchain::Capabilities()` reports supported present modes, formats and HDR, truthfully, so a settings screen can grey out what is
unavailable. `Hdr10` stays unsupported until the RHI has a 10-bit format. DX12: scRGB is reported when a float swap chain accepts the
linear color space.

### Switching backends

Not a function: a change of the `backend` setting is a device reset like any other (`Renderer::ApplySettings`, cost class
`DeviceReset`). Every owner registered with `renderer::DeviceResources` releases on the old device and recreates on the new one from
its CPU-side source, picking its shader blobs by the new device's `DeviceCaps::shader_format`. Nothing a backend does is needed for it
beyond the functions above. Tested by `tests/rhi/backend_switch_gpu_test.cpp` (Vulkan and DX12, and the GPU switch between a
hardware adapter and WARP).
