# Session state on macOS (checklist for Phase 14)

`os::QuerySessionState` (`engine/foundation/os/session.h`) answers whether windows can appear. Implement it in `os/macos/session.mm` and add the file to `FOUNDATION_PLATFORM_SOURCES`.

- **Locked:** `CGSessionCopyCurrentDictionary()` has `CGSSessionScreenIsLocked` true (the key is absent when unlocked). `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked` are the distributed notifications if events are wanted.
- **Inactive:** the same dictionary has `kCGSessionOnConsoleKey` false when another user's session holds the console (fast user switching).
- **Idle:** `CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType)` against a limit, or report `Active`; a sleeping display is `Idle` (`CGDisplayIsAsleep`).
- **Unknown:** `CGSessionCopyCurrentDictionary` returns null (an SSH login without a window server).
- iOS and Android (Phases 22): iOS uses `UIApplication.isProtectedDataAvailable` (device locked) and the scene's activation state; Android uses `KeyguardManager.isKeyguardLocked` and `PowerManager.isInteractive`. Consoles (Phase 23): the suspend or constrained state the platform reports, else `Unknown`.
- Map the flags with `SessionStateFromFlags`.
- Test: `tests/foundation/session_state_test.cpp` runs unchanged.
