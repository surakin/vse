#pragma once

#include "platform/input_event.h"
#include "platform/linux/xkb_keyboard.h"
#include "types.h"

//! Keyboard events for the Linux backends that get raw evdev key transitions (Wayland): key down and up, text, and client-side key repeat.

namespace engine_placeholder::os {

/// Turns evdev key presses and releases into [[os::InputEvent]]s: `KeyDown`/`KeyUp`, the `Text` a press produces, and the auto-repeat the Wayland compositor does not send.
///
/// Independent of Wayland types and of any clock: the caller passes the time, so a test drives repeat with a fake clock. Only the most recently pressed repeating key repeats, as
/// keyboards do; a modifier never does. A key that is held when focus is lost is released with a `KeyUp` before `FocusLost`.
///
/// - Thread-safety: not-thread-safe; owner-thread only.
/// - Ownership: borrows the keyboard and the queue, which must outlive it.
class KeyboardEvents {
public:
    /// The most repeats one [[os::KeyboardEvents::Tick]] queues, so a program that stalled for seconds is not flooded when it wakes.
    static constexpr u32 kMaxRepeatsPerTick = 4;

    /// Constructs an unattached object; call [[os::KeyboardEvents::Init]].
    KeyboardEvents() = default;

    /// Attaches the keyboard and the queue.
    /// - Param keyboard: the keymap and state; must outlive this object.
    /// - Param queue: where events go; must outlive this object.
    void Init(XkbKeyboard* keyboard, InputEventQueue* queue) {
        keyboard_ = keyboard;
        queue_ = queue;
    }

    /// Sets the repeat parameters the compositor announced (`wl_keyboard.repeat_info`).
    /// - Param rate_per_second: repeats per second after the delay; 0 turns repeat off.
    /// - Param delay_ms: milliseconds a key is held before the first repeat.
    void SetRepeatInfo(i32 rate_per_second, i32 delay_ms);

    /// Reports a key press or release.
    ///
    /// The modifier state of the keyboard must already include the key's own modifier effect (a Wayland `modifiers` event arrives before the `key` event it belongs to... for a
    /// modifier key itself the order is not guaranteed, which does not matter: modifier keys produce no text).
    /// - Param evdev_code: the evdev keycode.
    /// - Param pressed: true for a press.
    /// - Param now_ns: the monotonic time of the event.
    void Key(u32 evdev_code, bool pressed, u64 now_ns);

    /// Reports that the window got keyboard focus. Call [[os::KeyboardEvents::KeyAlreadyDown]] afterwards for each key the compositor says is held.
    /// - Param now_ns: the monotonic time of the event.
    void FocusGained(u64 now_ns);

    /// Reports a key that was already down when focus arrived: a `KeyDown` without text, and it does not repeat.
    /// - Param evdev_code: the evdev keycode.
    /// - Param now_ns: the monotonic time of the event.
    void KeyAlreadyDown(u32 evdev_code, u64 now_ns);

    /// Reports that the window lost keyboard focus: queues a `KeyUp` for every held key, then `FocusLost`, and stops repeat and any compose sequence.
    void FocusLost(u64 now_ns);

    /// Forgets every held key without queueing anything (a new keymap makes the old translations meaningless).
    void Forget();

    /// Queues the repeats that are due at `now_ns`.
    void Tick(u64 now_ns);

    /// Returns whether a key is being repeated.
    bool Repeating() const { return repeating_ != kNone; }

private:
    static constexpr u32 kMaxEvdevCode = 0x300;
    static constexpr u32 kNone = 0xFFFFFFFFu;

    void Queue(InputEventType type, os::Key key, bool repeat, u64 now_ns);
    void QueueText(u64 now_ns);

    XkbKeyboard* keyboard_ = nullptr;
    InputEventQueue* queue_ = nullptr;
    os::Key held_[kMaxEvdevCode] = {};
    i32 rate_ = 25;
    i32 delay_ms_ = 600;
    u32 repeating_ = kNone;
    u64 next_repeat_ns_ = 0;
    char text_[4] = {0, 0, 0, 0};
    u8 text_length_ = 0;
};

} // namespace engine_placeholder::os
