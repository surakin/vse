# Phase 24 / Day 382 — macOS: Off-Screen Surfaces and Multi-Material Meshes (Metal)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373-380 built on Linux; Phase 21; Day 381. **Done on the Mac.**

Both features on Metal.

## Tasks

- [x] Pull and build `macos-clang` (x86-64; plain tests only for now) and `macos-clang-arm64` compile-only in both unity modes; Metal code only under `engine/rhi/metal/` and `*/macos/`. **Partly:** `build-macos` (x86-64, unity) only; the arm64 trees and the unity-off tree were skipped by the user (2026-11-17). **Done later on 2026-11-17** (with the Phase 25 closing runs, at Day 426): all four trees clean.
- [x] Metal: the off-screen colour target, the blit into the ring (shared-storage buffers, a completion handler per slot), the row layout, `@autoreleasepool` at every entry point; validation on (`MTL_DEBUG_LAYER=1`...).
- [x] Golden images on Metal under the tolerance of Day 358; the Metal-versus-Vulkan differences recorded. **Not done:** the goldens are in `host_scene`, a window test, and the Mac's console was locked; the user skipped the tests. **Done later on 2026-11-17, unlocked:** `engine_c_scene` passed on Metal with validation: `against the golden image: largest difference 2, 96 values off by more than 1` (the golden is the Vulkan one), and its two-surface device-loss part passed.
- [x] The Cocoa C host's walk-around sample draws its off-screen mode. (written; not run: the console was locked)
- [x] Record in `KNOWN-ISSUES.md` what could not be done; the Mac console must be unlocked for window tests.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The Day 374-380 tests pass on the Mac on Metal, or each failure is recorded with its observed behavior.

## Outcome

Done on the Mac (2026-11-17, `master` at `623dbe1d`), in `build-macos` only (x86-64, unity, plain), the user's instruction; the Mac's console was locked and the user could not unlock it, so the window tests were not run and the user skipped the remaining tests.

**Metal needed no change.** The addendum changed no RHI code: the off-screen surface (`engine/engine_c/surface.cpp`) uses `CreateTexture`, `CopyTextureToBuffer`, `MapBuffer`, `CompletedFence` and `ReadbackTexture`, which the Metal backend already has (with its `@autoreleasepool` entry points from Phases 21 and 23). Run on Metal with validation on (`MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert MTL_SHADER_VALIDATION=1`), each printing its own `ok`:
- `engine_c_offscreen` (`host_offscreen.c`: the frame ring, sizes, resize, the slow host, the injected device loss and its recovery);
- `engine_c_acceptance_scene` (`host_acceptance_scene.c`: meadow and three robots, per-robot `Screen` slot textures, 1280x720 off-screen, a resize, a lost device, a slow host);
- the multi-material and slot tests matched by `ctest -R "index ranges|Index ranges|slot|Slot|ranges"`: 35 of 35 (among them `A mesh's index ranges draw as separate draws and instance by range` and the level and glTF slot tests).

**Not run (locked console):** `engine_c_scene` (the Day 358 golden `scene_rock_160x120.rgba` and Day 377's `TestMeshSlotMaterials`), `engine_c_walk_around` and `engine_c_walk_around_offscreen`: they printed `skipped: the console is locked (unlock the screen)`, which their ctests count as a pass. So no golden image was compared on Metal for the addendum, and no Metal-versus-Vulkan difference is recorded. The scoped `scripts/test-phase.sh --affected --flavor plain` was started and stopped unfinished (the user: skip the tests); no `ran X of Y` line.

**Written:** the Cocoa `host_window_show` (`tests/engine_library/macos/host_window_cocoa.m`, the stub of Day 383): the picture is copied into a `CGImage` (sRGB, RGBA8 with the alpha byte skipped) and set as the contents of the content view's layer, unscaled from the top-left at the window's scale, inside a `CATransaction` with actions off; the scene hosts link QuartzCore. New `host_window_can_show()` (1 on X11 and Cocoa, 0 on Win32 until Day 381) and the walk-around's check that every picture it took was shown where it is 1. Built clean; not run.

**Window tests, later on 2026-11-17 (console unlocked).** `engine_c_scene`, `engine_c_walk_around` and `engine_c_walk_around_offscreen` passed on Metal with validation; the off-screen walk-around printed `39 of 40 frames taken, 39 shown in the window`, so the Cocoa `host_window_show` ran. The review's regression check was shown to fail: with `host_window_show` made to return 0, `engine_c_walk_around_offscreen` failed with `CHECK failed: !host_window_can_show() || shown == acquired`, and passed again once restored. The full plain run with the console unlocked: `ran 3745 of 9956 tests (phases: all; flavors: plain; display: included; crash tests: included) in 597 s`, the failures none of this day's (`KNOWN-ISSUES.md`, Phase 25 closing). Not run: asan and tsan (the user's rule for the Mac), the shared tree.

## Post-implementation code review

`/code-review high` over the uncommitted diff: 8 findings, all fixed.
1. The picture was set on the layer AppKit owns, which a redraw of the layer-backed view replaces: the view's `layerContentsRedrawPolicy` is now `NSViewLayerContentsRedrawNever`.
2. The walk-around printed how many pictures were shown and checked nothing: `host_window_can_show()` and `CHECK(!host_window_can_show() || shown == acquired)`. Not shown to fail without the fix: the test cannot run while the console is locked.
3. `KNOWN-ISSUES.md` still called the Cocoa function a stub: updated.
4. The day file and `STATUS.md` were not in the diff: updated.
5. `host_window.h` said 0 meant only "cannot show pixels yet": it now lists the bad-argument and refused-image cases.
6. The colour space and the layer settings were made on every frame: once per window now (released by `host_window_close`).
7. `kCAGravityResize` stretched the picture where X11 draws it 1:1 from the top-left: `kCAGravityTopLeft` and the window's `contentsScale`.
8. `stride < width * 4u` could wrap in 32 bits: compared in 64 bits.
