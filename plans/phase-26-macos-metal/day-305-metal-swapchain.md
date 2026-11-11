# Phase 26 / Day 305 — Metal VI: `CAMetalLayer` Swapchain and Presentation

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 304.

Presentation on Metal: the swapchain over the Cocoa window's `CAMetalLayer`, `nextDrawable` as `Acquire`, present modes, resize, `Reconfigure`, surface
re-creation and EDR. Modelled on DX12 Day 169.

## Tasks

- [ ] **Swapchain** (`metal_swapchain.mm`): from the window's opaque layer pointer (Day 295): `device`, `pixelFormat` (`BGRA8Unorm` or `BGRA8Unorm_sRGB`
      per the requested format), `drawableSize` from the backing size, `maximumDrawableCount` (2 or 3 from the frame count), `framebufferOnly` true unless
      the swapchain image is read back (screenshots).
- [ ] **Acquire and present:** `Acquire` is `nextDrawable` (it can block: bounded by the layer's `allowsNextDrawableTimeout`, a timeout reported as
      the RHI's "surface not ready" result); present through `presentDrawable:` on the frame's command buffer; the drawable is never kept past its frame.
- [ ] **Present modes:** `Fifo` is `displaySyncEnabled = YES`; `Immediate` is `NO`; `Mailbox` falls back to `Fifo` with `ActualPresentMode` saying so
      (no Mailbox on macOS); tested through `ActualPresentMode`.
- [ ] **Resize and `Reconfigure`:** a resize serial change updates `drawableSize`; `Reconfigure` sets size, format, present mode and color space, and
      the next `nextDrawable` follows; `RecreateSurface` makes a new `CAMetalLayer` for the view (always possible).
- [ ] **Color space and HDR:** `colorspace` sRGB by default; `HdrMode::ScRgbLinear` maps to `RGBA16Float`, `kCGColorSpaceExtendedLinearSRGB` and
      `wantsExtendedDynamicRangeContent`, granted only when the screen reports `maximumPotentialExtendedDynamicRangeColorComponentValue > 1`; this MacBook's
      display is not EDR (record what it reports), so HDR stays policy-tested only; `GetColorSpace` answers.
- [ ] **Fullscreen and Retina:** the swapchain follows the native-fullscreen resize and a move between screens of different scale (checked by hand with
      an external display if one is available; recorded).
- [ ] **Tests:** `swapchain_reconfigure_test` and the shared presentation tests through Metal in the display group; `metal_swapchain_test.mm` (present-mode
      mapping, timeout path, drawable never held, re-created layer).
- [ ] **Documentation:** `rhi.md` "Presentation" Metal notes; `KNOWN-ISSUES.md` HDR line updated with this display's numbers; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: blocking in `nextDrawable` (never on a job worker without a bound), drawable lifetime,
      resize races between the main thread and recording jobs.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Metal swapchain presents into the Cocoa window, resizes, reconfigures, survives layer re-creation and fullscreen, reports its actual present
mode and color space, and the presentation tests pass in three flavors with validation silent.
