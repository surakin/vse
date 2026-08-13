# Phase 1 / Day 24 — X11 Fallback: Connection & Window

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 23 (Wayland "hello window" milestone) — same
internal `Window` interface this reuses/implements against.

## Tasks

- [ ] Add `libX11` (headers + link) to CMake.
- [ ] Call `XOpenDisplay(nullptr)`; confirm a non-null `Display*`,
  logging an error and exiting cleanly if it's null.
- [ ] Call `XCreateWindow`/`XCreateSimpleWindow` to create the window;
  `XSelectInput` for the event mask needed (exposure, structure
  notify, key/button/pointer).
- [ ] Call `XMapWindow` to show it.
- [ ] Register `WM_DELETE_WINDOW` via `XSetWMProtocols` so the WM's
  close button sends a `ClientMessage` instead of killing the
  connection outright.
- [ ] Handle `Expose`, `ConfigureNotify` (resize — log new dimensions),
  and `ClientMessage` (check for `WM_DELETE_WINDOW`, exit cleanly).
- [ ] Build and run under an X11 session/Xwayland; visually confirm a
  window appears, resize it and confirm dimensions log, close it via
  the WM and confirm clean exit.
- [ ] Commit.

**Done:** a window appears, resizing logs new dimensions, WM close
exits cleanly.
