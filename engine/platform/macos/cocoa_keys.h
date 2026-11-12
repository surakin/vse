#pragma once

//! macOS key events to [[os::Key]] (Phase 26 Day 312), with the meaning the other backends give keys: layout-aware.
//!
//! A key that types a character is named by the character the active layout gives it with Shift and Caps Lock only
//! (`charactersIgnoringModifiers`), as XKB names it by its keysym and Windows by its layout's character: on a French
//! AZERTY layout the key in the Q position is `Key::A`, Shift+1 on a US layout is `!` and so `Unknown`, and a dead key
//! (no character) is `Unknown`. Option and Command do not rename a key: Option+E is still `Key::E` (Option is the
//! engine's Alt; on macOS it is also the layout's third level, so its characters still arrive as `Text`). Every other
//! key is named by its virtual key code (`keyCode`, the position on the keyboard): the left and right modifiers apart,
//! the number pad's digits as the digits and its Enter as Enter (as XKB's `KP_0`..`KP_9` and `KP_Enter`), Help (the
//! Insert position on an extended keyboard) as Insert, the forward Delete as Delete and the Delete above Return as
//! Backspace. Keys the enum has no name for (fn, F13-F20, the number pad's operators, Clear and decimal point, the
//! volume keys, the JIS Eisu and Kana keys) are `Unknown`. Plain C++, no AppKit: the codes and flags are numbers, so
//! this is tested without a window.
#include "platform/input.h"
#include "types.h"

namespace engine_placeholder::os::cocoa
{

/// Key codes are below this (they are 7-bit on every Apple keyboard).
inline constexpr u32 kCocoaKeyCodeCount = 128;

/// Returns the [[os::Key]] a key event names; `Key::Unknown` for keys the enum does not have.
///
/// - Param key_code: the event's `keyCode`.
/// - Param character: the first Unicode scalar of `charactersIgnoringModifiers`, or 0 for none (a dead key).
/// - Returns: the key.
/// - Fails: never; an unknown code is `Unknown`.
Key KeyFromCocoa(u32 key_code, u32 character);

/// Returns the [[os::ModifierBit]] mask of an event's `modifierFlags`: Shift, Control as Ctrl, Option as Alt, Command
/// as Super, Caps Lock. A Mac keyboard has no Num Lock, so `kModNumLock` is never set.
///
/// - Param modifier_flags: the event's `modifierFlags`.
/// - Returns: the mask.
/// - Fails: never.
u32 ModifiersFromCocoa(u64 modifier_flags);

/// True for the codes `flagsChanged:` reports (the Shift, Control, Option and Command keys of both sides, Caps Lock,
/// fn), which never come as `keyDown:`/`keyUp:`.
///
/// - Param key_code: the event's `keyCode`.
/// - Returns: whether the key is a modifier key.
/// - Fails: never.
bool IsCocoaModifierKey(u32 key_code);

/// Whether a modifier key named by a `flagsChanged:` event is down after it: from the device-dependent bit of that side
/// in `modifier_flags` where the event carries one, else from the modifier's own flag (an event made without device
/// bits, as `NSEvent` constructors make them). Caps Lock reports its toggle state, not the key.
///
/// - Param key_code: the event's `keyCode` (a code for which [[os::cocoa::IsCocoaModifierKey]] is true).
/// - Param modifier_flags: the event's `modifierFlags`.
/// - Returns: true if that key is held after the event.
/// - Fails: false for a code that is not a modifier key.
bool CocoaModifierKeyDown(u32 key_code, u64 modifier_flags);

/// True for a Unicode scalar that is typed text: not a control character (C0, DEL, C1) and not one of the private-use
/// scalars AppKit gives the function and arrow keys (U+F700-U+F8FF).
///
/// - Param scalar: the scalar.
/// - Returns: whether it is text.
/// - Fails: never.
bool IsTypedScalar(u32 scalar);

/// How many points of a precise (trackpad, Magic Mouse) scroll make one wheel notch: AppKit's own line scroll
/// (`NSScrollView.verticalLineScroll` is 10 points by default), which is also the Linux backends' units per notch.
inline constexpr f64 kCocoaPointsPerNotch = 10.0;

/// A scroll event in the engine's notches.
///
/// - Thread-safety: thread-compatible plain data.
struct CocoaWheel
{
    f64 dx = 0.0;  ///< Notches, positive right.
    f64 dy = 0.0;  ///< Notches, positive down.
};

/// Converts `scrollingDeltaX`/`scrollingDeltaY` to notches: precise deltas (points) divided by
/// [[os::cocoa::kCocoaPointsPerNotch]], line deltas as they are (AppKit's acceleration may make them fractional). macOS
/// gives positive deltas for scrolling up and left, so both signs turn; the user's "natural scrolling" setting is
/// applied by the system before the event reaches the program, as it is for every other macOS program.
///
/// - Param delta_x: `scrollingDeltaX`.
/// - Param delta_y: `scrollingDeltaY`.
/// - Param precise: `hasPreciseScrollingDeltas`.
/// - Returns: the notches.
/// - Fails: never.
CocoaWheel CocoaWheelNotches(f64 delta_x, f64 delta_y, bool precise);

}  // namespace engine_placeholder::os::cocoa
