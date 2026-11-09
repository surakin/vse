# Window layer: what a platform backend implements

The window layer (`engine/platform/`) is polled: a program calls `Window::PumpEvents`, then reads the `InputState` the backend filled and drains the
input event stream with `Window::PollEvent`. This list is kept in step with the source tree by the test `window_backend_lists`: every platform
directory under `engine/platform/` has a `###` section here (headed with the directory name in backticks), so a new backend cannot be added, and a
planned one cannot be forgotten, without its checklist. Design and reasons: `docs/superpowers/specs/2026-11-10-ui-design.md`.

## The input event stream (every platform)

A backend overrides `Window::PollEvent` and `Window::DroppedEvents` and queues, in the order the server delivered them, one
[[os::InputEvent]] for each of:

- **Keys:** `KeyDown` (with `repeat` set for an auto-repeat) and `KeyUp`, with the layout-aware `Key` and the modifiers at that moment. Where the server
  does not repeat keys (Wayland), the backend repeats them itself from the server's rate and delay, on the program's thread, inside `PumpEvents`, with a
  cap on repeats per pump.
- **Text:** one `Text` event per Unicode scalar the key produced, as UTF-8, after the `KeyDown` that caused it (and again with each repeat). A dead key or the
  middle of a compose sequence produces none; the completed sequence produces one. A press with Ctrl or Alt held, and Enter, Tab, Backspace, Escape and
  Delete, produce none. No input-method (IME) composition: text arrives as the keyboard layout and the compose table give it.
- **Pointer:** `PointerMove`, `PointerButton` (with the new state), `PointerEnter`, `PointerLeave`, in window pixels.
- **Wheel:** `Wheel` in notches, positive right and down, as `InputState::scroll_dx/dy`. A backend whose wheel is a continuous axis reports fractions.
- **Focus:** `FocusGained`, and on loss a `KeyUp` for every key still held, then `FocusLost`, so a program tracking keys from the stream never sees a key stuck.
- **Resize:** `Resize` with the new size in pixels.

The queue is `os::InputEventQueue` (1,024 events); when the program does not drain it the **newest** events are dropped and counted
(`DroppedEvents`), never older ones. `InputState` keeps working as before and agrees with the stream.

## Platform notes

### `linux`

Implemented, in two backends. **Wayland** (`linux/wayland/`): `wl_keyboard` key events through `XkbKeyboard` and `KeyboardEvents` (`linux/keyboard_events.*`,
text, compose and client-side repeat from `wl_keyboard.repeat_info`, so the seat is bound at version 5), pointer and axis events, `wl_keyboard.enter/leave` for focus,
the configure event for resize. **X11** (`linux/x11/`): detectable auto-repeat (`XkbSetDetectableAutoRepeat`) so a held key is one press and a stream of repeats,
text from `Xutf8LookupString` (committed input-method text arrives as `Text` as well), buttons 4 to 7 as wheel notches, focus events filtered for grab modes,
`ConfigureNotify` for resize. The compose table comes from the locale (`LC_ALL`, `LC_CTYPE`, `LANG`); without one, dead keys are not combined.

### `windows`

Not implemented yet for the event stream (Phase 15 Day 205): `win32_window.cpp` fills `InputState` today; Day 205 adds `WM_KEYDOWN`/`WM_KEYUP` (repeat from
the repeat flag in `lParam`), `WM_CHAR` (UTF-16 surrogate pairs joined into one scalar), `WM_MOUSEMOVE`, the button messages, `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL`
(`WHEEL_DELTA` units to notches), `WM_SETFOCUS`/`WM_KILLFOCUS` and `WM_SIZE`.

### `macos`

Not implemented yet (Phase 22): `NSEvent` key events with `interpretKeyEvents:` for text, dead keys and input methods, `scrollingDeltaX/Y`, `keyDown` repeat from
`isARepeat`, `windowDidBecomeKey`/`windowDidResignKey`, `windowDidResize`. The `PollEvent` default (no events) keeps the layer compiling meanwhile.

### `android`

Not implemented yet (Phase 23): `AInputEvent` key and motion events, text from the soft keyboard (an IME is unavoidable there), touch as the pointer.

### `ios`

Not implemented yet (Phase 23): `UIKeyCommand`/`pressesBegan` for hardware keys, `UITextInput` for the soft keyboard, touches as the pointer.
