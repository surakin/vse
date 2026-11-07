# Maintenance: a session-state query, so window tests skip on a locked session

Not a numbered phase (no phase is renumbered). Origin: `KNOWN-ISSUES.md` listed `X11Window::Create opens a mapped window on the real X server` (phase 1, display) as failing in about ten
runs with "cause not investigated". The user established the cause: the window tests only pass while the session is unlocked and the display is active. The decision: an OS
interface that reports the session state, implemented on Linux now and specified for the other platforms in checklists, and the window tests skip with a reason when the session
cannot show windows.

## Tasks

- [x] **Interface** `engine/foundation/os/session.h`: `enum class SessionState { Active, Locked, Idle, Inactive, Unknown }`, `SessionState QuerySessionState()`, `const char* SessionStateName(SessionState)`, the pure
  `SessionStateFromFlags(known, active, locked, idle)` the backends share, and `bool SessionCanShowWindows(SessionState)` (true for `Active` and `Unknown`: an unknown state never hides a failure). `///` docs per `DOC-CONVENTIONS.md`.
- [x] **Linux backend** `engine/foundation/os/linux/session.cpp`: asks logind (`org.freedesktop.login1.Session` at `/org/freedesktop/login1/session/auto`: `Active`, `LockedHint`, `IdleHint`) through `libsystemd.so.0`
  loaded with `os::LoadLibrary` at run time, so there is no build or link dependency and a system without systemd answers `Unknown`. Added to `FOUNDATION_PLATFORM_SOURCES`.
- [x] **Checklists** `engine/foundation/os/windows/SESSION.md` and `os/macos/SESSION.md` (and the mobile and console notes in them): what each platform calls, in the style of `FULLSCREEN.md`.
- [x] **Tests:** `tests/foundation/session_state_test.cpp` (the pure mapping, the names, `SessionCanShowWindows`, a live query returning one of the five values without crashing); `tests/support/display_session.h`
  with `SKIP_IF_NO_DISPLAY_SESSION()`; the X11, Wayland, window-backend and fullscreen tests call it first. Test registered in `tests/phases.txt` (phase 1, the platform layer).
- [x] **Window tests outside foundation:** the `tests/rhi` presentation tests tagged `[x11]`/`[wayland]` call the macro. The demos' window ctests (`*_wayland`, `*_x11`) were not changed; filed in `KNOWN-ISSUES.md`.
- [x] **Docs:** `os/session.h` in `docs/reference/modules.txt`, `cmake --build build --target docs`, a paragraph in `docs/guide/building-and-testing.md` ("Window tests and the session"), `ctest -L docs`, `docgen lint` at zero.
- [x] **Known issues:** replace the repeated X11 entries by one fact (window tests need an unlocked, active session and skip otherwise); record that the skip was not seen with a real locked session by a human.
- [x] `STATUS.md`; scoped tests (`--affected`, plain/asan/tsan); `/code-review high` on the diff, recorded below.

**Done:** `QuerySessionState` exists with a Linux implementation and checklists for the other platforms; the window tests skip with the reason on a locked or inactive session and run otherwise; docs, lint and
scoped tests pass; the review is recorded; the X11 entries in `KNOWN-ISSUES.md` are replaced by the single explanation.

## Result

`os::QuerySessionState` (Linux: logind through `libsystemd.so.0`, loaded at run time, 1 s timeout per call), the Windows and macOS checklists, `SKIP_IF_NO_DISPLAY_SESSION()` in the window tests of foundation and the RHI (cached for two seconds). Live query here: `active`.
Scoped runs: plain `ran 1332 of 3444 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 961 of 3444`, tsan `ran 678 of 3444`; `ctest -L docs` passes. Also fixed: the demo CLI checks shared one work directory per program and raced under `ctest -j` (one `renderer_demo_settings_bad_value` failure seen); each now uses a directory named by a hash of what it runs.

## Post-implementation code review

`/code-review high`: 10 findings.
- Fixed: window-opening tests that lacked the macro (Wayland window and connection, X11 focus-loss and layout, window-backend host tests); the macro removed from tests that open no window (zero-size and default-constructed windows, invalid connection); no timeout on bus calls (now 1 s, header text corrected); a query per test (cached two seconds in the test helper, library kept loaded); the work-directory hash lacked `EXE`, `GOLDEN`, `EXPECT` (added, comment corrected); the `//!` blurb merged into the os module prose (now a plain comment); the missing review section and `STATUS.md` line (this section).
- Declined: `Idle` still skips (the user's condition is "unlocked and display active", and a blanked display gives no frames); stated in the enum documentation. The `phases.txt` mapping finding does not apply: `engine/foundation` already maps to phase 1 in `tests/module_map.txt`.
- Recorded, not fixed: the query describes the caller's own logind session, so a run over ssh against the physical desktop's display reports the ssh session (documented in `session.h`, `session.cpp`; added to `KNOWN-ISSUES.md`).
