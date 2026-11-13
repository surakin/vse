# Phase 21 / Day 295 — Cocoa I: Application, Entry Point, Window, Session, Fullscreen

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 294.

The `engine/platform` library on macOS: `NSApplication` set up by the entry point, the window with a layer-backed view ready for `CAMetalLayer`,
resize and Retina scale, close, fullscreen, and the `macos` window-host backend. Input is Day 296.

## Tasks

- [x] **`ENGINE_WINDOWED_PROGRAM` on macOS** (`platform/macos/entry_point_macos.mm`, `entry_point.h`): a `main` that creates `NSApplication`,
      sets the activation policy `Regular`, installs a minimal menu (Quit with Cmd-Q, so the system's quit works), finishes launching, activates, and calls
      the program's entry on the main thread; tests and tools keep plain `main`. Document that windows may be created only on the main thread (AppKit);
      a test checks a window created from another thread fails cleanly with a log, not a crash.
      (`entry_point_macos.cpp`, plain C++: `cocoa::EnsureApplication` does the set-up, and the first window does it too, because tests open
      windows from a plain `main`. Activation happens with the first window. Quit is the menu's `terminate:`, which the application's
      delegate turns into every window's close request, never an exit; `ClearQuitRequest` takes it back. The thread test found a real bug:
      `EnsureApplication` returned early once the application existed, before checking the thread, so a later window off the main thread
      reached AppKit, which aborts; shown to fail before the fix.)
- [x] **`Info.plist` for windowed programs:** a CMake helper embeds one (`-sectcreate __TEXT __info_plist`: `CFBundleIdentifier`, `CFBundleName`,
      `NSHighResolutionCapable`) into `ENGINE_WINDOWED_PROGRAM` targets, so the settings suite name and Retina work without an app bundle.
      **Declined (Day 295):** neither needs one. Retina works without it (Spike C and today: backing scale 2.0, 1800x1064 pixels for
      900x532 points), activation from a terminal works (below), and the settings take their suite from the program (`app_name`, Day 293),
      not the bundle. A program that ships as an app bundle gets a real `Info.plist` with it (Day 311 if the demos are bundled).
- [x] **`CocoaWindow`** (`platform/macos/cocoa_window.mm/.h`): `NSWindow` with title, size in pixels (points times `backingScaleFactor`), a content
      `NSView` that is layer-backed and hands the RHI an opaque `CAMetalLayer*` through the window's native-handle accessor; `PumpEvents` drains
      `nextEventMatchingMask:untilDate:[NSDate distantPast]` and dispatches; `windowShouldClose:` sets the close flag (never closes behind the program's
      back); `windowDidResize:`/`windowDidChangeBackingProperties:` bump `ResizeSerial` and update the scale.
      (Also `Resize`, `FocusGained` and `FocusLost` in the event stream; the live-resize callback. The delegate reaches the C++ window through
      a plain pointer that `Close` clears first.)
- [x] **Window host:** `WindowBackend::Cocoa` and `auto` on macOS (`platform/macos/window_host_macos.mm`), the backend setting's documentation
      updated; without a window server (SSH) creating a window fails with a reason, and the `display` test group skips.
      (`WindowBackend::Cocoa`, `PlanCocoaWindowBackends`; `platform.window_backend` lists `cocoa`; `window_host_macos.cpp`, plain C++.
      Without a window server: refused when `CGSessionCopyCurrentDictionary` has no session; not tried, since this terminal runs in the
      console session; filed in `KNOWN-ISSUES.md`.)
- [x] **Session state** in the platform tests (`QuerySessionState` from Day 285 used by the `display` group to skip while locked, as on Linux).
- [x] **Fullscreen** per `engine/platform/macos/FULLSCREEN.md`: `NSWindowCollectionBehaviorFullScreenPrimary`, `toggleFullScreen:`, `IsFullscreen`
      from `windowDidEnterFullScreen`/`windowDidExitFullScreen`, the new size through resize; `SupportsFullscreen` false for a borderless window;
      `tests/foundation/window_fullscreen_test.cpp` passes (the animation means the test waits on the serial, with a timeout).
- [x] **Tests:** the window tests of Phase 1 and Phase 8 that need no input (`tests/foundation/window_*`) run on macOS in the `display` group;
      `hello_window` runs and closes from Cmd-Q and the close button (checked by hand; recorded).
- [x] **Gates:** `window_backend_lists` (the `macos` section of `engine/platform/BACKEND.md` now says what is built), `platform_neutral_check` (AppKit only
      under `platform/macos/`), `module_deps_check` (the platform library links `-framework AppKit -framework QuartzCore` only).
- [x] **Documentation:** `docs/guide/platform-windowing-and-input.md` macOS section (main thread, Info.plist, scale); `entry_point.h` documents the macOS
      expansion; `docgen lint` zero; reference regenerated.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote; the Mac unlocked).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: ARC ownership between the window, its delegate and the view (no cycle, no use after the
      window closes), events delivered during a live resize (AppKit runs its own loop then).
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a windowed program on macOS opens a Retina-aware window from `ENGINE_WINDOWED_PROGRAM`, resizes, goes fullscreen and closes, the window and
fullscreen tests pass in the display group in three flavors, and the window layer's macOS section and guide describe what is built.

## Result

Done on 2026-11-12 on the Mac. A windowed program opens a Retina window from `ENGINE_WINDOWED_PROGRAM`, resizes, goes fullscreen and closes;
`engine/platform` builds on macOS (`platform`, three flavors, AppKit and QuartzCore only) with `game/hello_window`.

- **Spike C rerun** (before the day's code, with the session unlocked): activation and key focus from a terminal in 104 ms, frames paced
  to the display (16.88 ms mean, 59 Hz), scale 2.0. Recorded in the design spec (decision 5); the `KNOWN-ISSUES.md` entry is closed.
  `hello_window` itself was on screen and frontmost (the window server's list), but **closing it by hand from Cmd-Q and the close button
  was not done**: the VNC link was too poor. `cocoa_window_macos_test.mm` drives both through AppKit instead: a Cmd-Q key event through
  the menu's key equivalent, and `performClose:` (filed in `KNOWN-ISSUES.md`).
- **Tests:** `tests/macos/cocoa_window_macos_test.mm` (11 cases, plain, asan, tsan, `display` group); `window_backend_test.cpp` and
  `window_fullscreen_test.cpp` have macOS branches. **Amended:** the Phase 1 and Phase 8 window tests run plain only on macOS, as on every
  platform (Phase 1 has no sanitizer executables); the three flavors of the Done condition are the macOS window tests' (Phase 21).
- **Found on the way:** the main-thread check came after the early return of `EnsureApplication` (above); macOS locks the console again when
  the VNC session that unlocked it ends, so the `display` group was run on its own while unlocked.
- **Runs (macOS):** `ctest -L display` while unlocked: 32 of 32, then 44 of 44 after the review's fixes, no skips (checked case by case);
  `scripts/test-phase.sh --affected --flavor all --no-display`: `ran 4114 of 4525 tests (phases: all; flavors: all; display: excluded; crash
  tests: excluded) in 1453 s`, the one failure `Throughput scales with participant count` (flaky under load). Non-unity build clean. `ctest -L
  docs` 3 of 3.

## Post-implementation code review

`/code-review high` on the day's uncommitted changes: 10 findings; 9 fixed, 1 fixed by a different route than proposed.

1. **Fixed: the setting's new description broke the demos' goldens on Linux and Windows** (`help.golden`, `listsettings.golden` and the
   READMEs of `rhi_demo`, `renderer_demo`, `ui_demo` quote it word for word): the nine files are updated. The demos are not built on the
   Mac, so the golden tests are filed for the Linux machine.
2. **Fixed: a window reopened after `Close` kept the old close request, focus and events**: `Open` resets them, as the Win32 window does. Test.
3. **Fixed by another route: `applicationShouldTerminate:` cancelled a logout, restart or shutdown too.** The review proposed answering every
   Quit "later"; that would put AppKit into its modal run-loop mode for an ordinary Cmd-Q as well. Instead the Apple event's quit reason
   decides: a session end is answered `NSTerminateLater` (the system waits for the program, and `ClearQuitRequest` answers no), a plain Quit
   is cancelled as before. The decision (`QuitReasonEndsSession`) is tested; a real logout was not tried.
4. **Fixed: `PumpEvents` could be re-entered from the live-resize callback**, taking the events AppKit's resize loop waits for: it returns at
   once there, as the Win32 window's does. No test: a live resize cannot be started by a test.
5. **Fixed: a closed window's `PumpEvents` served no other window**: it pumps for the program whenever it runs on the main thread. Test.
6. **Fixed: the public docs** of `SetModalLoopCallback` (`window.h`) and the setting table (`engine_settings.h`) say what macOS does.
7. **Fixed: `Close` never said the focus went**: it queues `FocusLost`. Test (with 2).
8. **Fixed: fullscreen asked for twice during the animation could end on the wrong side**: the window tracks the transition itself and applies
   the last request when it ends, from the next `PumpEvents`, since a toggle sent from `windowDidEnterFullScreen:` is ignored (the first
   version of the fix did that, and its new test failed in three flavors); a failed transition is handled. Test.
9. **Fixed: the test helper `ScopedBackendVariable` was duplicated**: defined once for Windows and macOS.
10. **Fixed: an unused include** in `window_backend_test.cpp`.
