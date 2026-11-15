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

## The clipboard (every platform)

`Window` has `SupportsClipboard(kind)`, `SetClipboard(kind, ClipboardOffer)`, `RequestClipboard(kind, format, custom_name)` (asynchronous: returns an id, the finished answer is a `ClipboardData` event and `TakeClipboardResult(id)`), `ClipboardHasFormat` and the `ClipboardChanged` event. Kinds are `Clipboard` and `Primary`; formats are `Text` (UTF-8), `FileList` (absolute local paths, `\n`-separated) and `Custom` (bytes under a name). A backend must: copy what it is offered and serve it until it loses the selection (it never keeps a pointer into the program's memory); answer a request exactly once, with `Timeout` after [[os::kClipboardTimeoutNs]] without progress; refuse transfers above [[os::kMaxClipboardBytes]]; clean text with [[os::SanitizeUtf8]] on the way in and out; take file lists only through [[os::ParseUriList]] (the shared rules, so no backend follows a network URL); keep at most [[os::ClipboardRequests::kMaxInFlight]] requests open (use [[os::ClipboardRequests]], which does the bookkeeping); queue `ClipboardChanged` when another program takes the selection; and fail the open requests when the window closes.

## Drag and drop between programs (every platform)

`Window` has `SupportsDragAndDrop()`, `SetDropAccept(DropAccept)`, `SetDropAction(DragAction)`, `RequestDropData(format, custom_name)` (asynchronous like the clipboard: a `DropData` event and `TakeClipboardResult(id)`), `FinishDrop(action)`, `BeginDrag(DragSource)`, `CancelDrag()` and `Dragging()`. Events: `DragEnter`, `DragOver`, `DragLeave`, `Drop` (receiving) and `DragEnded` (sending). A backend must: tell the other program the drag is refused, without any event for the program, when `SetDropAccept` names no format on offer; report positions in window pixels; negotiate the action from the source's allowed actions and the last `SetDropAction` (Shift asks for move, Ctrl for copy, where the platform says so); read dropped data only through the same rules as the clipboard (`SanitizeUtf8`, `ParseUriList`, the byte limit) and cut a file list at [[os::kMaxDropFileEntries]] entries with [[os::LimitPathList]], counting the rest as rejected; cap the types a source offers at [[os::kMaxDragTypes]]; finish a drop the program never finished after [[os::kDropSessionTimeoutNs]]; copy the offered bytes of a drag it starts (at most [[os::kMaxDragOffers]] offers) and end it with `DragEnded` (`Nothing` when cancelled, refused or unanswered for [[os::kDragTargetTimeoutNs]]); and end a running drag when the window closes.

## Platform notes

### `linux`

Implemented, in two backends. **Wayland** (`linux/wayland/`): `wl_keyboard` key events through `XkbKeyboard` and `KeyboardEvents` (`linux/keyboard_events.*`,
text, compose and client-side repeat from `wl_keyboard.repeat_info`, so the seat is bound at version 5), pointer and axis events, `wl_keyboard.enter/leave` for focus,
the configure event for resize. **X11** (`linux/x11/`): detectable auto-repeat (`XkbSetDetectableAutoRepeat`) so a held key is one press and a stream of repeats,
text from `Xutf8LookupString` (committed input-method text arrives as `Text` as well), buttons 4 to 7 as wheel notches, focus events filtered for grab modes,
`ConfigureNotify` for resize. **Cursor and lock (Day 186).** Wayland: shapes through `wp_cursor_shape_v1` (without it `SupportsCursorShape()` is false and the compositor's cursor stays; `Hidden` still works through `wl_pointer.set_cursor`; `libwayland-cursor` is not used), images as a `wl_shm` ARGB buffer on a cursor surface with the window's buffer scale, set with the serial of the last `wl_pointer.enter` and re-set on every enter; the lock is `zwp_pointer_constraints_v1.lock_pointer` (one-shot) with `zwp_relative_pointer_v1` for the motion, available only when the compositor offers both. X11: shapes from the cursor theme (`XcursorLibraryLoadCursor`) with the core font cursors as the fallback, images as Xcursors at their pixel size (X11 has no cursor scaling), the lock is `XGrabPointer` confined to the window with a blank cursor and XInput 2 raw motion (without XInput 2 the grab works and no relative motion arrives). **Clipboard (Day 187).** Wayland: `wl_data_device_manager` (version 3) and, when offered, `zwp_primary_selection_device_manager_v1`; `Set` needs the serial of the last key, button or enter event (`WaylandInput` records it) and fails before the first; the MIME types are `text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING` for text and `text/uri-list` for files; data moves through non-blocking pipes (`linux/clipboard_pipe.*`) that `PumpEvents` advances, writes block signal `SIGPIPE` for their duration, and a stalled peer costs only its timeout; our own selection answers our own requests locally; every listener slot of the data objects is filled (libwayland aborts on a null one). X11: `CLIPBOARD` and `PRIMARY` with `TARGETS`, `UTF8_STRING`, `STRING` (Latin-1, only when lossless), `text/plain`, `text/uri-list`, `TIMESTAMP`; `INCR` in both directions above 256 KiB; `SelectionClear` and XFixes owner notifications give `ClipboardChanged`; the selection is released (with a round trip) when the window closes, no clipboard manager keeps it. **Drag and drop (Day 188).** Wayland: `wl_data_device` enter, motion, leave and drop with `wl_data_offer` (`accept`, `set_actions` copy and move, `finish`); a drag we start uses `wl_data_device.start_drag` with the serial of the last key or button and an ARGB icon surface; there is no test compositor, so this path is covered by a listener-completeness test only. X11: XDND version 5 both ways: `XdndAware`, `XdndEnter` with `XdndTypeList`, `XdndPosition`/`XdndStatus`, `XdndLeave`, `XdndDrop`/`XdndFinished`, data through `XdndSelection`; as a source the pointer and keyboard are grabbed on the root window, the target is found under the pointer (following `XdndProxy`), and the drag image is not drawn. The compose table comes from the locale (`LC_ALL`, `LC_CTYPE`, `LANG`); without one, dead keys are not combined.

### `windows`

Implemented (Phase 15 Day 205) in `windows/win32_window.cpp`, `win32_clipboard.cpp` and `win32_dragdrop.cpp`. **Events:** `WM_KEYDOWN`/`WM_KEYUP` (and the
`SYS` forms) give `KeyDown` (repeat from bit 30 of `lParam` while the key is held) and `KeyUp` with the modifiers held before the key; AltGr's synthetic left Ctrl is
dropped, and one Shift release ends both Shift keys with a `KeyUp` each. **Text** comes from `WM_CHAR` after `TranslateMessage` (the layout, dead keys and AltGr are
the system's; `WM_DEADCHAR` is ignored); a surrogate pair becomes one scalar, control characters and Ctrl or Alt held give none, and there is no IME composition (the
committed characters arrive as `WM_CHAR`). The first `WM_MOUSEMOVE` inside is the `PointerEnter`, `WM_MOUSELEAVE` (`TrackMouseEvent`) the `PointerLeave`; buttons
capture the mouse; `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` are `WHEEL_DELTA` units turned into notches (and 10 per notch in `InputState`). `WM_SETFOCUS`/`WM_KILLFOCUS`
are the focus events, `WM_SIZE` the `Resize` (the stream starts with the settled size). Events queued while Windows runs a modal loop (the user dragging the frame) stay
queued. **Cursor:** the shapes are the system cursors, applied on `WM_SETCURSOR` over the client area (the frame keeps the system's resize cursors); an image is a
32-bit DIB made into a cursor with `CreateIconIndirect`, straight alpha, at its pixel size (Windows does not scale cursors by DPI, so `scale` is not applied).
**Lock:** `ClipCursor` to a point at the client area's centre (kept there across moves and resizes), the cursor hidden, raw mouse input
(`RegisterRawInputDevices`, `WM_INPUT`) for `RelativeMotion`; released on unlock, focus loss and close. **Clipboard:** `CF_UNICODETEXT`, `CF_HDROP` and
`RegisterClipboardFormat` names; the Win32 calls are synchronous, so a request is answered at once and announced by its event; `WM_CLIPBOARDUPDATE`
(`AddClipboardFormatListener`) gives `ClipboardChanged` for another program's change; no primary selection; unlike X11 and Wayland the system keeps the data after
the window closes. File lists are native paths, not `file://` URIs, so they pass `os::win32::AcceptLocalPath` instead of `ParseUriList`: only drive-absolute paths,
no UNC or device path, no `.` or `..`, no control character or stream. **Drag and drop:** OLE on the window's thread (`OleInitialize`; a thread already in the
multithreaded apartment has none and `SupportsDragAndDrop` is false). Receiving: an `IDropTarget`; OLE's data object is valid only during `Drop`, so every accepted
format is read then and `RequestDropData` answers from that copy; the answer is the program's `SetDropAction`, else Copy, with Windows' keys (Shift moves, Ctrl and
Shift link). Sending: `DoDragDrop` is modal, so `BeginDrag` calls `DragSource::started` before the drag runs, returns when it is over (after queueing `DragEnded`), and the
modal-loop callback keeps the program drawing; it refuses to start unless a mouse button is down; the drag image is not drawn (`IDragSourceHelper` is not used).

### `macos`

**Built (Phase 21 Day 295): the window** (`macos/cocoa_window.mm`): `NSWindow` with a `CAMetalLayer`-backed view, `PumpEvents` draining
`nextEventMatchingMask:untilDate:distantPast`, the size in pixels at the backing scale, `windowShouldClose:` and Quit as close requests,
native fullscreen; the window host (`macos/window_host_macos.cpp`) and the entry point (`macos/entry_point_macos.cpp`). **The event stream
(Day 296):** the content view is the first responder. `keyDown:`/`keyUp:` give `KeyDown` (repeat from `isARepeat`, or a press of a key already
down) and `KeyUp`, named by `KeyFromCocoa` (`macos/cocoa_keys.*`: the key code for keys named by position, `charactersIgnoringModifiers` for
the others); a release ends the `Key` its key was pressed as (in `InputState` only once no other key of that name is held), and a key-up for a key the window never saw go down is dropped. `flagsChanged:`
gives the modifier keys (the side from the device bits; Caps Lock as a press and a release at once, since macOS reports only its toggle), with
the modifiers held before them. Option is Alt, Command is Super. AppKit does not pass a key-up with Command held to the window, so `PumpEvents`
gives such an event to its window directly. **Text:** a minimal `NSTextInputClient`; `keyDown:` calls `interpretKeyEvents:` unless Control or
Command is held, `insertText:` gives a `Text` per scalar (control characters and AppKit's function-key scalars U+F700-U+F8FF dropped), dead keys
and input methods are the system's (marked text is tracked, not shown), `doCommandBySelector:` gives nothing; Option's characters do arrive (on
macOS it is the layout's third level). **Pointer:** `mouseMoved:` (from the tracking area, key or not; a move outside with no button held is dropped) and the three `*Dragged:` as `PointerMove`,
the three button pairs (`buttonNumber` 2 to 4: middle, back, forward), positions in pixels from the top left; an `NSTrackingArea`
(`ActiveAlways`, `InVisibleRect`, `MouseMoved`, `EnabledDuringMouseDrag`) for `PointerEnter`/`PointerLeave`, and the first move inside is an enter when none came; `acceptsFirstMouse:`
is true. **Wheel:** `scrollingDeltaX/Y`, signs turned (macOS: positive up and left), precise deltas over 10 points per notch, line deltas as
notches; `InputState` 10 units per notch. **Focus:** `windowDidBecomeKey` (the modifiers read again) and `windowDidResignKey` (a `KeyUp` per held
key, then `FocusLost`; buttons stay held); `Close` releases the keys too. **Resize:** `windowDidResize` and `windowDidChangeBackingProperties`;
the stream starts with the settled size. **Cursor and lock (Day 297,
`macos/cocoa_cursor.mm`):** the shapes are `NSCursor`s (the diagonal resize cursors from macOS 15's frame-resize cursors, else the arrow;
`ResizeAll` the open hand), set from `cursorUpdate:` (the tracking area has `NSTrackingCursorUpdate`) and on every change with the pointer inside;
`Hidden` and the lock are `[NSCursor hide]`, kept to exactly one hide per window and undone on leave, close and destruction; an image is an
`NSCursor` from an `NSBitmapImageRep` (premultiplied RGBA, the default format) of `width / scale` points. The lock warps to the view's centre and
calls `CGAssociateMouseAndMouseCursorPosition(false)` (no Accessibility permission needed); `RelativeMotion` from `deltaX/deltaY` (accelerated),
the first one after a warp that moved the cursor dropped, and the cursor warped back to the centre when the window moves or resizes. **Clipboard (`macos/cocoa_clipboard.mm`):** the general `NSPasteboard`; text `NSPasteboardTypeString`, files
as file URLs through `BuildUriList`/`ParseUriList` both ways, custom formats as the named type; synchronous, announced by the event;
`changeCount` polled in `PumpEvents` (every 250 ms at most) for `ClipboardChanged`; a file list cut at `kMaxDropFileEntries` URLs before the rules; no `Primary`; the system keeps the data after the window closes. An `NSString`'s
unpaired surrogates become U+FFFD. **Drag and drop (`macos/cocoa_dragdrop.mm`):** the view is the `NSDraggingDestination`
(`registerForDraggedTypes:` from `SetDropAccept`, at most `kMaxDragTypes` types looked at, every accepted format read in
`performDragOperation:`) and the `NSDraggingSource` (`beginDraggingSessionWithItems:event:source:` from the held button's event); the source's
operation mask is the allowed actions (Generic is a move); the end releases the buttons AppKit's drag loop took the release of; `CancelDrag` makes
the mask none, since the session cannot be stopped, and `DragEnded` gives what the target did; `Close` during a drag reports `DragEnded`
(`Nothing`) and keeps the view alive for the session's last callbacks.

### `android`

Not implemented yet (Phase 29): `AInputEvent` key and motion events, text from the soft keyboard (an IME is unavoidable there), touch as the pointer.

### `ios`

Not implemented yet (Phase 29): `UIKeyCommand`/`pressesBegan` for hardware keys, `UITextInput` for the soft keyboard, touches as the pointer.
