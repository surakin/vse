# Phase 26 / Day 313 — Cocoa III: Cursor, Pointer Lock, Clipboard, Drag and Drop

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 312.

The rest of the window layer's contract on macOS (`engine/platform/BACKEND.md`): cursor shapes and images, the pointer lock, the clipboard and drag
and drop between programs.

## Tasks

- [ ] **Cursor shapes:** the ten `os::CursorShape`s from `NSCursor` (`arrowCursor`, `IBeamCursor`, `pointingHandCursor`, `crosshairCursor`, the resize
      cursors; a shape with no system cursor documented with its fallback); applied through `resetCursorRects`/`cursorUpdate:` over the view, re-applied
      on enter; `Hidden` through `[NSCursor hide]` balanced exactly (hide/unhide is a counter: tested).
- [ ] **Cursor images:** premultiplied RGBA8 up to 128x128 to an `NSImage` with the image's scale (points = pixels / scale) and the hotspot; equal image not
      rebuilt; `ClearCursorImage` returns to the shape.
- [ ] **Pointer lock:** `CGAssociateMouseAndMouseCursorPosition(false)`, cursor hidden and warped to the view's centre, `RelativeMotion` from the events'
      `deltaX/Y`; the `os::PointerLockState` rules (focus loss releases, focus gain re-asks, close releases); without the Accessibility permission nothing
      is needed for this call (checked, recorded).
- [ ] **Clipboard** (`platform/macos/cocoa_clipboard.mm`): `NSPasteboard generalPasteboard`; `Text` as `NSPasteboardTypeString`, `FileList` as file URLs
      (`NSPasteboardTypeFileURL`, local only, through `ParseUriList`'s rules or a macOS path check like Windows' `AcceptLocalPath`, decided and documented),
      `Custom` as a declared type named from `custom_name`; synchronous calls answered at once and announced by their event (as Windows); `ClipboardChanged`
      by polling `changeCount` inside `PumpEvents`; no `Primary` kind (`SupportsClipboard(Primary)` false); limits and `SanitizeUtf8` applied; the system
      keeps the data after the window closes (documented, as Windows).
- [ ] **Drag and drop receiving:** the view as `NSDraggingDestination` (`registerForDraggedTypes:` from `SetDropAccept`), `draggingEntered:`/`Updated:`/
      `Exited:`/`performDragOperation:` as `DragEnter`/`DragOver`/`DragLeave`/`Drop`; the data read during `performDragOperation:` (valid only then) so
      `RequestDropData` answers from the copy; the action from `draggingSourceOperationMask` and `SetDropAction` (Option copies, Command moves, as macOS
      does); file-list cut at `kMaxDropFileEntries`; unfinished drops timed out.
- [ ] **Drag and drop sending:** `beginDraggingSessionWithItems:event:source:` from the last mouse-down event (refused without a button down), the offered
      bytes copied, `draggingSession:endedAtPoint:operation:` as `DragEnded`; the drag image from `DragSource::image` if given (else a default).
- [ ] **Tests:** the shared cursor, lock, clipboard and drag-and-drop tests run on macOS where they can be driven in-process (pasteboard round trips with a
      private named pasteboard so the user's clipboard is untouched; the drop target fed synthesized dragging info); the rest checked by hand with Finder
      and TextEdit (recorded).
- [ ] **Documentation:** `BACKEND.md` `macos` section complete as built; the guide; the Days 186-188 macOS lines in `KNOWN-ISSUES.md` closed; `docgen lint`
      zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: cursor hide/unhide balance on every exit path, pasteboard data lifetime, the drop
      session's copy and timeout.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every operation of the window layer's contract is implemented on macOS or reports `Supports...` false with a documented reason; the shared tests
pass in three flavors; the hand checks with Finder and TextEdit are recorded; the macOS clipboard, cursor and drag-and-drop deferrals are closed.
