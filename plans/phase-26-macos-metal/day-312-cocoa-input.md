# Phase 26 / Day 312 — Cocoa II: Keyboard, Text, Pointer and the Event Stream

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 311.

The input event stream of `engine/platform/BACKEND.md` on macOS: keys, text, pointer, wheel, focus and resize, in order, through the shared
`os::InputEventQueue`, and `InputState` agreeing with it.

## Tasks

- [x] **Keys** (`platform/macos/cocoa_keys.cpp/.h`: plain C++, the codes and flags as numbers, so the table is tested without a window): a table from `keyCode` (layout-independent virtual key codes) to `os::Key` for every key the
      enum names, with the layout-aware character from `charactersIgnoringModifiers` where `os::Key` is layout-aware (as the other backends define it);
      `KeyDown` with `repeat` from `isARepeat`, `KeyUp`; modifiers from `modifierFlags` (Command maps to the engine's `Super`/`Meta` as the input model
      defines); `flagsChanged:` produces modifier key events; Cmd-key presses reach the view (not swallowed by the menu unless they are menu shortcuts).
- [x] **Text:** the view implements `NSTextInputClient` minimally and calls `interpretKeyEvents:`; `insertText:` gives one `Text` event per scalar after
      its `KeyDown`; dead keys (Option-E then E gives `é`) produce none until committed; Ctrl- or Cmd-held presses and Enter, Tab, Backspace, Escape,
      Delete produce none; marked text (IME composition) is ignored, as on the other backends (documented).
- [x] **Pointer:** `mouseMoved:`/`*Dragged:` (from the tracking area with `NSTrackingMouseMoved` instead of `acceptsMouseMovedEvents`, after review: moves reach an unfocused window too) as `PointerMove` in window pixels with Y flipped to top-left origin; buttons
      left, right, other (`buttonNumber`); `NSTrackingArea` for `PointerEnter`/`PointerLeave`.
- [x] **Wheel:** `scrollingDeltaX/Y`; precise (trackpad) deltas become fractional notches by a documented divisor, line deltas whole notches; signs per the
      engine's "positive right and down" with the user's natural-scrolling setting left to the system (documented: macOS inverts before we see it).
- [x] **Focus:** `windowDidBecomeKey`/`windowDidResignKey`; on loss a `KeyUp` for every held key, then `FocusLost` (tested by losing focus with a key held).
- [x] **Resize** events in pixels; the stream starts with the settled size.
- [x] **`InputState`** agrees with the stream (the shared tests that compare them run on macOS).
- [x] **Tests:** the window-layer event tests that inject platform events run with synthesized `NSEvent`s (`keyEventWithType:...`,
      `mouseEventWithType:...` posted to the window) in the display group; the key table test covers every `os::Key`; `input_probe` checked by hand
      on this MacBook's keyboard and trackpad (US layout plus one dead-key layout; recorded).
- [x] **Documentation:** `BACKEND.md` `macos` section for the event stream as built; the guide's macOS section; `KNOWN-ISSUES.md` line "input event stream
      ... macOS waits for Phase 26" closed; `docgen lint` zero.
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` with the display group (quote).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: event ordering between `keyDown:` and `insertText:`, keys stuck after Cmd-Tab, the queue's
      newest-dropped rule.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** keys, text (with dead keys), pointer, wheel, focus and resize arrive on macOS in the stream with `InputState` agreeing, the window-layer event
tests pass with synthesized events in three flavors, and the event-stream deferral for macOS is closed.

## As built (2026-11-12, the Mac)

- **Files:** `engine/platform/macos/cocoa_keys.h/.cpp` (`KeyFromCocoa`, `ModifiersFromCocoa`, `IsCocoaModifierKey`, `CocoaModifierKeyDown`,
  `IsTypedScalar`, `CocoaWheelNotches`); the content view in `cocoa_window.mm` is the first responder and a minimal `NSTextInputClient`; the
  window's handlers keep the `Key` each key code was pressed as. `game/input_probe` builds on macOS, with a `input_probe_runs_cocoa` test.
- **Decisions:** Option is Alt and Command is Super; Option does not rename a key and does type (it is macOS's third level); Caps Lock is a press
  and a release at once (macOS reports only its toggle); a trackpad scroll is 10 points to the notch (`NSScrollView`'s line scroll); a key-up with
  Command held is given to its window by `PumpEvents` (AppKit keeps it otherwise: shown by a test failing without it); the activating click
  reaches the program; buttons stay held on a focus loss (macOS gives the release to the window the press went to).
- **Found by hand:** a held letter repeated its `KeyDown` without `Text`. macOS's press-and-hold (the accent menu) took the repeats. It is
  turned off for the program in the registration domain only (`ApplePressAndHoldEnabled` NO; the user's settings are not touched); the
  repeat then gave its text, checked by hand again.
- **Hand check (`input_probe`, by the user at the Mac, Spanish ISO layout):** letters, overlapping presses,
  Shift+A gives `A`, Ctrl+S no text, Option+S `∫` with the key still S, Cmd and Caps Lock, the Spanish dead accents (´ then a gives `á`,
  \` then a `à`, ´ then ñ gives `´` and `ñ`), `ñ` as text without a key name, repeat with text (after the fix), Cmd-Tab with J held (released
  before `FocusLost`), left and two-finger (right) clicks, two-finger scrolling in fractions both ways, enter and leave, moves while the window
  is not key (after the review change). **Not done by hand:** the US layout (only Spanish ISO was in use), a drag out past the window's edge
  and back (the one long drag stayed inside), the number pad (no keypad on the MacBook), the right-hand modifiers each on their own; `KNOWN-ISSUES.md`.
- **Runs (macOS):** `scripts/test-phase.sh --affected --flavor all --display`: `ran 4219 of 4598 tests (phases: all; flavors: all; display:
  included; crash tests: excluded) in 1462 s`. The one failure was the flaky `Throughput scales with participant count`. The screen locked partway
  through, so 129 window tests skipped; the 45 runs of the new input cases had passed by then. Rerun unlocked: `ctest -L display`, 105 of 105, no
  skips. The input tests alone: 20 of 20, several runs. Both unity modes build clean. `ctest -L docs` 3 of 3; `docs/reference/` regenerated, and
  `docgen lint` passes at zero.

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings.

1. **Enter and leave stopped during a drag** (the tracking area lacked `NSTrackingEnabledDuringMouseDrag`). Fixed.
2. **No moves for an unfocused window** (`acceptsMouseMovedEvents` works only for the key window), and a key window could be told of moves
   outside it. Fixed: `NSTrackingMouseMoved` on the always-active tracking area, `acceptsMouseMovedEvents` dropped, and a move outside with no
   button held is dropped; regression test (shown failing first). Checked by hand: moves arrive with the window behind another program's.
3. **Press-and-hold takes held letters.** The same bug the hand check found; fixed as above, with a test of the registration domain.
4. **Two keys of one name** (the digit row's 1 and the pad's, Return and the pad's Enter): releasing one cleared the `Key` in `InputState`
   while the other was held. Fixed (`StillHeld`); regression test shown failing first.
5. **The day's records** (`KNOWN-ISSUES.md`, `STATUS.md`, this file) were not written yet when the review ran. Done in this commit.
6. **`Close` released the keys twice.** Harmless; now once.
7. **A dead store** in the Caps Lock branch. Removed.
8. **`FirstScalar` decoded UTF-16 surrogates by hand** without checking the low half. Now `DecodeUtf8At`.
