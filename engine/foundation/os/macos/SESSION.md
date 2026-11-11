# Session state on macOS (Phase 26; implemented on Day 285)

`os::QuerySessionState` (`engine/foundation/os/session.h`) answers whether windows can appear. It is implemented in `os/macos/session.mm` (in `FOUNDATION_PLATFORM_SOURCES`, linking CoreGraphics and Foundation).

- **Locked:** `CGSessionCopyCurrentDictionary()` has `CGSSessionScreenIsLocked` true (the key is absent when unlocked). `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked` are the distributed notifications if events are wanted.
- **Inactive:** the same dictionary has `kCGSessionOnConsoleKey` false when another user's session holds the console (fast user switching).
- **Idle:** a sleeping main display (`CGDisplayIsAsleep(CGMainDisplayID())`). The input-time route (`CGEventSourceSecondsSinceLastEventType`) needs a limit the engine does not have, so it is not used (Day 285; Windows decided the same).
- **Unknown:** `CGSessionCopyCurrentDictionary` returns null (an SSH login without a window server).
- iOS and Android (Phases 27): iOS uses `UIApplication.isProtectedDataAvailable` (device locked) and the scene's activation state; Android uses `KeyguardManager.isKeyguardLocked` and `PowerManager.isInteractive`. Consoles (Phase 28): the suspend or constrained state the platform reports, else `Unknown`.
- Map the flags with `SessionStateFromFlags`.
- Test: `tests/foundation/session_state_test.cpp` runs unchanged.
