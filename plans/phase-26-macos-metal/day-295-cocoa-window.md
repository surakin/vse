# Phase 26 / Day 295 — Cocoa I: Application, Entry Point, Window, Session, Fullscreen

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 294.

The `engine/platform` library on macOS: `NSApplication` set up by the entry point, the window with a layer-backed view ready for `CAMetalLayer`,
resize and Retina scale, close, fullscreen, and the `macos` window-host backend. Input is Day 296.

## Tasks

- [ ] **`ENGINE_WINDOWED_PROGRAM` on macOS** (`platform/macos/entry_point_macos.mm`, `entry_point.h`): a `main` that creates `NSApplication`,
      sets the activation policy `Regular`, installs a minimal menu (Quit with Cmd-Q, so the system's quit works), finishes launching, activates, and calls
      the program's entry on the main thread; tests and tools keep plain `main`. Document that windows may be created only on the main thread (AppKit);
      a test checks a window created from another thread fails cleanly with a log, not a crash.
- [ ] **`Info.plist` for windowed programs:** a CMake helper embeds one (`-sectcreate __TEXT __info_plist`: `CFBundleIdentifier`, `CFBundleName`,
      `NSHighResolutionCapable`) into `ENGINE_WINDOWED_PROGRAM` targets, so the settings suite name and Retina work without an app bundle.
- [ ] **`CocoaWindow`** (`platform/macos/cocoa_window.mm/.h`): `NSWindow` with title, size in pixels (points times `backingScaleFactor`), a content
      `NSView` that is layer-backed and hands the RHI an opaque `CAMetalLayer*` through the window's native-handle accessor; `PumpEvents` drains
      `nextEventMatchingMask:untilDate:[NSDate distantPast]` and dispatches; `windowShouldClose:` sets the close flag (never closes behind the program's
      back); `windowDidResize:`/`windowDidChangeBackingProperties:` bump `ResizeSerial` and update the scale.
- [ ] **Window host:** `WindowBackend::Cocoa` and `auto` on macOS (`platform/macos/window_host_macos.mm`), the backend setting's documentation
      updated; without a window server (SSH) creating a window fails with a reason, and the `display` test group skips.
- [ ] **Session state** in the platform tests (`QuerySessionState` from Day 285 used by the `display` group to skip while locked, as on Linux).
- [ ] **Fullscreen** per `engine/platform/macos/FULLSCREEN.md`: `NSWindowCollectionBehaviorFullScreenPrimary`, `toggleFullScreen:`, `IsFullscreen`
      from `windowDidEnterFullScreen`/`windowDidExitFullScreen`, the new size through resize; `SupportsFullscreen` false for a borderless window;
      `tests/foundation/window_fullscreen_test.cpp` passes (the animation means the test waits on the serial, with a timeout).
- [ ] **Tests:** the window tests of Phase 1 and Phase 8 that need no input (`tests/foundation/window_*`) run on macOS in the `display` group;
      `hello_window` runs and closes from Cmd-Q and the close button (checked by hand; recorded).
- [ ] **Gates:** `window_backend_lists` (the `macos` section of `engine/platform/BACKEND.md` now says what is built), `platform_neutral_check` (AppKit only
      under `platform/macos/`), `module_deps_check` (the platform library links `-framework AppKit -framework QuartzCore` only).
- [ ] **Documentation:** `docs/guide/platform-windowing-and-input.md` macOS section (main thread, Info.plist, scale); `entry_point.h` documents the macOS
      expansion; `docgen lint` zero; reference regenerated.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote; the Mac unlocked).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: ARC ownership between the window, its delegate and the view (no cycle, no use after the
      window closes), events delivered during a live resize (AppKit runs its own loop then).
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a windowed program on macOS opens a Retina-aware window from `ENGINE_WINDOWED_PROGRAM`, resizes, goes fullscreen and closes, the window and
fullscreen tests pass in the display group in three flavors, and the window layer's macOS section and guide describe what is built.
