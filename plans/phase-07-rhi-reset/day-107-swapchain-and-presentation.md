# Phase 7 / Day 107 — Swapchain & Presentation Changes

**Phase:** [Phase 7 — RHI Reset](../phase-07-rhi-reset.md)
**Depends on:** Day 106.

Present mode, size, format, HDR and fullscreen change without a device reset where the API allows it; when a surface
itself must change, the window side supports it.

## Tasks

- [ ] `Swapchain::Reconfigure(const SwapchainDesc&)` replacing the resize-only path: present mode, extent and image format in one call, reusing the old swapchain handle as `oldSwapchain` on Vulkan; returns `Recreated`/`Ok`/`Minimized`/`Lost`; `Resize` becomes a thin wrapper. The null swapchain implements it with the same status rules.
- [ ] `SwapchainDesc` grows `format` (preferred, `Auto` by default) and `hdr` (off, scRGB-linear or HDR10 as capabilities allow); `vk_swapchain_policy` (pure) chooses among the surface's formats and color spaces; unsupported requests fall back with a log line and the *actual* result is readable (`ActualPresentMode()`, `ImageFormat()`, `ColorSpace()`).
- [ ] `SwapchainCaps` queried from the device and window: supported present modes, the format list, HDR support, so Day 104's `Capabilities` is true and the settings UI can grey out what is unavailable.
- [ ] Format change consequence: anything built against the old swapchain format (the tonemap pass pipeline, the pipeline target) is told through the same observer path (`OnSwapchainChanged`); the renderer rebuilds only those pipelines (`PipelineRebuild` class), tested on the null backend.
- [ ] **Window side:** check `engine/foundation/os/window.h`. Add `SetFullscreen(bool)` and `IsFullscreen()` with the Wayland (`xdg_toplevel_set_fullscreen`) and X11 (`_NET_WM_STATE_FULLSCREEN`) implementations, a resize event (existing resize serial) on completion, and a capability flag; a platform that cannot do it reports unsupported and the setting greys out. `BACKEND.md`-style checklists for Windows and macOS added next to the window headers (borderless versus exclusive fullscreen on Windows, native fullscreen on macOS). A live-session window test in the `display` resource group (a locked session breaks it; see `KNOWN-ISSUES.md`).
- [ ] **Surface re-creation:** a path for the window's native surface to be replaced (window re-created or the compositor loses it): `Swapchain` returns `Lost`, `DeviceHost` can `RecreateSurface(window)` without destroying the device when the adapter can still present to the new surface, otherwise it falls back to a full reset. Test on the null backend (injected surface loss) and, with a window, on Vulkan.
- [ ] Minimized and zero-extent windows during a reconfigure: no zero-sized swapchain is created; the request is remembered and applied when the window returns.
- [ ] Tests: present-mode change keeps rendering and the picture (Vulkan GPU test); a format change reports the actual format; unsupported HDR falls back and says so; zero-extent defers; the policy function tests are pure; X11 and Wayland fullscreen toggle tests (skip without a session); `Reconfigure` during a frame is rejected; ASan/TSan clean.
- [ ] **Documentation:** labels on every new function; `rhi.md` "Presentation" section; `platform-windowing-and-input.md` gets fullscreen; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected` and the asan/tsan flavors (the day touches the window and swapchain lifetime); quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; record it.
- [ ] Update `STATUS.md`; commit.

**Done:** present mode, size and format (including HDR where the surface offers it) change live through `Reconfigure`, fullscreen toggles on Wayland and X11 or reports unsupported, a lost surface is recoverable without a device reset when possible, and the capabilities are reported truthfully to the settings layer.
