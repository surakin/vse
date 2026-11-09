#pragma once

#include "platform/input.h"
#include "types.h"

//! The input event stream: what a window backend reports in the order the server delivered it, beside the polled `InputState`.
//!
//! `InputState` answers "what is down now"; it cannot say that a key was pressed and released between two polls, carries no text, and has no key repeat
//! on Wayland. A UI needs all three, so each window also queues `InputEvent`s that the program drains with `Window::PollEvent` after `PumpEvents`.
//! Pure data, no display library: the header is part of the `platform_input` target.

namespace engine_placeholder::os {

/// The kind of an [[os::InputEvent]].
enum class InputEventType : u8 {
    None = 0,      ///< No event; the value of a default-constructed event.
    KeyDown,       ///< A key went down, or repeated (see `InputEvent::repeat`).
    KeyUp,         ///< A key went up.
    Text,          ///< One Unicode scalar the keyboard produced, as UTF-8 in `InputEvent::text`.
    PointerMove,   ///< The pointer moved to (`x`, `y`) in window pixels.
    PointerButton, ///< A mouse button went down or up (`button` is a [[os::MouseButtonBit]], `down` the new state).
    Wheel,         ///< The wheel turned by (`dx`, `dy`) notches; positive is right and down, as `InputState::scroll_dx/dy`.
    PointerEnter,  ///< The pointer entered the window at (`x`, `y`).
    PointerLeave,  ///< The pointer left the window.
    FocusGained,   ///< The window got keyboard focus.
    FocusLost,     ///< The window lost keyboard focus; held keys were released by `KeyUp` events just before.
    Resize,        ///< The window size changed to (`width`, `height`) pixels.
    RelativeMotion, ///< While the pointer is locked: it moved by (`x`, `y`) device units, unaccelerated where the server says so. Never sent unlocked.
};

/// One input event.
///
/// A plain value (no pointers, trivially copyable); which fields mean something depends on `type`, the others are zero.
///
/// Thread-safety: thread-compatible plain data.
struct InputEvent {
    InputEventType type = InputEventType::None; ///< What happened.
    bool repeat = false;                        ///< `KeyDown`: true for an auto-repeat of a key that is already down.
    bool down = false;                          ///< `PointerButton`: true for a press, false for a release.
    u8 text_length = 0;                         ///< `Text`: bytes used in `text` (1 to 4).
    Key key = Key::Unknown;                     ///< `KeyDown`, `KeyUp`: the layout-aware key.
    u32 modifiers = 0;                          ///< Mask of [[os::ModifierBit]] values when the event happened.
    u32 button = 0;                             ///< `PointerButton`: a [[os::MouseButtonBit]] value.
    u32 width = 0;                              ///< `Resize`: the new width in pixels.
    u32 height = 0;                             ///< `Resize`: the new height in pixels.
    f64 x = 0.0;                                ///< `PointerMove`, `PointerEnter`: x in window pixels; `Wheel`: horizontal notches; `RelativeMotion`: dx.
    f64 y = 0.0;                                ///< `PointerMove`, `PointerEnter`: y in window pixels; `Wheel`: vertical notches; `RelativeMotion`: dy.
    u64 time_ns = 0;                            ///< Monotonic time (`os::MonotonicTimeNanos`) when the backend queued the event.
    char text[4] = {0, 0, 0, 0};                ///< `Text`: the scalar as UTF-8, not NUL-terminated.
};

static_assert(sizeof(InputEvent) <= 64, "an input event stays small: the queue holds a thousand of them");

/// A bounded first-in first-out queue of [[os::InputEvent]] values with a counter of the events it had to drop.
///
/// When the queue is full the **newest** event is dropped and counted, never an older one, so what a program does read stays in order and a stalled program loses events
/// visibly instead of growing memory.
///
/// Thread-safety: thread-compatible; the backend pushes and the owner pops, both on the window's thread.
/// Ownership: owns its storage inline.
class InputEventQueue {
public:
    /// The most events the queue holds.
    static constexpr usize kCapacity = 1024;

    /// Appends `event` if there is room.
    /// Param event: the event to queue.
    /// Returns: true if queued.
    /// Fails: returns false and counts a drop when the queue is full.
    bool Push(const InputEvent& event) {
        if (count_ == kCapacity) {
            ++dropped_;
            return false;
        }
        events_[(head_ + count_) % kCapacity] = event;
        ++count_;
        return true;
    }

    /// Removes the oldest event into `out`.
    /// Param out: receives the event; untouched when the queue is empty.
    /// Returns: true if an event was removed.
    bool Pop(InputEvent* out) {
        if (count_ == 0) {
            return false;
        }
        *out = events_[head_];
        head_ = (head_ + 1) % kCapacity;
        --count_;
        return true;
    }

    /// Returns the number of queued events.
    usize Size() const { return count_; }

    /// Returns how many events were dropped because the queue was full, since construction.
    u64 Dropped() const { return dropped_; }

    /// Removes every queued event; the drop counter is kept.
    void Clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    InputEvent events_[kCapacity];
    usize head_ = 0;
    usize count_ = 0;
    u64 dropped_ = 0;
};

} // namespace engine_placeholder::os
