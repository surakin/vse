# Backend lifecycle checklist (Phase 8)

What a backend (the consoles later; Metal, built in Phase 21) must provide so the device can be destroyed and created again at run time, a
setting can change without a restart, and a lost device can be replaced. The Vulkan backend (`engine/rhi/vulkan`), the Direct3D 12
backend (`engine/rhi/dx12`, complete since Phase 13 Day 172), the Metal backend (`engine/rhi/metal`, Phase 21 Days 300-309) and the null backend (`engine/rhi/null`) are the references; `tests/rhi/lifecycle_conformance_test.cpp` is the conformance suite: a new backend
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
Metal, as built (Day 309): `MTLCopyAllDevices` matched against `adapter`, then `ENGINE_RHI_GPU`, else a discrete GPU before an integrated
one; one `MTLCommandQueue` and an `MTLSharedEvent` timeline. There is no validation switch at run time (Metal reads `MTL_DEBUG_LAYER` when
the process starts), so `validation` is advisory: a request that differs from the process is logged as taking effect at the next start,
and `sync_validation` has no counterpart (Metal tracks hazards). iOS will use `MTLCreateSystemDefaultDevice` (Phase 31).

### `EnumerateAdapters`
Lists the adapters without creating a device: stable id, name, kind, device-local memory, driver text, whether it can present to the
window, whether it is suitable and why not. DX12: a throwaway `IDXGIFactory6`; the id is `dx12-` and the vendor, device, subsystem and
revision ids (the LUID is valid only until reboot, so it goes in `AdapterInfo::luid`, which `MatchAdapter` accepts since Day 162),
`DXGI_ADAPTER_FLAG_SOFTWARE` is `Software` (WARP), UMA from `D3D12_FEATURE_DATA_ARCHITECTURE`; an adapter is suitable when
`D3D12CreateDevice` succeeds on it at feature level 12.0. Metal, as built (Day 309): `MTLCopyAllDevices`, no queue; the id is `registryID` in 16 hex digits, the driver the macOS version,
`hasUnifiedMemory` / `isLowPower` give the kind, `isHeadless` whether it can present; suitable with the Mac2 family and Metal 3.

### `Adapter`
`Device::Adapter()` reports the adapter the device was created on (the same struct, so a saved setting round-trips).

### `Epoch`
`Device::Epoch()` is the device serial (unique in the process, constant for the life of the device, never 0).

### `Resources`
`Device::Resources()` returns the live object counts per kind, device memory in bytes and internal objects; thread-safe; counts only
live objects (call `Quiesce` first). DX12: from the same handle pools; device memory is the sum of
`GetResourceAllocationInfo` over the committed resources alive, internal objects every `ID3D12Resource` alive (the staging ring's too). Metal, as
built (Day 309): the same pools; memory is each buffer's and texture's `allocatedSize`, internal objects every Metal buffer and texture
alive (the staging ring's too; a swapchain's drawables are the layer's and not counted).

### `InFrame`
`Device::InFrame()` is true between `BeginFrame` and `EndFrame`; the device host refuses a reset inside a frame.

### `Quiesce`
Waits for the GPU and drains deferred destruction; the device stays usable. A lost device returns as soon as the API reports the
loss. DX12: signal the queue's fence, wait for it (bounded), then collect what retired. Metal, as built (Day 309): a wait for the timeline's last value through the shared-event listener, in 100 ms slices that end on a lost
device, then the collection; `Quiesce` also writes the pipeline archive.

### `WaitIdle`
As `Quiesce` (the default `Quiesce` calls it). DX12: fence wait, bounded; a removed device ends it. Metal, as built (Day 309): the timeline
wait above, without the archive.

### `DeviceLost`
Sticky once set. DX12: `DXGI_ERROR_DEVICE_REMOVED`, `DEVICE_HUNG` or `DEVICE_RESET` from `Present`, `Signal`, a resource creation or
`GetDeviceRemovedReason` (`IsDeviceLostResult`); the reason is logged once, with a trace. Metal, as built (Day 309): the
device observer (`MTLCopyAllDevicesWithObserver`: `MTLDeviceWasRemovedNotification` and `MTLDeviceRemovalRequestedNotification` for this
device) and a command buffer whose error leaves the device unusable (removed, access revoked, timeout, page fault, internal) set it, once,
with a log line and an ensure; the handlers hold shared state, not the device, and the observer is removed before the device goes.

### `InjectDeviceLoss`
Development aid: make the device behave as lost now (later submissions ignored, `DeviceLost` true). Also the fault injection fields
`lose_device_at_frame`, `lose_device_at_submit`, `lose_device_at_present` and the shared `strikes_left` limit, and `fail_allocations_from`.
DX12: a real removal through `ID3D12Device5::RemoveDevice`, so the driver reports it from then on as it would a hang; the debug layer's
messages about the expected removal are demoted to information. D3D12 hands out one device per adapter in a process, so a removal
loses every live device of that adapter, as a real one would; a device made after they are all released is new. `fail_allocations_from`
counts committed resources. Metal, as built (Day 309): Metal has no call that removes a device, so the loss is recorded and every
later submission, upload, readback and present is refused as on a real loss; the faults are the same fields, `fail_allocations_from`
counting Metal buffers and textures.

### `RecreateSurface`
Replaces the presentation surface while keeping the device, when the adapter can present to the new surface; otherwise fails and the
host does a full reset. Requires no swapchain and no open frame. DX12: DXGI has no surface object (a swap chain is made for a window by
`CreateSwapChainForHwnd`), so it waits for the GPU and remembers the new window; it refuses a window that is not Win32. Metal, as built (Day 309): it waits for the GPU and remembers the window; the next swapchain configures that window's own
`CAMetalLayer` (a layer belongs to its view and takes any device, and replacing it would leave the window's pointer to it stale).

### `Reconfigure`
`Swapchain::Reconfigure(SwapchainDesc)` changes present mode, size, format and HDR mode in one call, reusing the old swapchain where
possible, returning `Ok`, `Minimized` or `Lost`; a zero extent is remembered and applied when the window has an area. DX12:
`ResizeBuffers` (flip discard, three buffers; present mode maps to the sync interval and `DXGI_PRESENT_ALLOW_TEARING`, tearing allowed
from creation since `ResizeBuffers` cannot add it), format and color space through `SetColorSpace1`. A rebuild waits for the GPU and
releases only the swap chain's own images, so it is safe inside a frame. Metal, as built (Day 309): `drawableSize`, `pixelFormat`, `colorspace`, `wantsExtendedDynamicRangeContent`, `displaySyncEnabled` and
`maximumDrawableCount` on the layer; `Lost` on a lost device.

### `ActualPresentMode`
The present mode really in use after fallbacks. DX12: `Mailbox` falls back to `Fifo`; `Immediate` needs tearing. Metal, as built (Day
325): `Fifo` is `displaySyncEnabled`, `Immediate` turns it off, `Mailbox` (not on macOS) falls back to `Fifo`.

### `GetColorSpace`
The color space of the images (`SrgbNonlinear`, `ScRgbLinear`, `Hdr10`). Metal, as built (Day 309): sRGB, or extended linear sRGB
when scRGB HDR was granted.

### `Capabilities`
`Swapchain::Capabilities()` reports supported present modes, formats and HDR, truthfully, so a settings screen can grey out what is
unavailable. `Hdr10` stays unsupported until the RHI has a 10-bit format. DX12: scRGB is reported when a float swap chain accepts the
linear color space. Metal, as built (Day 309): `Fifo` and `Immediate`, `bgra8_srgb` and `bgra8_unorm`, scRGB when the window's screen
reports a potential EDR value above 1 (read when the swapchain is built).

### Not provided: a run-time backend switch

A program chooses its backend when it starts; the renderer's `backend` setting takes effect at the next start (Phase 13 Day 173, the
user's decision). A backend therefore never has to hand over to another one in the same process. Switching the GPU within a backend
is a full reset through the functions above (`tests/rhi/adapter_switch_gpu_test.cpp`: a hardware adapter and WARP on DX12). A Mac with
one GPU has nothing to switch to; the switch logic is covered by the null backend there, as on Linux.
