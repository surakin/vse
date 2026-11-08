# Phase 13 / Day 169 — DX12 VI: Swapchain and Presentation

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 168; Day 158 (the Win32 window).

Present to a Win32 window through a flip-model DXGI swap chain with every behavior the RHI's `Swapchain` promises.

## Tasks

- [x] **Creation:** `CreateSwapChainForHwnd` with `DXGI_SWAP_EFFECT_FLIP_DISCARD`, buffer count from the RHI, UNorm buffers with **sRGB render-target views** when the
      RHI asks for an sRGB format; `MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` (fullscreen is the window's borderless mode, not DXGI's).
- [x] **Present modes:** `Fifo` = sync interval 1; `Immediate` = interval 0 with `DXGI_PRESENT_ALLOW_TEARING` when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` is supported
      (else reported unsupported); `Mailbox` falls back to `Fifo` with the documented log line; the supported set reported to the settings system honestly.
- [x] **Frame pacing:** the frame-latency waitable object (`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency`) waited on in `Acquire`
      with a bounded wait; `Acquire` returns the current back buffer index.
- [x] **`Reconfigure`:** size and format changes through `ResizeBuffers` after idling and releasing every reference to the back buffers; a minimized window (zero
      client size) skips presenting without spinning; `RecreateSurface` makes a new swap chain on the same device.
- [x] **HDR:** `HdrMode::ScRgbLinear` maps to `R16G16B16A16_FLOAT` with `SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)` when `CheckColorSpaceSupport`
      allows it; record whether this PC's display supports HDR and what was checked; HDR10 stays filed.
- [x] **Errors:** `DXGI_ERROR_DEVICE_REMOVED`/`RESET` from `Present` reported as device loss (recovery is Day 172); occlusion (`DXGI_STATUS_OCCLUDED`) handled.
- [ ] **`rhi_demo` on DX12** (moved to Day 173, where the demos choose their RHI backend): `--backend dx12` runs 1000 frames with `--validation`, resizes by dragging, minimizes and restores, toggles fullscreen; record the
      checks done by hand.
- [x] **Tests:** the shared `swapchain_reconfigure_test` and the swapchain tests with a DX12 factory (display group); `dx12_swapchain_test.cpp` (phase 13) for the
      tearing flag, the latency wait, zero-size handling; debug layer silent.
- [x] **Runs:** Windows `--affected` plain and asan.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: back-buffer references held across `ResizeBuffers` (it fails if any remain), waits that could
      hang when the window is hidden, sRGB view versus buffer format.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** DX12 presents to a Win32 window with every present mode mapped or honestly reported, reconfigures on resize and format change, and `rhi_demo` runs
1000 frames on DX12 with the debug layer silent.

## Notes

- **The swap chain** is flip-discard with three linear buffers (`B8G8R8A8_UNORM` by default) on the device's Win32 window; an sRGB format is
  the render-target view (checked: a clear to linear 0.5 through the view reads back 188 from the buffer). Alt+Enter is off
  (`DXGI_MWA_NO_ALT_ENTER`). Buffers are device textures (external, in `PRESENT`), freed at once before `ResizeBuffers`.
- **Present modes:** `Fifo` interval 1; `Immediate` interval 0 with `DXGI_PRESENT_ALLOW_TEARING` (tearing is allowed on this PC, so the swap
  chain is created with it, since `ResizeBuffers` cannot add it); `Mailbox` is not offered and falls back to `Fifo` with an info line. The
  capabilities say so.
- **Frame pacing:** `SetMaximumFrameLatency(FramesInFlight)` and the waitable object, waited on in `Acquire` for at most 500 ms
  (non-alertable): a timeout is `Minimized`. A minimized window returns `Minimized` at once (twenty frames in under 200 ms, tested).
- **HDR on this PC:** the output is SDR (270 nits, `DXGI_OUTPUT_DESC1`); scRGB (`R16G16B16A16_FLOAT`, `G10_NONE_P709`) is presentable through
  the compositor and is granted when asked for. `CheckColorSpaceSupport` answers for the buffers' current format, so the capability is
  learned from a float swap chain (review finding). HDR10 stays filed.
- **Errors:** a device-loss result from `Present` marks the device lost (`NoteRemoved`; recovery is Day 172); `DXGI_STATUS_OCCLUDED` is `Ok`.
  Each present is followed by a fence signal, so waiting for the device waits for the presents (without it the debug layer reported a
  back buffer released with GPU work in flight).
- **Plan amendment:** the `rhi_demo` item moves to Day 173 (the demos pick their RHI backend there); the shared swapchain flow is tested
  through `DeviceHost` on a Win32 window with a DX12 device (`dx12_swapchain_test.cpp`), as `swapchain_reconfigure_test` does for Vulkan.
- **A Vulkan fix rides along:** the review found the Vulkan swapchain had the same mid-frame use-after-free as the DX12 one (below); its
  presentation checks now cover it on every platform.

## For the Linux run

Shared: `engine/rhi/vulkan/vk_swapchain.cpp` (`ReleaseImages` frees only the swapchain's images) and `tests/rhi/vulkan_swapchain_test.cpp`
(the in-frame rebuild check in `RunPresentationChecks`, run by the Wayland and X11 cases). Run: both unity modes, `--affected` with asan and
tsan, the Wayland and X11 swapchain tests with the session unlocked.

## Runs

Windows PC:

- `scripts/test-phase.sh --affected`: `ran 1265 of 2469 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 138 s; the last full run took 13 s`.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 939 of 2469 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 73 s; the last full run took 13 s` (`FramePool does not commit slab memory until blocks are used [asan]`
  failed once, the filed flake; it passed alone right after on Day 168).
- `rhi_tests "[swapchain]"`: 27 test cases, 25 passed, 2 skipped (Wayland, X11); the DX12 swapchain tests with the debug layer silent. All
  three trees build clean; docs 51 samples, 0 failed; lint 4489 of 4489.
- Linux: not run; pending until the phase close (Day 174).

## Post-implementation code review

`/code-review high` on the day's diff: 5 findings, all fixed; two of them while testing, before the review (the present fence and the scRGB
capability), recorded with it.

- **Fixed (regression test, failed without the fix):** rebuilding the swap chain inside a frame (`Acquire` after a resize) used `WaitIdle`,
  which freed every retired resource, including one the frame destroyed and a list recorded before still used. On this PC the list then
  hung the GPU (TDR, `DXGI_ERROR_DEVICE_HUNG`). The swapchain waits for the GPU only now (`Dx12Device::WaitForGpu`).
- **Fixed (regression test, failed without the fix):** the Vulkan swapchain had the same flaw (`ReleaseImages` collected everything retired);
  the validation layer reported `VUID-vkQueueSubmit2-commandBuffer-03874`. It frees only its own images now.
- **Fixed (the reconfigure test failed without it):** presents were not fenced (above).
- **Fixed:** the latency wait was alertable and took an APC as a free buffer; it is not alertable now (no test: an APC cannot be queued
  from the test without one of our own).
- **Fixed (the reconfigure test failed without it):** the scRGB capability was read from an 8-bit swap chain (above).
