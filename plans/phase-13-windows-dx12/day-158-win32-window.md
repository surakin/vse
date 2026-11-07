# Phase 13 / Day 158 — Win32 Window

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 157; `engine/platform/window.h`, `engine/platform/windows/FULLSCREEN.md`.

The `os::Window` backend for Windows (input is tomorrow). New files under `engine/platform/windows/` (the `platform` library, Phase 12).

## Tasks

- [x] **Backend selection:** `WindowBackend::Win32` added; `ParseWindowBackend("win32")`; `PlanWindowBackends` on Windows yields Win32 for `Auto` and refuses
      Wayland/X11 with the reason; the Linux plan is unchanged (tests on both machines). The `window_backend` setting's enum names gain `win32`.
- [x] **`Win32Window`** (`platform/windows/win32_window.h/.cpp`): a window class registered once per process (`RegisterClassExW`, `CS_OWNDC` only if the spec needs it),
      `CreateWindowExW` with the client size the caller asked for (`AdjustWindowRectExForDpi`), title in UTF-8 converted, `WndProc` dispatching to the instance
      through `GWLP_USERDATA` (set in `WM_NCCREATE`); `PumpEvents` drains with `PeekMessageW` and never blocks; `ShouldClose` on `WM_CLOSE` (the window is not
      destroyed until the owner destroys it).
      *(No `CS_OWNDC`: the swapchain draws, not GDI.)*
- [x] **Size and state:** `Width`/`Height` in physical pixels from `WM_SIZE`; `IsMinimized` from `SIZE_MINIMIZED`; `ResizeSerial` bumped on every size change and on
      leaving minimize; the modal resize loop (`WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE`) does not freeze the app's frame loop — the spec picks the approach (a timer
      during the loop, or rendering from `WM_PAINT`), and a test checks the serial changes during a programmatic resize.
      *(Decided: a timer during the loop that calls a callback, `os::Window::SetModalLoopCallback`, neutral so the demos reach it through the
      host; Linux never calls it. The test drives the loop's messages itself; a real drag is the human check.)*
- [x] **DPI:** per-monitor-v2 (manifest from Day 148 plus `SetThreadDpiAwarenessContext` fallback); `WM_DPICHANGED` applies the suggested rectangle; the scale
      factor is exposed only if `window.h` gains it — decide whether this phase adds it (the UI phase needs it) and record.
      *(Decided: `window.h` gains no scale factor in this phase; `Win32Window::Dpi` is the backend's own. The UI phase (16) adds one for every
      backend: `KNOWN-ISSUES.md`.)*
- [x] **Native info:** `GetNativeInfo` returns `NativeWindowKind::Win32` with `HINSTANCE` and `HWND`; `BackendName` is `"win32"`; `PlatformWindowHost` on Windows
      (`platform/windows/window_host_windows.cpp`) mirrors the Linux one.
- [x] **Fullscreen** per `FULLSCREEN.md`: borderless on the window's monitor, saved style and placement restored; `IsFullscreen` true once the `WM_SIZE` for the
      new rectangle arrives; `tests/foundation/window_fullscreen_test.cpp` runs on Windows.
- [x] **Lifecycle events:** whatever `window.h` already models for suspend/resume/focus (`WM_ACTIVATEAPP`, `WM_POWERBROADCAST` `PBT_APMSUSPEND`/
      `PBT_APMRESUMEAUTOMATIC`); events the interface lacks are filed for the phase that adds them (`PLAN.md` assumption #11), not added ad hoc.
      *(`window.h` models none of them, so nothing is added; filed in `KNOWN-ISSUES.md`.)*
- [x] **Window tests on Windows:** the shared window tests (`tests/foundation/window_*`) run in the `display` group, skipping through `SKIP_IF_NO_DISPLAY_SESSION()`
      on a locked session; Windows-specific cases (`win32_window_windows_test.cpp`, phase 13): DPI change by `SetWindowPos` across a simulated DPI, minimize and
      restore, close request, destroying the window with messages still queued, two windows at once.
      *(The Windows cases are in `tests/windows/win32_window_windows_test.cpp`: a test that includes `os/windows/win32.h` must live under a
      Windows directory, `platform_neutral_check` rule 4.)*
- [x] **A source-level entry point for windowed programs** (developer decisions, 2026-11-07: windowed programs use the Windows subsystem, and the
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
      *(Corrections: the subsystem comes from a `/SUBSYSTEM:WINDOWS` linker directive in the macro's expansion, which wins over CMake's
      `/subsystem:console` in both linkers; clearing CMake's flag fails for every test (LNK1561: `main` is in Catch2's library). The helper is
      `os::detail::RunWindowedProgram`, in the platform layer's own namespace. The console is attached at start, not at the first write.)*
- [ ] **Human check** (record here or file): started from Explorer, `hello_window` opens no console window; a window appears, resizes by dragging, minimizes and restores, toggles fullscreen and closes with the close button.
      *(Not done: no person has started it from Explorer yet, and fullscreen has no key until input arrives (Day 159); filed in
      `KNOWN-ISSUES.md`. Checked by machine: the subsystem is 2, the window opens, and `hello_window --run-seconds=1` prints its UTF-8
      arguments through a pipe and exits 0.)*
- [x] **Documentation:** `window.h` comments name Win32 where they list backends; `docs/guide/platform-windowing-and-input.md` "Windows"; the docs module map
      (`docs/reference/modules.txt`) includes the Windows window header if it is public, as the Linux ones are; reference regenerated; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (`window.h` and the backend plan changed); quote the lines.
      *(Windows done; the Linux run waits for the phase close, Day 174.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: `WndProc` reentrancy (messages sent during `CreateWindowExW` and `DestroyWindow`, before
      `GWLP_USERDATA` is set and after the object is gone), the class unregistered while a window exists, the resize-loop strategy.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** a Win32 window opens, reports size, minimize, DPI and close correctly, toggles borderless fullscreen, hands its `HWND` to graphics code through
`NativeWindowInfo`, and the window tests pass on Windows.

## Notes (2026-11-07, Windows PC)

- **Backend choice:** `WindowBackend::Win32` and `ParseWindowBackend("win32")` on every platform; `BackendPlan` gained `refusal`, the reason a
  backend asked for by name is not on the platform. `PlanWindowBackends` stays Linux's plan (it refuses `win32`); `PlanWin32WindowBackends` is
  Windows' (Win32 for `auto` and `win32`, refusals for `wayland` and `x11`). Both are pure and tested on both machines; each host logs the refusal.
  `platform.window_backend` gained `win32` (and a description naming both platforms; the demos' goldens and READMEs were edited by hand, since the
  demos do not build on Windows before Day 161).
- **`os::win32::Win32Window`** (`platform/windows/win32_window.h/.cpp`): the class `EngineWindow` is registered once per process
  (`std::call_once`) and never unregistered, so no window outlives it. `WM_NCCREATE` stores the object in `GWLP_USERDATA`; messages before it
  (`WM_GETMINMAXINFO`) and windows of the class made without `Open` get the default procedure; `Close` clears the pointer before `DestroyWindow`,
  so nothing `DestroyWindow` sends and nothing still queued reaches the object. The object cannot move (neither copyable nor movable), so the
  host holds it by value. The client size is exact at the window's DPI (`AdjustWindowRectExForDpi` with the system DPI, then corrected with
  `GetDpiForWindow`); the thread's DPI awareness is set to per-monitor-v2 for the creation if it is not already (the manifest makes it so).
- **Size and state:** `Width`/`Height` from `WM_SIZE`, kept while minimized; `IsMinimized` from `SIZE_MINIMIZED`; `ResizeSerial` changes on a
  size change and on leaving minimize. **The modal loops** (decided today): `os::Window::SetModalLoopCallback` (neutral, default ignored; the
  demos reach it through the host), called by a 10 ms timer the window starts on `WM_ENTERSIZEMOVE` and `WM_ENTERMENULOOP`. A pump of any
  window from inside the callback returns at once (a thread-wide flag; found in review: a per-window flag let another window's pump take the drag
  loop's messages).
- **DPI:** `WM_DPICHANGED` applies the suggested rectangle (or refills the monitor when fullscreen); `Win32Window::Dpi`. No scale factor in
  `window.h` this phase: the UI phase adds one for every backend (filed).
- **Fullscreen** per `FULLSCREEN.md`; synchronous on Windows (`IsFullscreen` is true when `SetFullscreen` returns); a minimized window is
  restored first (found in review). `window_fullscreen_test.cpp` runs a Win32 case.
- **Lifecycle:** `window.h` has no focus, suspend or resume events, so none were added (filed).
- **Entry point:** `platform/entry_point.h`, `ENGINE_WINDOWED_PROGRAM(EngineMain)`: `main` on Linux, `wWinMain` on Windows calling
  `os::detail::RunWindowedProgram`, which connects standard output and error (inherited handles as they are; otherwise `AttachConsole` to the
  parent's console and `CONOUT$` for the Win32 handles and the C runtime's streams), builds UTF-8 `argv` from `CommandLineToArgvW` (one block, kept
  for the process: the stash points into it) and stashes it. **The subsystem:** the plan's way (clear CMake's `/subsystem:console` and let the
  linkers infer it from the entry point) fails for every test program: with no `/SUBSYSTEM`, link.exe stops with LNK1561 before it finds `main`
  in Catch2's library. Instead the macro's expansion carries `#pragma comment(linker, "/SUBSYSTEM:WINDOWS")`, which wins over the command line's
  `/subsystem:console` in link.exe and in lld-link (measured: `hello_window` and the probe have subsystem 2 in both builds, test programs 3).
  Still nothing chosen per target in CMake. `hello_window` uses the macro; the demos on Day 161. Allow-list entries: `config_access_check` (the
  host's `ENGINE_WINDOW_BACKEND`, the entry point's command line), `file_access_check` (E3, the console device).
- **`platform_neutral_check` rule 5:** a file under `game/` that includes `platform/window_host.h` or `platform/window.h` defines no `main`,
  `wmain`, `WinMain` or `wWinMain`; the two demos are listed until Day 161 (an entry whose file has no entry point of its own fails). Planted
  fixture: `fixtures/bad/game/windowed/main.cpp` (two reports); clean fixtures: a windowed program with the macro and a console tool.
- **Tests:** `tests/windows/win32_window_windows_test.cpp` (phase 13; `windows_platform_tests`, plain and ASan; the `[window]` cases in the
  `display` group): open and native info, the title in UTF-16, reopen after close, zero size and a second open, a programmatic resize, minimize and
  restore, the close request, `WM_QUIT`, two windows, destruction with messages queued, a DPI change, the modal-loop callback, a callback pumping
  another window, a foreign window of the class, fullscreen from minimized, the subsystem of a windowed and a console program, and a windowed
  program's UTF-8 arguments, stash and output through pipes (`windows_windowed_probe`). `tests/foundation`'s `window_backend_test.cpp` (the plans on
  both platforms, the Windows host) and `window_fullscreen_test.cpp` build on Windows; the Wayland, X11 and keyboard tests stay Linux's.
  `file_io_test.cpp`'s first case reads the running program through `io::ExecutablePath` (ELF or `MZ`) instead of `/proc/self/exe`.
- **Also:** `key_translate.cpp` (xkbcommon's keysym header) builds on Linux only; `platform_asan`/`_tsan` and `hello_window_asan` exist only where
  the flavor does; the Windows status message in the root `CMakeLists.txt` was out of date and is rewritten.
- **For the Linux run at the phase close:** `engine/platform/window.h`, `window.cpp`, `entry_point.h`, `linux/entry_point_linux.cpp` (new),
  `linux/window_host_linux.cpp`, `engine/platform/CMakeLists.txt`, `game/hello_window` (both files), `game/common/settings_main.cpp`,
  `engine/foundation/settings/engine_settings.{h,cpp}` with the demos' goldens and READMEs, `tests/foundation` (`CMakeLists.txt`,
  `window_backend_test.cpp`, `window_fullscreen_test.cpp`, `file_io_test.cpp`), `tools/platform_neutral_check` (rule 5 and fixtures),
  `tests/phases.txt`. On Linux: `scripts/test-phase.sh --affected` with asan and tsan, the demos' settings tests, `ctest -L docs`, both unity settings.

## Runs

Windows PC, after the review's fixes:

- `scripts/test-phase.sh --affected`: `ran 573 of 1168 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 59 s; the last
  full run took 13 s` (everything: the day changed `CMakeLists.txt` files and the phase map).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 333 of 1168 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 26 s;
  the last full run took 13 s`.
- Full `ctest -j8`: MSVC 1167 of 1168, `windows-clang-cl` 735 of 736, non-unity MSVC 1167 of 1168; the one failure in each was `docs_check` (the
  reference was regenerated before the review's comment changes). Regenerated: `ctest -L docs` `100% tests passed out of 3` in all three builds;
  `docgen lint` 4099/4099.
- `hello_window --run-seconds=1 --log-level=info` with a non-ASCII argument, through a pipe: prints its program path, the argument and "using
  win32 backend", exits 0.
- An earlier full run was not usable: a first copy of the run, which I believed had not started, ran at the same time in the same build trees
  (`platform_neutral_check_planted_count` failed in it and passed alone right after).
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 7 findings, 3 fixed, 4 declined.

- **Fixed:** the modal-loop callback's re-entry guard was per window, so a callback that pumped another window drained the queue inside
  Windows' own drag loop. Now one flag per thread. Test: `Windows: a modal-loop callback that pumps another window takes no message from the
  platform's loop` (fails with the guard removed).
- **Fixed:** `WM_NCCREATE` used `lpCreateParams` without a check, so a window of the class made without `Open` crashed. Test: `Windows: a window
  of the engine's class made without Open gets the default procedure` (an access violation without the fix).
- **Fixed:** fullscreen asked for while minimized never took effect. The window is restored first. Test: `Windows: fullscreen asked for while
  minimized restores the window and covers the monitor` (fails without the fix).
- **Declined:** `os::KeyFromKeysym` is declared in the neutral `input.h` and defined only on Linux. No Windows code calls it; Day 159 (input) decides
  where it lives (filed).
- **Declined:** `PumpEvents` takes `WM_QUIT` as a close request, so another component's `WM_QUIT` is consumed. The engine posts none; documented on
  `PumpEvents`.
- **Declined:** the default `Window::SetModalLoopCallback` discards its parameters with `(void)`, as `SetFullscreen`'s default does; it hides no
  defect (the default does nothing by design).
- **Declined:** `SW_SHOWDEFAULT` follows the launcher's show state, so a launcher asking for a hidden window gets one. That is what the flag is
  for (a shortcut set to "minimized"); ctest's launches show the window (the minimize tests pass under ctest).
