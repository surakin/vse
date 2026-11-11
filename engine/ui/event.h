#pragma once

#include "platform/input.h"
#include "types.h"
#include "ui/geometry.h"

//! The events a surface delivers to its widgets (Phase 15, Day 192): pointer, wheel, key, text, focus and tick, as plain data.

namespace engine_placeholder::ui {

/// The key enumeration of the platform layer; the toolkit has no second one.
using Key = os::Key;

/// The kind of a [[ui::Event]].
enum class EventType : u8 {
    None,         ///< No event.
    PointerMove,  ///< The pointer moved to `position`.
    PointerDown,  ///< A button went down at `position` (`button` is an [[os::MouseButtonBit]]).
    PointerUp,    ///< A button went up at `position`.
    PointerEnter, ///< The pointer entered the widget; sent by the surface, never bubbles.
    PointerLeave, ///< The pointer left the widget; sent by the surface, never bubbles. Posted to the surface it means the pointer left the surface: the hover is cleared.
    Wheel,        ///< The wheel turned by (`wheel_x`, `wheel_y`) notches at `position`.
    KeyDown,      ///< A key went down (or repeated); goes to the focus owner.
    KeyUp,        ///< A key went up; goes to the focus owner.
    Text,         ///< One Unicode scalar typed, as UTF-8 in `text`; goes to the focus owner.
    FocusGained,  ///< The widget got the keyboard focus; sent by the surface, never bubbles.
    FocusLost,    ///< The widget lost the keyboard focus; sent by the surface, never bubbles.
    WindowFocusGained, ///< Posted to the surface: the window it shows in got the focus (the focus owner gets `FocusGained` again).
    WindowFocusLost,   ///< Posted to the surface: the window lost the focus; capture is released, popups close, the modifier mirror resets and the focus owner gets `FocusLost`.
    RelativeMotion,    ///< While the pointer is locked: it moved by (`rel_x`, `rel_y`) device units; goes to the widget that asked for the lock (Day 201), never bubbles.
};

/// One event.
///
/// Posted to a surface with `position` in surface coordinates (logical units); a widget receives it with `position` in its own local space (its top-left corner is the origin).
/// A widget that handles an event sets `handled`; otherwise it bubbles to the parent, up to the root. Enter, leave and focus events go to one widget only.
///
/// - Thread-safety: thread-compatible plain data.
struct Event {
    EventType type = EventType::None; ///< What happened.
    bool handled = false;             ///< Set by a widget that dealt with the event; stops bubbling.
    bool repeat = false;              ///< `KeyDown`: an auto-repeat.
    u8 text_length = 0;               ///< `Text`: bytes used in `text`.
    Key key = Key::Unknown;           ///< `KeyDown`, `KeyUp`: the key.
    char text[4] = {0, 0, 0, 0};      ///< `Text`: the scalar as UTF-8.
    u32 modifiers = 0;                ///< Mask of [[os::ModifierBit]] values.
    u32 button = 0;                   ///< `PointerDown`, `PointerUp`: an [[os::MouseButtonBit]] value.
    Point position;                   ///< Pointer events: where, in logical units (surface or local space, see above).
    f32 wheel_x = 0;                  ///< `Wheel`: horizontal notches, positive to the right.
    f32 wheel_y = 0;                  ///< `Wheel`: vertical notches, positive downwards.
    f32 rel_x = 0;                    ///< `RelativeMotion`: horizontal movement in device units (not scaled by the surface scale), positive to the right.
    f32 rel_y = 0;                    ///< `RelativeMotion`: vertical movement in device units, positive downwards.
    f64 time = 0;                     ///< The surface clock, in seconds, when the event was delivered.
};

} // namespace engine_placeholder::ui
