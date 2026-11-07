# Session state on Windows (checklist for Phase 13)

`os::QuerySessionState` (`engine/foundation/os/session.h`) answers whether windows can appear. Implement it in `os/windows/session.cpp` and add the file to `FOUNDATION_PLATFORM_SOURCES`.

- **Locked:** the input desktop is not the default desktop. `OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP)` fails or `GetUserObjectInformation(UOI_NAME)` names `Winlogon` while the workstation is locked; a screen saver
  shows as `Screen-saver`. For events instead of polling, `WTSRegisterSessionNotification` delivers `WTS_SESSION_LOCK` and `WTS_SESSION_UNLOCK`.
- **Inactive:** `WTSQuerySessionInformation(WTSConnectState)` is not `WTSActive` for the current session (fast user switching, a disconnected remote session).
- **Idle:** `GetLastInputInfo` against a limit if the product wants one; the platform has no idle hint of its own, so reporting `Active` is acceptable.
- **Unknown:** any call fails (a service session, a container without a desktop).
- Map the flags with `SessionStateFromFlags`; do not re-implement the priority (locked, then idle, then inactive).
- Test: `tests/foundation/session_state_test.cpp` runs unchanged; the window tests already skip through `SKIP_IF_NO_DISPLAY_SESSION()`.
