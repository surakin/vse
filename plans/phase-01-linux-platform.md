# Phase 1 — Linux Platform Layer

**Depends on:** Phase 0 (Foundation).
**Goal:** window, input, time, threads, and files on Linux, built on
libc plus the reference Wayland/X11 client libraries. **Wayland is the
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

## Days

Each day is its own file with a specific, checkable sub-task list and a
Done condition — see `CLAUDE.md`'s "How the plan is structured."

13. [Process lifecycle & argv](phase-01-linux-platform/day-13-process-lifecycle-argv.md)
14. [Wayland connection & registry](phase-01-linux-platform/day-14-wayland-connection-registry.md)
15. [Wayland surface & xdg_shell window](phase-01-linux-platform/day-15-wayland-surface-xdg-shell.md)
16. [Wayland event loop](phase-01-linux-platform/day-16-wayland-event-loop.md)
17. [Wayland keyboard input](phase-01-linux-platform/day-17-wayland-keyboard-input.md)
18. [Wayland pointer input](phase-01-linux-platform/day-18-wayland-pointer-input.md)
19. [High-resolution timing](phase-01-linux-platform/day-19-high-resolution-timing.md)
20. [Threading primitives](phase-01-linux-platform/day-20-threading-primitives.md)
21. [File I/O](phase-01-linux-platform/day-21-file-io.md)
22. [GPU driver library loading](phase-01-linux-platform/day-22-gpu-driver-loading.md)
23. ["Hello window" milestone (Wayland)](phase-01-linux-platform/day-23-hello-window-wayland.md)
24. [X11 fallback: connection & window](phase-01-linux-platform/day-24-x11-connection-window.md)
25. [X11 fallback: input](phase-01-linux-platform/day-25-x11-input.md)
26. [Backend selection & fallback logic](phase-01-linux-platform/day-26-backend-selection-fallback.md)

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
- **Thread creation/sync (resolved).** Day 20 uses libc's `pthread` API
  (`pthread_create`, `pthread_mutex_t`, `pthread_cond_t`) rather than raw
  `clone()`/`futex` — same libc-preference reasoning as Phase 0's
  Day 1/2. Phase 2's job system builds its worker pool on top of this.
