# Phase 26 / Day 280 — Cocoa II: Keyboard, Text, Pointer and the Event Stream

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 279.

The input event stream of `engine/platform/BACKEND.md` on macOS: keys, text, pointer, wheel, focus and resize, in order, through the shared
`os::InputEventQueue`, and `InputState` agreeing with it.

## Tasks

- [ ] **Keys** (`platform/macos/cocoa_keys.mm/.h`): a table from `keyCode` (layout-independent virtual key codes) to `os::Key` for every key the
      enum names, with the layout-aware character from `charactersIgnoringModifiers` where `os::Key` is layout-aware (as the other backends define it);
      `KeyDown` with `repeat` from `isARepeat`, `KeyUp`; modifiers from `modifierFlags` (Command maps to the engine's `Super`/`Meta` as the input model
      defines); `flagsChanged:` produces modifier key events; Cmd-key presses reach the view (not swallowed by the menu unless they are menu shortcuts).
- [ ] **Text:** the view implements `NSTextInputClient` minimally and calls `interpretKeyEvents:`; `insertText:` gives one `Text` event per scalar after
      its `KeyDown`; dead keys (Option-E then E gives `é`) produce none until committed; Ctrl- or Cmd-held presses and Enter, Tab, Backspace, Escape,
      Delete produce none; marked text (IME composition) is ignored, as on the other backends (documented).
- [ ] **Pointer:** `mouseMoved:`/`*Dragged:` (with `acceptsMouseMovedEvents`) as `PointerMove` in window pixels with Y flipped to top-left origin; buttons
      left, right, other (`buttonNumber`); `NSTrackingArea` for `PointerEnter`/`PointerLeave`.
- [ ] **Wheel:** `scrollingDeltaX/Y`; precise (trackpad) deltas become fractional notches by a documented divisor, line deltas whole notches; signs per the
      engine's "positive right and down" with the user's natural-scrolling setting left to the system (documented: macOS inverts before we see it).
- [ ] **Focus:** `windowDidBecomeKey`/`windowDidResignKey`; on loss a `KeyUp` for every held key, then `FocusLost` (tested by losing focus with a key held).
- [ ] **Resize** events in pixels; the stream starts with the settled size.
- [ ] **`InputState`** agrees with the stream (the shared tests that compare them run on macOS).
- [ ] **Tests:** the window-layer event tests that inject platform events run with synthesized `NSEvent`s (`keyEventWithType:...`,
      `mouseEventWithType:...` posted to the window) in the display group; the key table test covers every `os::Key`; `input_probe` checked by hand
      on this MacBook's keyboard and trackpad (US layout plus one dead-key layout; recorded).
- [ ] **Documentation:** `BACKEND.md` `macos` section for the event stream as built; the guide's macOS section; `KNOWN-ISSUES.md` line "input event stream
      ... macOS waits for Phase 26" closed; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: event ordering between `keyDown:` and `insertText:`, keys stuck after Cmd-Tab, the queue's
      newest-dropped rule.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** keys, text (with dead keys), pointer, wheel, focus and resize arrive on macOS in the stream with `InputState` agreeing, the window-layer event
tests pass with synthesized events in three flavors, and the event-stream deferral for macOS is closed.
