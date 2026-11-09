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
    ClipboardData,    ///< A clipboard request finished: `request_id`, `clipboard_status`, `clipboard_kind`; fetch the bytes with `Window::TakeClipboardResult`.
    DragEnter,        ///< A drag from another program entered the window at (`x`, `y`) (the first position): `drag_formats`, `drag_actions` (the source's allowed actions).
    DragOver,         ///< The drag moved to (`x`, `y`); answer with `Window::SetDropAction`.
    DragLeave,        ///< The drag left the window without dropping.
    Drop,             ///< The drag was dropped at (`x`, `y`) with `drag_action`; read it with `Window::RequestDropData`, then call `Window::FinishDrop`.
    DropData,         ///< A `RequestDropData` finished: `request_id`, `clipboard_status`; fetch the bytes with `Window::TakeClipboardResult`.
    DragEnded,        ///< A drag this window started ended: `drag_action` is what the target did, `Nothing` if it was cancelled or refused.
    ClipboardChanged, ///< The owner of a selection changed (`clipboard_kind`): a Paste command may need enabling or disabling.
    RelativeMotion, ///< While the pointer is locked: it moved by (`x`, `y`) device units, unaccelerated where the server says so. Never sent unlocked.
};

/// One input event.
///
/// A plain value (no pointers, trivially copyable); which fields mean something depends on `type`, the others are zero.
///
/// Thread-safety: thread-compatible plain data.
struct InputEvent {
    // The small fields come first so that the struct stays within 64 bytes.
    InputEventType type = InputEventType::None; ///< What happened.
    bool repeat = false;                        ///< `KeyDown`: true for an auto-repeat of a key that is already down.
    bool down = false;                          ///< `PointerButton`: true for a press, false for a release.
    u8 text_length = 0;                         ///< `Text`: bytes used in `text` (1 to 4).
    Key key = Key::Unknown;                     ///< `KeyDown`, `KeyUp`: the layout-aware key.
    u8 clipboard_status = 0;                    ///< `ClipboardData`, `DropData`: a [[os::ClipboardStatus]] value.
    u8 clipboard_kind = 0;                      ///< `ClipboardData`, `ClipboardChanged`: a [[os::ClipboardKind]] value.
    u8 drag_formats = 0;                        ///< `DragEnter`, `Drop`: the formats on offer, bits `kDropText`, `kDropFiles`, `kDropCustom`.
    u8 drag_actions = 0;                        ///< `DragEnter`, `DragOver`, `Drop`: the actions the source allows, bits of [[os::DragAction]].
    u8 drag_action = 0;                         ///< `Drop`, `DragEnded`: the [[os::DragAction]] chosen.
    char text[4] = {0, 0, 0, 0};                ///< `Text`: the scalar as UTF-8, not NUL-terminated.
    u32 modifiers = 0;                          ///< Mask of [[os::ModifierBit]] values when the event happened.
    u32 button = 0;                             ///< `PointerButton`: a [[os::MouseButtonBit]] value.
    u32 width = 0;                              ///< `Resize`: the new width in pixels.
    u32 height = 0;                             ///< `Resize`: the new height in pixels.
    f64 x = 0.0;                                ///< `PointerMove`, `PointerEnter`, drag events: x in window pixels; `Wheel`: horizontal notches; `RelativeMotion`: dx.
    f64 y = 0.0;                                ///< `PointerMove`, `PointerEnter`, drag events: y in window pixels; `Wheel`: vertical notches; `RelativeMotion`: dy.
    u32 request_id = 0;                         ///< `ClipboardData`, `DropData`: the id the request returned.
    u64 time_ns = 0;                            ///< Monotonic time (`os::MonotonicTimeNanos`) when the backend queued the event.
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
