# Phase 1 — Linux Platform Layer

**Depends on:** Phase 0 (Foundation).
**Goal:** window, input, time, threads, and files on Linux, built directly
on raw syscalls and wire protocols. **Wayland is the primary display
backend; X11 is a fallback backend** for systems/WMs that haven't moved to
Wayland — see root `PLAN.md` assumption #3.
**Definition of done:** the "hello window" milestone (Day 24 for Wayland,
Day 28 for X11 fallback) passes identically under both backends.

## Open risk — Vulkan WSI surface creation

Vulkan's WSI extensions (`VK_KHR_wayland_surface`, `VK_KHR_xcb_surface`)
take `struct wl_display*`/`struct wl_surface*` or `xcb_connection_t*` —
types owned by `libwayland-client`/`libxcb`, because GPU driver WSI
implementations call back into those libraries internally (e.g. for
`linux-dmabuf` negotiation). A hand-rolled raw-protocol client may not be
able to satisfy this without linking those libraries narrowly, as a
driver-contract exception rather than a general convenience dependency.
**Spike this at the very start of Phase 2** before assuming either backend
can hand a swapchain a valid surface. If it turns out unavoidable, scope
it as "linking the display-protocol client library is part of the GPU
driver contract, same as the driver library itself" and update assumption
#3 accordingly — don't let it block Phase 2 in silence.

## Tasks

- [ ] **Day 13 — Process lifecycle & argv.** Real `_start` reading
  argc/argv/envp off the stack per the System V ABI, calling engine `main`.
  **Done:** engine prints its received command-line arguments.

- [ ] **Day 14 — Unix domain socket primitives.** Raw `socket`/`connect`/
  `sendmsg`/`recvmsg` syscall wrappers, reused by both the Wayland and X11
  backends below.
  **Done:** connects successfully to a well-known local Unix socket (e.g.
  the Wayland socket at `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`).

- [ ] **Day 15 — Wayland connection & registry.** Implement the Wayland
  wire protocol handshake: connect to `$WAYLAND_DISPLAY`, `wl_display.
  get_registry`, bind `wl_compositor`, `xdg_wm_base`, `wl_seat`, `wl_shm`
  globals.
  **Done:** registry global names/interfaces for the above are printed
  correctly.

- [ ] **Day 16 — Wayland surface & xdg_shell window.** `wl_compositor.
  create_surface`, `xdg_wm_base.get_xdg_surface`, `xdg_surface.
  get_toplevel`, initial `configure`/`ack_configure`/`commit` handshake.
  **Done:** a real window appears on screen via a Wayland compositor.

- [ ] **Day 17 — Wayland event loop.** Handle `xdg_surface.configure`,
  `xdg_toplevel.configure` (resize), `xdg_toplevel.close`.
  **Done:** resizing logs new dimensions; the compositor's close affordance
  exits cleanly.

- [ ] **Day 18 — Wayland keyboard input.** `wl_seat` → `wl_keyboard`;
  handle the `keymap` event (compositor hands you a memory-mapped XKB
  keymap file). Hand-parse enough of the XKB text keymap format to map
  keycodes to symbols for common (e.g. US QWERTY) layouts — full XKB
  compliance (dead keys, complex layouts, layout switching) is deferred.
  **Done:** key presses on a standard layout print correct engine key enum
  values.

- [ ] **Day 19 — Wayland pointer input.** `wl_pointer` motion/button/axis
  events.
  **Done:** an `InputState` struct accurately reflects live mouse
  position/buttons/scroll.

- [ ] **Day 20 — High-resolution timing.** `clock_gettime(CLOCK_MONOTONIC)`
  wrapper and frame delta-time calculation.
  **Done:** the main loop logs a stable delta-time under a frame limiter.

- [ ] **Day 21 — Threading primitives.** `clone()`-based thread creation;
  futex-based mutex and condition variable.
  **Done:** a worker thread and the main thread increment a shared counter
  under the mutex with no races across repeated stress runs.

- [ ] **Day 22 — File I/O.** `open`/`read`/`write`/`close`/`lseek` wrappers;
  a "read whole file into an arena buffer" utility.
  **Done:** reads its own executable's ELF header and verifies the magic
  number.

- [ ] **Day 23 — GPU driver library loading.** Load `libvulkan.so` via the
  OS dynamic loader; resolve `vkGetInstanceProcAddr`. Resolve the Day-16
  open risk here in practice: confirm what `VK_KHR_wayland_surface` needs
  and document the outcome.
  **Done:** `vkGetInstanceProcAddr` resolves and successfully fetches
  `vkCreateInstance`.

- [ ] **Day 24 — "Hello window" milestone (Wayland).** Ties the primary
  backend together: open a window, run the event loop, respond to
  keyboard/mouse input, exit cleanly on compositor-close.
  **Done:** window opens, stays responsive, closes cleanly under Wayland.

- [ ] **Day 25 — X11 fallback: handshake.** Implement the X11 client
  connection setup request/response over the Day-14 socket layer,
  including `.Xauthority` MIT-COOKIE authentication.
  **Done:** parses and prints the server's vendor string, root window ID,
  and screen dimensions from the Setup response.

- [ ] **Day 26 — X11 fallback: window & event loop.** `CreateWindow`,
  `MapWindow`; handle `Expose`, `ConfigureNotify`, `ClientMessage` (for
  `WM_DELETE_WINDOW`).
  **Done:** a window appears, resizing logs new dimensions, WM close exits
  cleanly.

- [ ] **Day 27 — X11 fallback: input.** `KeyPress`/`KeyRelease` mapped via
  `GetKeyboardMapping` (minimal viable mapping, no XKB), `ButtonPress`/
  `Release`/`MotionNotify` for the mouse — feeding the same `InputState`
  struct as the Wayland backend.
  **Done:** key/mouse state under X11 matches the Wayland backend's
  behavior for the same interactions.

- [ ] **Day 28 — Backend selection & fallback logic.** Detect
  `$WAYLAND_DISPLAY` at startup; if unset or the connection fails, fall
  back to `$DISPLAY`/X11. Both backends implement one internal `Window`
  interface so upper layers are backend-agnostic.
  **Done:** the Day-24 "hello window" milestone passes identically when
  each backend is forced explicitly (env var override for testing).

## Notes / open questions

- Full XKB compliance (layouts beyond common QWERTY, dead keys) is
  explicitly deferred — revisit if/when needed.
- Wayland's `linux-dmabuf` protocol may matter later for zero-copy
  presentation; not needed for the Day 15-24 milestone, which only needs a
  surface to exist for Vulkan's swapchain to target.
