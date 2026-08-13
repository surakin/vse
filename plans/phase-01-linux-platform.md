# Phase 1 — Linux Platform Layer

**Depends on:** Phase 0 (Foundation).
**Goal:** window, input, time, threads, and files on Linux, built on raw
syscalls plus the reference Wayland/X11 client libraries. **Wayland is the
primary display backend; X11 is a fallback backend** for systems/WMs that
haven't moved to Wayland — see root `PLAN.md` assumption #3.
**Definition of done:** the "hello window" milestone (Day 23 for Wayland,
Day 26 for X11 fallback) passes identically under both backends.

**Design note (root `PLAN.md` assumption #11):** the internal `Window`/
input interface this phase defines (Day 26) is the seam *every* later
platform backend plugs into (Phases 10-13, desktop and mobile/console
alike) — keep its shape generic rather than mirroring Wayland/X11-specific
concepts upward, so those later ports are additive backends, never a
redesign. Concretely: the window model should have room for
lifecycle events beyond open/resize/close/focus (suspend/resume/
low-memory, relevant to mobile even though Linux desktop never fires
them), and the input model should have room for touch and gamepad
alongside key/mouse/scroll, even though only keyboard/mouse are wired up
in this phase. Unused event/input variants on desktop are fine; a missing
seam that forces a later interface change is what this note exists to
prevent.

## Vulkan WSI surface creation (previously an open risk — now resolved)

Vulkan's WSI extensions (`VK_KHR_wayland_surface`, `VK_KHR_xcb_surface`)
take `struct wl_display*`/`struct wl_surface*` or `xcb_connection_t*` —
types owned by `libwayland-client`/`libxcb`. Since this phase links those
libraries directly (assumption #3) rather than hand-rolling the wire
protocol, those types are simply what the platform layer already has on
hand; no separate spike or driver-contract exception is needed going into
Phase 3.

## Tasks

- [ ] **Day 13 — Process lifecycle & argv.** Real `_start` reading
  argc/argv/envp off the stack per the System V ABI, calling engine `main`.
  **Done:** engine prints its received command-line arguments.
  **Flagged for re-evaluation:** this task predates Phase 0's resolved
  libc decision (see `PLAN.md` assumption #2 and
  `plans/phase-00-foundation.md`'s notes) — Phase 0 now links libc
  normally with a standard `main()` entry, so a hand-rolled `_start`
  may be unnecessary duplication of what libc's own CRT startup already
  does. Re-decide this when Phase 1 is expanded into day files, not
  before.

- [ ] **Day 14 — Wayland connection & registry.** Link `libwayland-client`;
  `wl_display_connect`, `wl_display_get_registry`, and a registry listener
  that binds `wl_compositor`, `xdg_wm_base` (via generated/vendored
  xdg-shell protocol bindings — `wayland-scanner` output or a thin
  hand-written equivalent), `wl_seat`, `wl_shm` globals.
  **Done:** registry globals for the above are bound successfully and
  logged.

- [ ] **Day 15 — Wayland surface & xdg_shell window.** `wl_compositor_
  create_surface`, `xdg_wm_base_get_xdg_surface`, `xdg_surface_
  get_toplevel`, initial `configure`/`ack_configure`/`wl_surface_commit`
  handshake via the library's request wrappers.
  **Done:** a real window appears on screen via a Wayland compositor.

- [ ] **Day 16 — Wayland event loop.** Integrate `wl_display_dispatch`/
  `dispatch_pending` into the engine's main loop; handle `xdg_surface`
  configure, `xdg_toplevel` configure (resize) and close callbacks.
  **Done:** resizing logs new dimensions; the compositor's close affordance
  exits cleanly.

- [ ] **Day 17 — Wayland keyboard input.** `wl_seat_get_keyboard`; handle
  the `keymap` event by loading it through `libxkbcommon` (the standard
  companion library for interpreting a Wayland keymap — same "core
  system library" category as `libwayland-client`, see root `PLAN.md`
  assumption #1) instead of hand-parsing the XKB text format. Feed
  keycodes through `xkb_state` for symbol translation.
  **Done:** key presses on any real keyboard layout (not just US QWERTY)
  produce correct engine key enum values — full XKB compliance is in
  scope now that a real XKB library is doing the work.

- [ ] **Day 18 — Wayland pointer input.** `wl_pointer` motion/button/axis
  listener callbacks.
  **Done:** an `InputState` struct accurately reflects live mouse
  position/buttons/scroll.

- [ ] **Day 19 — High-resolution timing.** `clock_gettime(CLOCK_MONOTONIC)`
  wrapper and frame delta-time calculation.
  **Done:** the main loop logs a stable delta-time under a frame limiter.

- [ ] **Day 20 — Threading primitives.** `clone()`-based thread creation;
  futex-based mutex and condition variable.
  **Done:** a worker thread and the main thread increment a shared counter
  under the mutex with no races across repeated stress runs.

- [ ] **Day 21 — File I/O.** `open`/`read`/`write`/`close`/`lseek` wrappers;
  a "read whole file into an arena buffer" utility.
  **Done:** reads its own executable's ELF header and verifies the magic
  number.

- [ ] **Day 22 — GPU driver library loading.** Load `libvulkan.so` via the
  OS dynamic loader; resolve `vkGetInstanceProcAddr`.
  **Done:** `vkGetInstanceProcAddr` resolves and successfully fetches
  `vkCreateInstance`.

- [ ] **Day 23 — "Hello window" milestone (Wayland).** Ties the primary
  backend together: open a window, run the event loop, respond to
  keyboard/mouse input, exit cleanly on compositor-close.
  **Done:** window opens, stays responsive, closes cleanly under Wayland.

- [ ] **Day 24 — X11 fallback: connection & window.** Link `libX11`;
  `XOpenDisplay`, `XCreateWindow`/`XCreateSimpleWindow`, `XSelectInput`,
  `XMapWindow`; register `WM_DELETE_WINDOW` via `XSetWMProtocols`; handle
  `Expose`, `ConfigureNotify`, `ClientMessage`.
  **Done:** a window appears, resizing logs new dimensions, WM close exits
  cleanly.

- [ ] **Day 25 — X11 fallback: input.** `KeyPress`/`KeyRelease` via
  `XLookupString`/`Xutf8LookupString` (full keysym translation, not a
  minimal hand-rolled mapping), `ButtonPress`/`Release`/`MotionNotify` for
  the mouse — feeding the same `InputState` struct as the Wayland backend.
  **Done:** key/mouse state under X11 matches the Wayland backend's
  behavior for the same interactions.

- [ ] **Day 26 — Backend selection & fallback logic.** Detect
  `$WAYLAND_DISPLAY` at startup; if unset or the connection fails, fall
  back to `$DISPLAY`/X11. Both backends implement one internal `Window`
  interface so upper layers are backend-agnostic.
  **Done:** the Day-23 "hello window" milestone passes identically when
  each backend is forced explicitly (env var override for testing).

## Notes / open questions

- **`libxkbcommon` is a new named dependency, not explicitly called out by
  the user when approving `libc`/`libwayland`/`libx11`.** It's included
  here because it's the reference library Wayland compositors expect
  clients to use for keymap interpretation — same "ships with the
  OS/distro" category — but flag/confirm this explicitly rather than
  treating it as silently pre-approved.
- Wayland's `linux-dmabuf` protocol may matter later for zero-copy
  presentation; not needed for the Day 14-23 milestone, which only needs a
  surface to exist for Vulkan's swapchain to target.
- xdg-shell isn't part of the Wayland core protocol `libwayland-client`
  ships bindings for — it needs either `wayland-scanner`-generated code
  from the standard `xdg-shell.xml` protocol spec (a build-time step, not
  a new library dependency) or a small hand-written set of request/event
  wrappers for it. Decide which when Day 14 starts.
