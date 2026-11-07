# Phase 12 / Day 162 — DX12 VI: Swapchain and Presentation

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 161; Day 151 (the Win32 window).

Present to a Win32 window through a flip-model DXGI swap chain with every behavior the RHI's `Swapchain` promises.

## Tasks

- [ ] **Creation:** `CreateSwapChainForHwnd` with `DXGI_SWAP_EFFECT_FLIP_DISCARD`, buffer count from the RHI, UNorm buffers with **sRGB render-target views** when the
      RHI asks for an sRGB format; `MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` (fullscreen is the window's borderless mode, not DXGI's).
- [ ] **Present modes:** `Fifo` = sync interval 1; `Immediate` = interval 0 with `DXGI_PRESENT_ALLOW_TEARING` when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` is supported
      (else reported unsupported); `Mailbox` falls back to `Fifo` with the documented log line; the supported set reported to the settings system honestly.
- [ ] **Frame pacing:** the frame-latency waitable object (`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency`) waited on in `Acquire`
      with a bounded wait; `Acquire` returns the current back buffer index.
- [ ] **`Reconfigure`:** size and format changes through `ResizeBuffers` after idling and releasing every reference to the back buffers; a minimized window (zero
      client size) skips presenting without spinning; `RecreateSurface` makes a new swap chain on the same device.
- [ ] **HDR:** `HdrMode::ScRgbLinear` maps to `R16G16B16A16_FLOAT` with `SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)` when `CheckColorSpaceSupport`
      allows it; record whether this PC's display supports HDR and what was checked; HDR10 stays filed.
- [ ] **Errors:** `DXGI_ERROR_DEVICE_REMOVED`/`RESET` from `Present` reported as device loss (recovery is Day 165); occlusion (`DXGI_STATUS_OCCLUDED`) handled.
- [ ] **`rhi_demo` on DX12:** `--backend dx12` runs 1000 frames with `--validation`, resizes by dragging, minimizes and restores, toggles fullscreen; record the
      checks done by hand.
- [ ] **Tests:** the shared `swapchain_reconfigure_test` and the swapchain tests with a DX12 factory (display group); `dx12_swapchain_test.cpp` (phase 12) for the
      tearing flag, the latency wait, zero-size handling; debug layer silent.
- [ ] **Runs:** Windows `--affected` plain and asan.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: back-buffer references held across `ResizeBuffers` (it fails if any remain), waits that could
      hang when the window is hidden, sRGB view versus buffer format.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** DX12 presents to a Win32 window with every present mode mapped or honestly reported, reconfigures on resize and format change, and `rhi_demo` runs
1000 frames on DX12 with the debug layer silent.
