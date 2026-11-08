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
platform backend plugs into (Phases 13, 22, 23 and 24, desktop and mobile/console
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

**Design note, `os::CommandLine` (Day 13, added 2026-08-16):** same seam
treatment as the `Window`/input interface above, for the same reason —
raw `argc`/`argv` isn't portable (GUI-subsystem Windows gets one command
string, Android gets launch args from an `Intent`, not process `argv` at
all). Parsing (flags/options/positional args) is one portable
implementation; only *sourcing* the initial argument list
(`CommandLine::FromNative()`) is per-platform, mirroring `os.h`'s
existing interface-header/per-platform-impl split. See
`docs/superpowers/specs/2026-08-16-command-line-wrapper-design.md`.

## Vulkan WSI surface creation (previously an open risk — now resolved)

Vulkan's WSI extensions (`VK_KHR_wayland_surface`, `VK_KHR_xcb_surface`)
take `struct wl_display*`/`struct wl_surface*` or `xcb_connection_t*` —
types owned by `libwayland-client`/`libxcb`. Since this phase links those
libraries directly (assumption #3) rather than hand-rolling the wire
protocol, those types are simply what the platform layer already has on
hand; no separate spike or driver-contract exception is needed going into
Phase 4.

## Days

Each day is its own file with a specific, checkable sub-task list and a
Done condition — see `CLAUDE.md`'s "How the plan is structured."

13. [Process lifecycle & argv](phase-01-linux-platform/day-13-process-lifecycle-argv.md)
14. [High-resolution timing](phase-01-linux-platform/day-14-high-resolution-timing.md)
15. [Threading primitives](phase-01-linux-platform/day-15-threading-primitives.md)
16. [File I/O](phase-01-linux-platform/day-16-file-io.md)
17. [GPU driver library loading](phase-01-linux-platform/day-17-gpu-driver-loading.md)
18. [Wayland connection & registry](phase-01-linux-platform/day-18-wayland-connection-registry.md)
19. [Wayland surface & xdg_shell window](phase-01-linux-platform/day-19-wayland-surface-xdg-shell.md)
20. [Wayland event loop](phase-01-linux-platform/day-20-wayland-event-loop.md)
21. [Wayland keyboard input](phase-01-linux-platform/day-21-wayland-keyboard-input.md)
22. [Wayland pointer input](phase-01-linux-platform/day-22-wayland-pointer-input.md)
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
  presentation; not needed for the Day 18-23 milestone, which only needs a
  surface to exist for Vulkan's swapchain to target.
- xdg-shell isn't part of the Wayland core protocol `libwayland-client`
  ships bindings for — it needs either `wayland-scanner`-generated code
  from the standard `xdg-shell.xml` protocol spec (a build-time step, not
  a new library dependency) or a small hand-written set of request/event
  wrappers for it. Decide which when Day 18 starts.
- **Thread creation/sync (resolved).** Day 15 uses `std::thread`/
  `std::mutex`/`std::condition_variable` directly (`PLAN.md` assumption
  #2) rather than wrapping `pthread` or raw `clone()`/`futex` — these are
  already portable across every target platform, so there's no
  per-platform piece to isolate here the way Vulkan/Wayland need one.
  Phase 2's job system builds its worker pool on top of this.
- **Windowing sequenced last within this phase.** Wayland (Days 18-22)
  and X11 (Days 24-25) come after the non-windowing platform primitives
  (timing, threading, file I/O, GPU driver loading — Days 14-17), not
  interleaved with them — the windowing backends are the most
  external-library-dependent and highest-risk part of this phase, so the
  simpler, more self-contained primitives are built and proven first.
