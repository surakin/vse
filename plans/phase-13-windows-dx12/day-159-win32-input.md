# Phase 13 / Day 159 — Win32 Input

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 158; `engine/platform/input.h`, `engine/platform/key_translate.cpp`.

Fill the bound `InputState` from Win32 messages with the same meaning the Wayland and X11 backends give it.

## Tasks

- [ ] **Keys:** `WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSKEYDOWN`/`WM_SYSKEYUP` mapped through the scan code (bits 16-23 plus the extended bit; `MapVirtualKeyW` only for what
      the scan code cannot tell), so the `Key` enum is layout-independent the way Linux's evdev codes are; left/right modifiers told apart (`VK_LSHIFT` etc. via
      the scan code), `Pause`, `PrintScreen`, numpad with and without NumLock; auto-repeat follows what the Linux backends report (the `KeyboardState` contract).
      A table test of the translation covers every `Key`.
- [ ] **Modifiers and locks:** `ModifierBit`s from `GetKeyState` at each key event; Caps/Num lock state; `Alt` alone does not open the system menu (handle
      `WM_SYSKEYDOWN`/`WM_SYSCHAR`/`SC_KEYMENU` as the spec says), `Alt+F4` still closes.
- [ ] **Text input:** `WM_CHAR` with UTF-16 surrogate pairs joined into one code point, delivered as UTF-8 the way the Linux backends deliver text; `WM_UNICHAR`
      accepted; dead keys and IME composition produce the composed text (an IME's composition window is not supported this phase — filed for the UI phase).
- [ ] **Mouse:** position from `WM_MOUSEMOVE` (client coordinates, physical pixels), buttons including X1/X2, wheel (`WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL`, fractional
      `WHEEL_DELTA` accumulated), `TrackMouseEvent` for leave; relative motion from raw input (`RegisterRawInputDevices` for the mouse, `WM_INPUT`) when the
      interface asks for relative mode; capture (`SetCapture` while a button is held) and cursor clip/hide if the interface has them.
- [ ] **Focus loss:** `WM_KILLFOCUS`/`WM_ACTIVATEAPP` releases held keys and buttons, matching the Linux rule (the X11 review found a bug here: keys released on a
      real focus loss but not during grabs; the Windows rule is written down and tested).
- [ ] **Tests:** the shared input tests run on Windows; `win32_input_windows_test.cpp` (phase 13) drives the window with `SendInput` (key presses, text with a
      surrogate pair, mouse buttons, wheel) and checks the `InputState`; a translation table test without a window.
- [ ] **Human check** (record or file): typing, held keys, mouse buttons, wheel and relative motion in `rhi_demo` (or a small input viewer if the demo cannot run
      until Day 161).
- [ ] **Documentation:** `input.h` notes the Windows differences; `platform-windowing-and-input.md` "Windows input"; reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` if `key_translate.cpp` or `input.h` changed (record); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: extended-key and Pause scan codes, a surrogate pair split across two `PeekMessage` drains,
      stuck keys after focus loss, raw-input buffer sizes.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** keyboard, text, mouse and relative motion fill `InputState` on Windows with the same meaning as on Linux, focus loss releases held input, and the
input tests pass on Windows.
