# Phase 24 / Day 370 — macOS: Scenes on an `NSView` (Metal)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-368 built on Linux; Phases 21, 23 Day 355. **Done on the Mac.**

The scene features on macOS with Metal.

## Tasks

- [x] Build and run every test of Days 359-368 on `macos-clang` (plain; sanitizers as the memory notes allow) and the arm64 compile check; fix what differs.
- [x] Metal validation stays on in the tests (CLAUDE.md); goldens under the tolerance of Day 358; the Metal shaders for the new pipelines (labels, per-slot override, skinned) are minted on the Mac (`tools/metal_mint`).
- [x] Host textures on Metal (shared storage, `replaceRegion`), the reset path, the Cocoa C host (`host_cocoa.m`) forwarding input into the scene.
- [x] Record in `KNOWN-ISSUES.md` what could not be done.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** (plain only, and the unity build of both trees: asan, tsan, unity off, the arm64 compile and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` not run, `KNOWN-ISSUES.md`) `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Mac; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the phase's scene tests pass on the Mac with Metal validation on, or each failure is recorded with its observed behavior.

## Outcome

**Done (2026-11-16, the Mac), plain flavor only.** Before the day the Mac built none of the phase's scene tests: `host_scene.c` (Days 359-368) and
`walk_around.c` (Day 371) opened X11 windows themselves, so only Day 358's spikes ran here.

- **The hosts' window, per platform.** `tests/engine_library/host_window.h` (open the window system, open and close a window of a size in pixels, describe
  it as an `engine_surface_info`, poll and post the walk-around's keys and pointer): `linux/host_window_x11.c` (Xlib, the code the two hosts had; it now
  waits for `MapNotify` and selects input only on a window that is polled) and `macos/host_window_cocoa.m` (an `NSWindow` whose plain `NSView` gets the
  engine's `CAMetalLayer`, sized in pixels at the window's own scale, shown before it returns). `host_scene.c` and `walk_around.c` have no X11 code left;
  `tests/engine_library/CMakeLists.txt` builds them with the X11 helper on Linux (plain, asan, as before) and the Cocoa one on the Mac (plain, asan, tsan,
  Metal's validation on).
- **Results on Metal:** `engine_c_scene` passes against the same golden image (largest difference 2, 96 of 76,800 values off by more than 1: within the
  rule of Day 358); `engine_c_walk_around --frames 40` passes (rebind, labels, three screens painted by threads, `textures_uploaded` for each).
- **Input into the scene:** in test mode the walk-around posts a W press and release to its own window through the window system (`host_window_post_key`)
  and checks that the camera moved forward; the check fails when the event is not delivered (seen: `z 6.00 before, 6.00 after`).
- **Shaders:** no shader under `shaders/` changed in Phase 24 and `shaders_check` passes: nothing to mint.
- **Host textures on Metal:** the RHI's upload (`engine_c/live_textures.*`, staging ring and a copy per update) works unchanged; no Metal-specific path.
- **Runs:** `scripts/test-phase.sh --affected --flavor plain`: `ran 3206 of 8741 tests (phases: all; flavors: plain; display: included; crash tests:
  excluded) in 431 s`, one failure, the filed flaky `Throughput scales with participant count` (passed alone); after the review's fixes (test hosts only)
  the plain `engine_library` tests of both trees, 72 of 72 each, and `std_usage_check`, `file_access_check`, `platform_neutral_check`; `ctest -L docs` 7
  of 7. The X11 helper was compiled with `-Wall -Wextra -Werror` against XQuartz's headers; it was not run (the Linux machine runs it).
- **Not done:** asan and tsan (the Mac's standing rule), the unity-off tree, the arm64 compile and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` (`KNOWN-ISSUES.md`).
- **Docs:** `docs/guide/engine-library.md` (the scene hosts on each platform, the posted key). No public header changed: the ABI snapshot and the reference
  are unchanged.

## Post-implementation code review

`/code-review high` over the day's diff: **10 findings, all fixed** (test hosts and their window helper only).

1. `walk_around --frames 15` failed: the release posted after frame 15 was never polled. The check runs for `frames_limit > 15` (15 and 16 both pass now).
2. The Cocoa window was sized with the main screen's scale and described with its own; it is sized from the window's `backingScaleFactor` now.
3. A failed `host_window_open` for a second window would crash in `host_window_describe`; a NULL window describes as kind 0, which the engine refuses.
4. A failed first window returned without destroying the engine; both hosts destroy it and end the window system first.
5. The Cocoa poll swallowed every key, so Command-Q and the other key equivalents never reached AppKit; only the five mapped plain keys are taken.
6. The X11 helper selected key and pointer input on every window (`host_scene`'s are never polled); input is selected on the first poll or post.
7. The header promised a wait until the window is shown, which X11 did not do; it waits for `MapNotify` (two seconds at most).
8. Real pointer motion during a test could turn the camera and fail the key check; test mode ignores motion.
9. The Cocoa open waited a fixed 0.2 s; it waits until the window is visible (two seconds at most).
10. The Cocoa helper linked QuartzCore, which it does not use, and not CoreGraphics, which `CGSessionCopyCurrentDictionary` needs; fixed.

Findings 1 and 8 were checked by running the program; 6 and 7 are X11 code compiled here and run only on the Linux machine.
