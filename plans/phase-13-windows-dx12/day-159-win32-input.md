# Phase 13 / Day 159 — Win32 Input

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 158; `engine/platform/input.h`, `engine/platform/key_translate.cpp`.

Fill the bound `InputState` from Win32 messages with the same meaning the Wayland and X11 backends give it.

## Tasks

- [x] **`platform/key_translate.cpp` loses its XKB part** (found on Day 147, spec section 3 item 3): it is neutral but includes `xkbcommon-keysyms.h`; the keysym
      translation moves to `platform/linux/` (used by the Wayland and X11 backends), the neutral file keeps what every backend shares, and the Day 150
      allow-list entry is deleted.
      *(Nothing was left that every backend shares: the file moved whole to `platform/linux/key_translate.cpp` with its own header, and
      `input.h` no longer declares `KeyFromKeysym`.)*
- [x] **Keys:** `WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSKEYDOWN`/`WM_SYSKEYUP` mapped through the scan code (bits 16-23 plus the extended bit; `MapVirtualKeyW` only for what
      the scan code cannot tell), so the `Key` enum is layout-independent the way Linux's evdev codes are; left/right modifiers told apart (`VK_LSHIFT` etc. via
      the scan code), `Pause`, `PrintScreen`, numpad with and without NumLock; auto-repeat follows what the Linux backends report (the `KeyboardState` contract).
      A table test of the translation covers every `Key`.
      *(Corrected: the keys stay layout-aware, as `input.h` and both Linux backends define them ("the same meaning" is the day's goal): a
      key that types a character is named by the layout's character for it, the scan code names the rest and tells left from right. The
      scan-code-only, layout-independent mapping the task describes would have given Windows another meaning than Linux.)*
- [x] **Modifiers and locks:** `ModifierBit`s from `GetKeyState` at each key event; Caps/Num lock state; `Alt` alone does not open the system menu (handle
      `WM_SYSKEYDOWN`/`WM_SYSCHAR`/`SC_KEYMENU` as the spec says), `Alt+F4` still closes.
      *(Shift, Ctrl, Alt and Super come from the keys the window saw held, as the Linux backends take them from their events, not
      from `GetKeyState`: Windows' AltGr is a synthetic left Ctrl plus the right Alt, and `GetKeyState` would report Ctrl and Alt where XKB
      reports neither. The locks are `GetKeyState`'s toggles.)*
- [x] **Text input:** `WM_CHAR` with UTF-16 surrogate pairs joined into one code point, delivered as UTF-8 the way the Linux backends deliver text; `WM_UNICHAR`
      accepted; dead keys and IME composition produce the composed text (an IME's composition window is not supported this phase — filed for the UI phase).
      *(Not done: no backend delivers text yet (`InputState` has no field for it; X11 runs the lookup and drops the text), so the
      characters typed, dead keys and input methods wait for the phase that adds text input on every backend (the UI phase, 16): filed. A
      dead key names no `Key`, as on Linux.)*
- [x] **Mouse:** position from `WM_MOUSEMOVE` (client coordinates, physical pixels), buttons including X1/X2, wheel (`WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL`, fractional
      `WHEEL_DELTA` accumulated), `TrackMouseEvent` for leave; relative motion from raw input (`RegisterRawInputDevices` for the mouse, `WM_INPUT`) when the
      interface asks for relative mode; capture (`SetCapture` while a button is held) and cursor clip/hide if the interface has them.
      *(`os::Window` has no relative mode, cursor clip or hide, so there is no raw input today; capture while a button is held is done.)*
- [x] **Focus loss:** `WM_KILLFOCUS`/`WM_ACTIVATEAPP` releases held keys and buttons, matching the Linux rule (the X11 review found a bug here: keys released on a
      real focus loss but not during grabs; the Windows rule is written down and tested).
- [x] **Tests:** the shared input tests run on Windows; `win32_input_windows_test.cpp` (phase 13) drives the window with `SendInput` (key presses, text with a
      surrogate pair, mouse buttons, wheel) and checks the `InputState`; a translation table test without a window.
      *(The window cases post Windows' messages to the window instead of typing with `SendInput`: `SendInput` goes to whatever window
      has the desktop's focus, which a test cannot be sure is its own, and would type into the developer's other programs. They use
      layout-neutral keys (and skip the letter case on a non-Latin layout). The shared input tests are the Linux keyboard tests (XKB);
      `win32_input_windows_test.cpp` is Windows' counterpart.)*
- [ ] **Human check** (record or file): typing, held keys, mouse buttons, wheel and relative motion in `rhi_demo` (or a small input viewer if the demo cannot run
      until Day 161).
      *(Not done: `rhi_demo` reaches Windows on Day 161; `hello_window` logs the input state for a person to try meanwhile. Filed.)*
- [x] **Documentation:** `input.h` notes the Windows differences; `platform-windowing-and-input.md` "Windows input"; reference regenerated; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` if `key_translate.cpp` or `input.h` changed (record); quote the lines.
      *(Windows done; the Linux run waits for the phase close, Day 174.)*
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: extended-key and Pause scan codes, a surrogate pair split across two `PeekMessage` drains,
      stuck keys after focus loss, raw-input buffer sizes.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** keyboard, text, mouse and relative motion fill `InputState` on Windows with the same meaning as on Linux, focus loss releases held input, and the
input tests pass on Windows.

## Notes (2026-11-08, Windows PC)

- **Before the day:** the pull brought the Linux side's fix for Days 157-158 (the unity build: `window_backend_test.cpp`'s using-directives
  leaked into the X11 test merged after it) and a note that the Wayland `renderer_demo` crash predates them. The Days 157-158 Linux entries in
  `KNOWN-ISSUES.md` stay open: that was a build fix, not the phase-close run.
- **The keysym translation** moved whole to `platform/linux/key_translate.cpp` with `platform/linux/key_translate.h` (`KeyFromKeysym`); `input.h`
  no longer declares it, and `module_deps_check`'s allow-list entry for it is gone (nothing was left that every backend shares).
- **Keys are layout-aware, as on Linux** (a correction of the task, which asked for a scan-code mapping "layout-independent the way Linux's evdev
  codes are"; but the Linux backends name keys by keysym, and `input.h` says keys are layout-aware). `os::win32::KeyFromWin32`
  (`platform/windows/win32_keys.h`) names a key that types a character by the character the active layout gives it (`ToUnicodeEx` with Shift,
  Caps Lock and AltGr only, and flag 4 so a dead key still composes), and every other key by its virtual key and scan code: left and right Shift
  by the scan code, Ctrl and Alt by the extended flag, the number pad's navigation keys `Unknown` like XKB's `KP_Home`, its digits `Num0`..`Num9`,
  Pause, Print Screen, NumLock and the number pad's operators `Unknown`. A release clears the Key its physical key was pressed as
  (`Win32KeyIndex`, the scan code with the extended flag), as X11 does; auto-repeat changes nothing; one Shift release ends both Shift keys
  (Windows sends none for the first).
- **Modifiers** come from the keys the window saw held, not `GetKeyState`: AltGr arrives as a synthetic left Ctrl plus the right Alt with the
  same message time, the Ctrl is dropped, and AltGr is `RightAlt` with neither `kModCtrl` nor `kModAlt`, as XKB's level-3 shift. The locks are
  the system's toggles. **Alt and F10 alone** open no menu (`SC_KEYMENU` with no character is refused); Alt+Space still opens the system menu,
  Alt+F4 still closes, Alt+letter does not beep.
- **Mouse:** client coordinates in physical pixels; `pointer_inside` from the move and `WM_MOUSELEAVE` (`TrackMouseEvent`); a press captures the
  mouse so the release arrives outside the window; buttons left, right, middle, X1 (back), X2 (forward); the wheel at 10 units per notch, down and
  right positive, fractions added; another window taking the capture releases the buttons.
- **Focus loss** (`WM_KILLFOCUS`) releases every key, button and modifier; Windows sends it only when the keyboard focus really leaves, not for
  the window's own menus. Tested with the message and with `SetFocus` between two windows of the program.
- **Not done, filed:** text input (no backend delivers it; the UI phase), relative mouse motion and cursor clip or hide (no API).
- **`rhi_boundary_check`:** it read Win32's virtual-key names (`VK_ESCAPE`) as Vulkan identifiers. In `engine/platform/windows/` only `vk*`,
  `Vk*`, `PFN_vk*` and Vulkan includes count now; planted fixture `fixtures/bad/engine/platform/windows/vulkan_in_win32.cpp` (a `vkCreateInstance`
  there is still reported), clean fixture `fixtures/good/engine/platform/windows/keys.cpp`.
- **Tests:** `tests/windows/win32_input_windows_test.cpp` (phase 13, in `windows_platform_tests`, plain and ASan): the translation table (every
  `Key`), layout-awareness and the `Unknown` cases, and a real window fed the messages: presses and releases, left and right, the release of the
  pressed Key, repeat, both Shift keys, a letter with Ctrl, AltGr, Alt alone and Alt+F4, the mouse (position, five buttons, capture, outside the
  client area, the wheel, leaving, the capture taken), focus loss by message and by `SetFocus`, a window without an `InputState`, keys without a
  scan code, a left Ctrl before a right Alt.
- **Docs:** `input.h` (the differences on Windows), the guide's "Windows input", `win32_window.h`, the reference (lint 4107/4107).
- **For the Linux run at the phase close:** `engine/platform/linux/key_translate.{h,cpp}`, `linux/xkb_keyboard.cpp`, `linux/x11/x11_window.cpp`,
  `engine/platform/input.h`, `engine/platform/CMakeLists.txt`, `tests/foundation/keyboard_input_test.cpp`, `tools/module_deps_check/allowlist.txt`,
  `tools/rhi_boundary_check` (the rule and fixtures). On Linux: `scripts/test-phase.sh --affected` with asan and tsan, both unity settings.

## Runs

Windows PC, after the review's fixes:

- `scripts/test-phase.sh --affected`: `ran 588 of 1198 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 64 s; the last
  full run took 13 s` (everything: the day changed `CMakeLists.txt` files).
- `scripts/test-phase.sh --affected --flavor asan`: `ran 348 of 1198 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 27 s;
  the last full run took 13 s`.
- Full `ctest -j8`: MSVC `100% tests passed out of 1198`, `windows-clang-cl` `100% tests passed out of 751`, non-unity MSVC
  `100% tests passed out of 1198` (the docs tests among them).
- Linux: not run; pending until the phase close (Day 174), `KNOWN-ISSUES.md`.

## Post-implementation code review

`/code-review high` on the day's diff: 5 findings, 2 fixed, 3 declined.

- **Fixed:** key messages with scan code 0 (injected input carries the virtual key alone) all used one slot of `held_`, so one key's release
  cleared another's and left the first stuck. The scan code now comes from `MapVirtualKeyW(MAPVK_VK_TO_VSC_EX)`. Test: `Windows: keys sent without
  a scan code each keep their own state` (fails without the fix).
- **Fixed:** the AltGr check read `GetMessageTime` after peeking at the next message, which makes that one "the last", so any left Ctrl followed
  by a right Alt in the queue was taken for AltGr. The time is read first. Test: `Windows: a left Ctrl pressed some time before a right Alt is a
  left Ctrl, not AltGr` (fails without the fix).
- **Declined:** the main and the number pad's Enter are both `Key::Enter`, so releasing one while the other is held reads Enter up. The Linux
  backends do the same (`Return` and `KP_Enter` are one Key); one `Key` per name is the enum's design.
- **Declined:** `ToUnicodeEx`'s flag 4 (leave the keyboard state alone) exists from Windows 10 1607 (2016); on an older build a dead key would be
  consumed by the lookup. The manifest names Windows 10; older builds are not supported, and not checked at run time.
- **Declined:** mouse messages do not refresh the modifier mask, as X11 does from each event. Here the mask changes only with key messages and a
  focus loss, which already update it; only a lock toggled in another program stays stale until the next key.
