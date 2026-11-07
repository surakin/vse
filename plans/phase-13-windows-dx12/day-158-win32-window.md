# Phase 13 / Day 158 — Win32 Window

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 157; `engine/platform/window.h`, `engine/platform/windows/FULLSCREEN.md`.

The `os::Window` backend for Windows (input is tomorrow). New files under `engine/platform/windows/` (the `platform` library, Phase 12).

## Tasks

- [ ] **Backend selection:** `WindowBackend::Win32` added; `ParseWindowBackend("win32")`; `PlanWindowBackends` on Windows yields Win32 for `Auto` and refuses
      Wayland/X11 with the reason; the Linux plan is unchanged (tests on both machines). The `window_backend` setting's enum names gain `win32`.
- [ ] **`Win32Window`** (`platform/windows/win32_window.h/.cpp`): a window class registered once per process (`RegisterClassExW`, `CS_OWNDC` only if the spec needs it),
      `CreateWindowExW` with the client size the caller asked for (`AdjustWindowRectExForDpi`), title in UTF-8 converted, `WndProc` dispatching to the instance
      through `GWLP_USERDATA` (set in `WM_NCCREATE`); `PumpEvents` drains with `PeekMessageW` and never blocks; `ShouldClose` on `WM_CLOSE` (the window is not
      destroyed until the owner destroys it).
- [ ] **Size and state:** `Width`/`Height` in physical pixels from `WM_SIZE`; `IsMinimized` from `SIZE_MINIMIZED`; `ResizeSerial` bumped on every size change and on
      leaving minimize; the modal resize loop (`WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`) does not freeze the app's frame loop — the spec picks the approach (a timer
      during the loop, or rendering from `WM_PAINT`), and a test checks the serial changes during a programmatic resize.
- [ ] **DPI:** per-monitor-v2 (manifest from Day 148 plus `SetThreadDpiAwarenessContext` fallback); `WM_DPICHANGED` applies the suggested rectangle; the scale
      factor is exposed only if `window.h` gains it — decide whether this phase adds it (the UI phase needs it) and record.
- [ ] **Native info:** `GetNativeInfo` returns `NativeWindowKind::Win32` with `HINSTANCE` and `HWND`; `BackendName` is `"win32"`; `PlatformWindowHost` on Windows
      (`platform/windows/window_host_windows.cpp`) mirrors the Linux one.
- [ ] **Fullscreen** per `FULLSCREEN.md`: borderless on the window's monitor, saved style and placement restored; `IsFullscreen` true once the `WM_SIZE` for the
      new rectangle arrives; `tests/foundation/window_fullscreen_test.cpp` runs on Windows.
- [ ] **Lifecycle events:** whatever `window.h` already models for suspend/resume/focus (`WM_ACTIVATEAPP`, `WM_POWERBROADCAST` `PBT_APMSUSPEND`/
      `PBT_APMRESUMEAUTOMATIC`); events the interface lacks are filed for the phase that adds them (`PLAN.md` assumption #11), not added ad hoc.
- [ ] **Window tests on Windows:** the shared window tests (`tests/foundation/window_*`) run in the `display` group, skipping through `SKIP_IF_NO_DISPLAY_SESSION()`
      on a locked session; Windows-specific cases (`win32_window_windows_test.cpp`, phase 13): DPI change by `SetWindowPos` across a simulated DPI, minimize and
      restore, close request, destroying the window with messages still queued, two windows at once.
- [ ] **A source-level entry point for windowed programs** (developer decisions, 2026-11-07: windowed programs use the Windows subsystem, and the
      main/WinMain difference is hidden in source, not chosen per target in CMake). `engine/platform/entry_point.h` declares the program's side,
      `int EngineMain(int argc, char** argv)` (UTF-8 `argv`), and a macro `ENGINE_WINDOWED_PROGRAM(EngineMain)` that a program writes once, in one
      source file, instead of `main`. It expands to the platform's real entry point: `int main(int argc, char** argv)` on Linux, and on Windows
      `wWinMain` (with `extern "C"` and the plain types the CRT expects, so the expanding file need not include `windows.h`). Both call
      `platform::detail::RunWindowedProgram(entry)` (`platform/<os>/entry_point_*.cpp`), which on Windows builds UTF-8 `argv` from the wide command
      line (`CommandLineToArgvW` and `os/windows/utf16.h`, so it does not depend on the manifest's code page), stashes it for
      `os::CommandLine::FromNative`, gives standard output and error somewhere to go (below), and returns `EngineMain`'s result.
      `config_access_check` flags `CommandLineToArgvW` and `GetCommandLineW` since Day 150, with no directory exempt: the Windows
      `entry_point_*.cpp` gets an allow-list entry with its reason (the command line is read before any `Settings` exist).
      - **The subsystem follows the entry point**, with no per-target CMake setting: `link.exe` and `lld-link` pick `/SUBSYSTEM:WINDOWS` for `wWinMain`
        and `/SUBSYSTEM:CONSOLE` for `main` when no `/SUBSYSTEM` is given. CMake passes `/subsystem:console` to every executable by default, so
        `cmake/EngineCompilerOptions.cmake` clears that once for the whole build (`CMAKE_CREATE_CONSOLE_EXE`); check what both linkers infer and
        record it. Tests and command-line tools keep a plain `main` and stay console programs (ctest captures their output, a terminal waits).
      - **Output of a GUI program:** handles it inherited (a pipe, a redirected file, ctest) are used as they are; with none, the first write tries
        `AttachConsole(ATTACH_PARENT_PROCESS)` once and opens `CONOUT$`, so a program started from a terminal still prints there (after the shell's
        prompt, since the shell does not wait for a GUI program: documented); with no parent console, writes report failure and the log file still
        has everything.
      - `game/hello_window` uses `ENGINE_WINDOWED_PROGRAM` from today; the demos on Day 161. `platform_neutral_check` (or `module_deps_check`) gains a
        rule that windowed programs under `game/` define no `main` or `WinMain` of their own.
      - Tests: a windowed probe written with the macro has subsystem 2 (`IMAGE_SUBSYSTEM_WINDOWS_GUI`) in its PE header, receives non-ASCII arguments,
        quotes and backslashes in `EngineMain`, and its output reaches a parent's pipe; a console test program still has subsystem 3. On Linux the
        macro gives an ordinary `main` (checked at the phase close).
- [ ] **Human check** (record here or file): started from Explorer, `hello_window` opens no console window; a window appears, resizes by dragging, minimizes and restores, toggles fullscreen and closes with the close button.
- [ ] **Documentation:** `window.h` comments name Win32 where they list backends; `docs/guide/platform-windowing-and-input.md` "Windows"; the docs module map
      (`docs/reference/modules.txt`) includes the Windows window header if it is public, as the Linux ones are; reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (`window.h` and the backend plan changed); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: `WndProc` reentrancy (messages sent during `CreateWindowExW` and `DestroyWindow`, before
      `GWLP_USERDATA` is set and after the object is gone), the class unregistered while a window exists, the resize-loop strategy.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a Win32 window opens, reports size, minimize, DPI and close correctly, toggles borderless fullscreen, hands its `HWND` to graphics code through
`NativeWindowInfo`, and the window tests pass on Windows.
