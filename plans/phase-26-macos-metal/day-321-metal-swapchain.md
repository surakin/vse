# Phase 26 / Day 321 — Metal VI: `CAMetalLayer` Swapchain and Presentation

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 320.

Presentation on Metal: the swapchain over the Cocoa window's `CAMetalLayer`, `nextDrawable` as `Acquire`, present modes, resize, `Reconfigure`, surface
re-creation and EDR. Modelled on DX12 Day 169.

## Tasks

- [x] **Swapchain** (`metal_swapchain.mm`): from the window's opaque layer pointer (Day 311): `device`, `pixelFormat` (`BGRA8Unorm` or `BGRA8Unorm_sRGB`
      per the requested format), `drawableSize` from the backing size, `maximumDrawableCount` (2 or 3 from the frame count), `framebufferOnly` true unless
      the swapchain image is read back (screenshots).
- [x] **Acquire and present:** `Acquire` is `nextDrawable` (it can block: bounded by the layer's `allowsNextDrawableTimeout`, a timeout reported as
      the RHI's "surface not ready" result); present through `presentDrawable:` on the frame's command buffer; the drawable is never kept past its frame.
- [x] **Present modes:** `Fifo` is `displaySyncEnabled = YES`; `Immediate` is `NO`; `Mailbox` falls back to `Fifo` with `ActualPresentMode` saying so
      (no Mailbox on macOS); tested through `ActualPresentMode`.
- [x] **Resize and `Reconfigure`:** a resize serial change updates `drawableSize`; `Reconfigure` sets size, format, present mode and color space, and
      the next `nextDrawable` follows; `RecreateSurface` makes a new `CAMetalLayer` for the view (always possible).
- [x] **Color space and HDR:** `colorspace` sRGB by default; `HdrMode::ScRgbLinear` maps to `RGBA16Float`, `kCGColorSpaceExtendedLinearSRGB` and
      `wantsExtendedDynamicRangeContent`, granted only when the screen reports `maximumPotentialExtendedDynamicRangeColorComponentValue > 1`; this MacBook's
      display is not EDR (record what it reports), so HDR stays policy-tested only; `GetColorSpace` answers.
- [x] **Fullscreen and Retina:** (Day 325, by the user: native fullscreen in and out through `renderer_demo` F5 and `ui_demo`'s green button, the picture right; EDR potential 2.00, now 1.00. A move between screens of different scale is not checked: one display) ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: (by hand, with the Day 325 run: the user is needed at the Mac) the swapchain follows the native-fullscreen resize and a move between screens of different scale (checked by hand with
      an external display if one is available; recorded).
- [x] **Tests:** (the Metal file; `swapchain_reconfigure_test` and the shared presentation tests join through the GPU fixture on Day 322) `swapchain_reconfigure_test` and the shared presentation tests through Metal in the display group; `metal_swapchain_test.mm` (present-mode
      mapping, timeout path, drawable never held, re-created layer).
- [x] **Documentation:** `rhi.md` "Presentation" Metal notes; `KNOWN-ISSUES.md` HDR line updated with this display's numbers; `docgen lint` zero.
- [x] **Runs:** `ran 6110 of 6490 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 1479 s` ([the Day 325 run](day-325-metal-lifecycle.md#the-run-of-days-315-325)). Originally: `scripts/test-phase.sh --affected --flavor all` with the display group (quote). **Deferred to Day 325 (the user's choice; the user is asked first).**
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: blocking in `nextDrawable` (never on a job worker without a bound), drawable lifetime,
      resize races between the main thread and recording jobs.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** the Metal swapchain presents into the Cocoa window, resizes, reconfigures, survives layer re-creation and fullscreen, reports its actual present
mode and color space, and the presentation tests pass in three flavors with validation silent.

## Plan amendments (made during the day)

- **`framebufferOnly` is always off,** not "true unless read back": the RHI cannot know at creation whether an image will be copied (a screenshot
  copies the swapchain image on the other backends), so it is off, as `CopySrc` usage of a swapchain image needs.
- **Present uses a command buffer of its own,** committed after the frame's lists (the queue keeps the order), not the frame's last list: the RHI's
  `Present` is separate from `Submit` and may follow several submissions.
- **`RecreateSurface` re-binds to the window's existing layer** instead of making a new `CAMetalLayer`: the layer belongs to the window's view and
  takes any Metal device, and replacing it would leave the window's own pointer to it (`GetNativeInfo().surface`) stale.
- **The `nextDrawable` bound is one second,** the layer's own timeout, which a program cannot shorten; the RHI's `Acquire` contract (half a
  second elsewhere) now says so.

## Implementation notes

- **`metal_swapchain.mm`, `metal_device_swapchain.mm`:**
  - **The layer:** the window's `CAMetalLayer`, configured with the device, pixel format, color space, EDR, display sync, drawable count (2 or 3)
    and drawable size.
  - **The texture:** one handle (an external texture) follows the current drawable, which `Acquire` takes and `Present` gives up.
  - **Resizes:** a window resize changes only the drawable size, at the next `Acquire`, which says `Recreated` once.
  - **Formats and HDR:** a request the layer cannot take falls back with a log line (`Mailbox` to `Fifo`, other formats to `bgra8_srgb`, HDR to off
    without EDR). The capabilities are read when the swapchain is built.
  - **Loss:** `lose_device_at_present` is honored, and a lost device makes `Reconfigure` and `Resize` say `Lost`.
- **`rhi_metal` links AppKit** (`NSScreen`'s EDR values, `NSView`'s window).
- **Tests added** (run on Day 325, display group): `tests/rhi/macos/metal_swapchain_test.mm` covers:
  - the present-mode mapping;
  - frames into a real window, with the drawable never held;
  - every reconfiguration, and HDR as the screen allows (it prints the screen's EDR values for the record);
  - an explicit resize, then the window's;
  - the surface re-created;
  - a minimized window;
  - the injected loss at the third present.
  `tests/rhi/macos/metal_test_support.h` now imports AppKit first, because its QuickDraw headers' global names clashed with a unity-merged test's.
- **Docs:** `docs/guide/rhi.md` (presentation); the `Acquire` contract in `rhi/swapchain.h`; the reference regenerated.
- **Built:** both unity modes on macOS, every flavor, no warning. **Runs: deferred to Day 325.**

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings, all fixed.

1. **After an explicit `Resize`, a window resize kept the old size.** Fixed: a window resize gives the window's size, as on the other backends;
   tested.
2. **`nextDrawable` can wait a second, not the contract's half.** Fixed in the contract's text (Metal's timeout cannot be shortened).
3. **`Present` and the next `Acquire` both said `Recreated` for one resize.** Fixed: only `Acquire` says it; tested.
4. **`lose_device_at_present` was ignored.** Fixed; tested.
5. **`Reconfigure` and `Resize` said `Ok` on a lost device.** Fixed; tested.
6. **Three departures from the plan were not recorded.** Recorded above, with the timeout.
7. **Every resize re-ran the whole configuration** (logging the fallbacks again). Fixed: a resize changes only the drawable size.
8. **`Capabilities` asked AppKit from any thread, and a null check was dead.** Fixed: the EDR answer is read in `Apply` and cached.
9. **`Initialize` checked a status `Apply` never returns.** Fixed.
10. **A hand-written move.** Fixed: `Move`.
