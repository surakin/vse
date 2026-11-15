# Phase 23 / Day 352 — Rendering into a Host Window: the Foreign Surface on Linux

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 349 and Phases 4, 8, 13.

The RHI renders into a native window the host owns (X11 and Wayland), recovers from loss, and can read the frame back.

## Tasks

- [ ] RHI: a `SurfaceDescriptor` (a versioned, size-prefixed struct: `kind` (X11 window with its display, Wayland `wl_surface` with its `wl_display`, later `HWND`, `NSView`/`CAMetalLayer`), the handles, the size and the scale) replaces `os::Window` in swapchain creation and in `RecreateSurface`; the engine's own windows build a descriptor from `os::Window` (no change in behavior); Vulkan on X11 (`VK_KHR_xlib_surface` or xcb) and Wayland (`VK_KHR_wayland_surface`, **using the host's connection**, no second connection), the platform-neutral and RHI boundary gates extended to the foreign-handle types, `BACKEND.md` checklists updated, Metal and DX12 parity noted in `KNOWN-ISSUES.md` until Days 354-355.
- [ ] The host owns size, visibility and DPI: `engine_surface_resized(engine*, surface, width, height, scale)`; the engine reports out-of-date and lost surfaces through a status and recovers through the Phase 8 reset path; the engine never creates, resizes or destroys the host's window.
- [ ] CPU readback: `engine_surface_read_pixels(...)` (also the screenshot and test path), asynchronous with a ticket, an RGBA8 buffer owned by the caller.
- [ ] `engine_caps` reports `SURFACE_X11`, `SURFACE_WAYLAND`, `READBACK`; a backend that cannot refuses with a clear status.
- [ ] Tests: a C host that opens its own X11 window (Xlib) and, separately, its own Wayland window, embeds the engine, renders a known scene (a cube from the demo's data), reads the pixels back and compares them with the golden image; resizes; destroys the swapchain by unmapping and recovers; locked-session caveat as for the other window tests (`KNOWN-ISSUES.md`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the engine renders a known scene into a host-owned X11 and Wayland window, survives resize and surface loss, and the readback matches the golden image.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
