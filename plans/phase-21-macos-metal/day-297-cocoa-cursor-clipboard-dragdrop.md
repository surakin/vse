# Phase 21 / Day 297 — Cocoa III: Cursor, Pointer Lock, Clipboard, Drag and Drop

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 296.

The rest of the window layer's contract on macOS (`engine/platform/BACKEND.md`): cursor shapes and images, the pointer lock, the clipboard and drag
and drop between programs.

## Tasks

- [x] **Cursor shapes:** the ten `os::CursorShape`s from `NSCursor` (`arrowCursor`, `IBeamCursor`, `pointingHandCursor`, `crosshairCursor`, the resize
      cursors; a shape with no system cursor documented with its fallback); applied through `resetCursorRects`/`cursorUpdate:` over the view, re-applied
      on enter; `Hidden` through `[NSCursor hide]` balanced exactly (hide/unhide is a counter: tested).
- [x] **Cursor images:** premultiplied RGBA8 up to 128x128 to an `NSImage` with the image's scale (points = pixels / scale) and the hotspot; equal image not
      rebuilt; `ClearCursorImage` returns to the shape.
- [x] **Pointer lock:** `CGAssociateMouseAndMouseCursorPosition(false)`, cursor hidden and warped to the view's centre, `RelativeMotion` from the events'
      `deltaX/Y`; the `os::PointerLockState` rules (focus loss releases, focus gain re-asks, close releases); without the Accessibility permission nothing
      is needed for this call (checked, recorded).
- [x] **Clipboard** (`platform/macos/cocoa_clipboard.mm`): `NSPasteboard generalPasteboard`; `Text` as `NSPasteboardTypeString`, `FileList` as file URLs
      (`NSPasteboardTypeFileURL`, local only, through `ParseUriList`'s rules or a macOS path check like Windows' `AcceptLocalPath`, decided and documented),
      `Custom` as a declared type named from `custom_name`; synchronous calls answered at once and announced by their event (as Windows); `ClipboardChanged`
      by polling `changeCount` inside `PumpEvents`; no `Primary` kind (`SupportsClipboard(Primary)` false); limits and `SanitizeUtf8` applied; the system
      keeps the data after the window closes (documented, as Windows).
- [x] **Drag and drop receiving:** the view as `NSDraggingDestination` (`registerForDraggedTypes:` from `SetDropAccept`), `draggingEntered:`/`Updated:`/
      `Exited:`/`performDragOperation:` as `DragEnter`/`DragOver`/`DragLeave`/`Drop`; the data read during `performDragOperation:` (valid only then) so
      `RequestDropData` answers from the copy; the action from `draggingSourceOperationMask` and `SetDropAction` (Option copies, Command moves, as macOS
      does); file-list cut at `kMaxDropFileEntries`; unfinished drops timed out.
- [x] **Drag and drop sending:** `beginDraggingSessionWithItems:event:source:` from the last mouse-down event (refused without a button down), the offered
      bytes copied, `draggingSession:endedAtPoint:operation:` as `DragEnded`; the drag image from `DragSource::image` if given (else a default).
- [x] **Tests:** (amended: the shared files `window_cursor_test.cpp`, `window_clipboard_test.cpp` and `window_dragdrop_test.cpp` mix their
      platform-neutral cases with Wayland and X11 code, so macOS gets its own backend tests, as Windows did on Day 205; the shared files are not
      touched) the shared cursor, lock, clipboard and drag-and-drop tests run on macOS where they can be driven in-process (pasteboard round trips with a
      private named pasteboard so the user's clipboard is untouched; the drop target fed synthesized dragging info); the rest checked by hand with Finder
      and TextEdit (recorded).
- [x] **Documentation:** `BACKEND.md` `macos` section complete as built; the guide; the Days 186-188 macOS lines in `KNOWN-ISSUES.md` closed; `docgen lint`
      zero.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: cursor hide/unhide balance on every exit path, pasteboard data lifetime, the drop
      session's copy and timeout.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every operation of the window layer's contract is implemented on macOS or reports `Supports...` false with a documented reason; the shared tests
pass in three flavors; the hand checks with Finder and TextEdit are recorded; the macOS clipboard, cursor and drag-and-drop deferrals are closed.

## As built (2026-11-12, the Mac)

- **Files:** `engine/platform/macos/cocoa_window_native.h` (the Objective-C side shared by the files below: the view's interface and its
  `Cursor` and `Dragging` categories, `CocoaWindow::Native`, the drop state, the pasteboard and image helpers), `cocoa_cursor.mm`,
  `cocoa_clipboard.mm`, `cocoa_dragdrop.mm`; `cocoa_window.mm` and `cocoa_window.h` extended. Tests: `tests/macos/cocoa_transfer_macos_test.mm`
  (17 cases, three flavors, `display`), one more case in `cocoa_input_macos_test.mm`.
- **Decisions:** the diagonal resize cursors are macOS 15's frame-resize cursors, the arrow before 15; `ResizeAll` is the open hand. `Hidden`
  and the lock keep exactly one `[NSCursor hide]` per window (`CursorHideDepth`, tested on every path). The lock needs no Accessibility
  permission (checked: granted at once in the tests and by hand). File lists go through `ParseUriList`/`BuildUriList` both ways (the Linux
  rules; not a Windows-style path check), with URLs built from the encoded text so the program's bytes are kept. The clipboard is synchronous,
  as on Windows; the system keeps it after the window closes. The default drop answer is Copy, else Move (macOS narrows the mask for Option and
  Command itself). `CancelDrag` cannot stop AppKit's session.
- **Found by the tests:** `fileURLWithPath:` rewrote `café` into the decomposed form, so a file list did not round trip byte for byte (now
  built from the uri-list text); `UTF8String` gives nothing for an `NSString` with an unpaired surrogate, so such typed text vanished (now
  `Utf8Of`, U+FFFD; the pasteboard itself stores such a string as nothing, seen on macOS 15).
- **Found by hand** (`input_probe`, the user at the Mac): the first `RelativeMotion` after taking the lock carried the warp's jump (219, 164
  and 248 points), now dropped when the warp moved the cursor; after a drag out of the window the button stayed down in `InputState`
  (AppKit's drag loop takes the release), now released at the drag's end. Both have tests shown failing first; the lock fix was checked by
  hand again (first motions after four locks: 5, 1, 2 and 2 points).
- **Hand check:** the ten shapes cycled, the custom image set and cleared, the lock taken and released (the cursor hidden and parked, relative
  motion arriving), Cmd-Tab with it; `Y` pasted into TextEdit as `café 🎉 from input_probe` (the user); TextEdit text and a Finder file pasted
  with `P` (`F`: text and files offered); text from TextEdit and a file from Finder dropped on the window (Copy, the right path); a text drag
  out of the window taken by TextEdit (`DragEnded` Copy) and one let go elsewhere (`Nothing`). **Not done by hand** (`KNOWN-ISSUES.md`): a file
  drag out to Finder, the custom drag image (`input_probe` gives none), `CancelDrag` during a real drag, the lock across a window move, a custom
  pasteboard type with another program, the drop timeout, macOS 13/14's arrow fallback for the diagonals.
- **Runs (macOS):** `scripts/test-phase.sh --affected --flavor all --display`: `ran 4264 of 4643 tests (phases: all; flavors: all; display:
  included; crash tests: excluded) in 1336 s`. It started before the review fixes. Two failures: the flaky `Throughput scales with
  participant count`, and the drag test under its old title (the review renamed it while the run was on, so ctest found no test of that name).
  None of the 116 skips was a window test. After the fixes: `ctest -L display` 159 of 159 in three flavors, no skips; the transfer tests 17 of
  17 three times in a row. Both unity modes build clean; `ctest -L docs` 3 of 3; the gates 62 of 62; `docs/reference/` regenerated, and
  `docgen lint` passes at zero.

## Post-implementation code review

`/code-review high` on the day's diff: 10 findings; 8 fixed, 2 in part.

1. **The lock warped only once**: a window moved or resized while locked left the parked cursor outside the view. Fixed: warped back on
   `windowDidMove` and every geometry change; test (the cursor at the new centre after a resize).
2. **`Close` did not end the window's own drag**, and AppKit might call a freed view. Fixed: `DragEnded` (`Nothing`) at close and the view kept
   alive until the session's end; test.
3. **After `CancelDrag`, `DragEnded` said `Nothing` even if a target had already taken the data.** Fixed: it reports what the target did; the
   guide says what `CancelDrag` can and cannot do on macOS.
4. **The first motion was dropped even after a warp of zero distance** (focus regained with the cursor parked). Fixed: dropped only after a
   real jump, and a pending drop goes with the lock; test.
5. **The arrow set on leave could overwrite a cursor another view had just set.** Fixed: only over the window's own cursor; test.
6. **A refused write after `clearContents` empties the pasteboard**, against "changing nothing". In part: NSPasteboard has no transaction, so
   the empty pasteboard cannot be avoided; every check that can fail runs before the clear, and such a change is now reported by
   `ClipboardChanged` instead of hidden. Documented.
7. **A file list over 16 MiB of URLs was refused whole.** Fixed: cut at `kMaxDropFileEntries` URLs before the rules, the rest counted.
8. **`changeCount` was asked on every `PumpEvents`.** Fixed: every 250 ms at most; the test pumps for up to a second.
9. **The image and string helpers were duplicated** between the cursor, clipboard and drag files. Fixed: `ImageOfPixels`, `StringOfBytes`.
10. **The BeginDrag test's title promised a bad-path case it did not check.** In part: retitled. `BeginDrag` checks the button before the
    offers, and a test cannot hold a real button, so the refusal of a bad path is covered through the shared `FileUrlsOf` by the clipboard test.
