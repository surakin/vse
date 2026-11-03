# Phase 1 / Day 24 — X11 Fallback: Connection & Window

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 23 (Wayland "hello window" milestone) — same
internal `Window` interface this reuses/implements against.

## Tasks

- [x] Add `libX11` (headers + link) to CMake (`pkg_check_modules(x11)`, `PUBLIC` on
  `foundation`/`foundation_asan`).
- [x] Call `XOpenDisplay(nullptr)`; confirm a non-null `Display*`,
  logging an error and exiting cleanly if it's null.
- [x] Call `XCreateWindow`/`XCreateSimpleWindow` to create the window;
  `XSelectInput` for the event mask needed (exposure, structure
  notify, key/button/pointer).
- [x] Call `XMapWindow` to show it.
- [x] Register `WM_DELETE_WINDOW` via `XSetWMProtocols` so the WM's
  close button sends a `ClientMessage` instead of killing the
  connection outright.
- [x] Handle `Expose`, `ConfigureNotify` (resize — log new dimensions),
  and `ClientMessage` (check for `WM_DELETE_WINDOW`, exit cleanly).
- [ ] Build and run under an X11 session/Xwayland; visually confirm a
  window appears, resize it and confirm dimensions log, close it via
  the WM and confirm clean exit. **Partially done.** Implemented as
  `os::x11::X11Window` (`os/linux/x11/`); `engine_main --backend=x11`
  (temporary flag until Day 26) runs it. Verified automatically — 7
  Catch2 tests passing both on this machine's Xwayland (with KWin as WM)
  and a bare Xvfb: window really becomes viewable (queried via a second
  connection), a second client's `WM_DELETE_WINDOW` ClientMessage sets
  `ShouldClose()`, other protocol messages don't, and an external
  `XResizeWindow` is tracked via `ConfigureNotify`. `engine_main` and
  `engine_main_asan` run clean with `--backend=x11`, exit 0. **Not
  done:** eyeballing the window and clicking the WM's real close button
  (no screen access in this session). Resize logging is `Info`-level on
  `LogPlatform` (hidden by the default `Warn`), same as Wayland's.
- [x] Commit.

**Done:** a window appears, resizing logs new dimensions, WM close
exits cleanly.
