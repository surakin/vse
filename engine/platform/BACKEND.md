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

## The cursor and the pointer lock (every platform)

A backend overrides the `Window` operations below; each has a `Supports...` query that is false where the backend (or the compositor) cannot do it, and the operation then returns false and changes nothing.

- **Shapes:** `SetCursor(CursorShape)` for the ten shapes of [[os::CursorShape]] (`Hidden` shows no cursor). The shape stays in effect while the pointer is over the window and is applied again when it re-enters (Wayland needs the enter serial).
- **Custom image:** `SetCursorImage(CursorImage)` takes premultiplied RGBA8 up to 128x128 with a hotspot inside the image and a scale 1 to 4; the window copies the pixels, an equal image is not made again, a replacement frees the old one, `ClearCursorImage` removes it and the shape shows again. The image outranks the shape. Invalid images are refused with [[os::ValidateCursorImage]].
- **Pointer lock:** `SetPointerLock(bool)`, `PointerLocked()`. While locked the pointer does not move and `RelativeMotion` events (unaccelerated device units) arrive; never while unlocked. The rules are [[os::PointerLockState]]: held only while the window has focus, released on focus loss and asked for again on focus gain while still wanted, a request while unfocused waits for focus, a second request is a no-op, destroying the window releases it. A lock the system takes away stays wanted but is asked for again only after the next focus change.

## Platform notes

### `linux`

Implemented, in two backends. **Wayland** (`linux/wayland/`): `wl_keyboard` key events through `XkbKeyboard` and `KeyboardEvents` (`linux/keyboard_events.*`,
text, compose and client-side repeat from `wl_keyboard.repeat_info`, so the seat is bound at version 5), pointer and axis events, `wl_keyboard.enter/leave` for focus,
the configure event for resize. **X11** (`linux/x11/`): detectable auto-repeat (`XkbSetDetectableAutoRepeat`) so a held key is one press and a stream of repeats,
text from `Xutf8LookupString` (committed input-method text arrives as `Text` as well), buttons 4 to 7 as wheel notches, focus events filtered for grab modes,
`ConfigureNotify` for resize. **Cursor and lock (Day 186).** Wayland: shapes through `wp_cursor_shape_v1` (without it `SupportsCursorShape()` is false and the compositor's cursor stays; `Hidden` still works through `wl_pointer.set_cursor`; `libwayland-cursor` is not used), images as a `wl_shm` ARGB buffer on a cursor surface with the window's buffer scale, set with the serial of the last `wl_pointer.enter` and re-set on every enter; the lock is `zwp_pointer_constraints_v1.lock_pointer` (one-shot) with `zwp_relative_pointer_v1` for the motion, available only when the compositor offers both. X11: shapes from the cursor theme (`XcursorLibraryLoadCursor`) with the core font cursors as the fallback, images as Xcursors at their pixel size (X11 has no cursor scaling), the lock is `XGrabPointer` confined to the window with a blank cursor and XInput 2 raw motion (without XInput 2 the grab works and no relative motion arrives). The compose table comes from the locale (`LC_ALL`, `LC_CTYPE`, `LANG`); without one, dead keys are not combined.

### `windows`

Not implemented yet for the event stream, the cursor and the lock (Phase 15 Day 205; cursor shapes `SetCursor` with the system cursors, custom images `CreateIconIndirect`, the lock `ClipCursor` plus `WM_INPUT` raw mouse input): `win32_window.cpp` fills `InputState` today; Day 205 adds `WM_KEYDOWN`/`WM_KEYUP` (repeat from
the repeat flag in `lParam`), `WM_CHAR` (UTF-16 surrogate pairs joined into one scalar), `WM_MOUSEMOVE`, the button messages, `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL`
(`WHEEL_DELTA` units to notches), `WM_SETFOCUS`/`WM_KILLFOCUS` and `WM_SIZE`.

### `macos`

Not implemented yet (Phase 22; cursors `NSCursor`, the lock `CGAssociateMouseAndMouseCursorPosition`): `NSEvent` key events with `interpretKeyEvents:` for text, dead keys and input methods, `scrollingDeltaX/Y`, `keyDown` repeat from
`isARepeat`, `windowDidBecomeKey`/`windowDidResignKey`, `windowDidResize`. The `PollEvent` default (no events) keeps the layer compiling meanwhile.

### `android`

Not implemented yet (Phase 23): `AInputEvent` key and motion events, text from the soft keyboard (an IME is unavoidable there), touch as the pointer.

### `ios`

Not implemented yet (Phase 23): `UIKeyCommand`/`pressesBegan` for hardware keys, `UITextInput` for the soft keyboard, touches as the pointer.
