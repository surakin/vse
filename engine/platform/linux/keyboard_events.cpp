#include "platform/linux/keyboard_events.h"

namespace vse::os
{

void KeyboardEvents::SetRepeatInfo(i32 rate_per_second, i32 delay_ms)
{
    rate_ = rate_per_second < 0 ? 0 : rate_per_second;
    delay_ms_ = delay_ms < 0 ? 0 : delay_ms;
}

void KeyboardEvents::Queue(InputEventType type, os::Key key, bool repeat, u64 now_ns)
{
    if (key == os::Key::Unknown)
    {
        return;  // a key the engine has no name for (a Cyrillic letter, say) still types its text, but is neither down
                 // nor up: nothing could release it
    }
    InputEvent e;
    e.type = type;
    e.key = key;
    e.repeat = repeat;
    e.modifiers = keyboard_->Modifiers();
    e.time_ns = now_ns;
    queue_->Push(e);
}

void KeyboardEvents::QueueText(u64 now_ns)
{
    if (text_length_ == 0)
    {
        return;
    }
    InputEvent e;
    e.type = InputEventType::Text;
    e.text_length = text_length_;
    for (u8 i = 0; i < text_length_; ++i)
    {
        e.text[i] = text_[i];
    }
    e.modifiers = keyboard_->Modifiers();
    e.time_ns = now_ns;
    queue_->Push(e);
}

void KeyboardEvents::Key(u32 evdev_code, bool pressed, u64 now_ns)
{
    if (evdev_code >= kMaxEvdevCode || keyboard_ == nullptr || !keyboard_->Valid())
    {
        return;
    }
    if (pressed)
    {
        const os::Key key = keyboard_->KeyForEvdevCode(evdev_code);
        const bool repeat = held_[evdev_code] != os::Key::Unknown;  // a press of a key already down
        held_[evdev_code] = key;
        text_length_ = static_cast<u8>(keyboard_->TextForEvdevCode(evdev_code, text_, sizeof(text_)));
        Queue(InputEventType::KeyDown, key, repeat, now_ns);
        QueueText(now_ns);
        if (keyboard_->KeyRepeats(evdev_code) && rate_ > 0)
        {
            repeating_ = evdev_code;
            next_repeat_ns_ = now_ns + static_cast<u64>(delay_ms_) * 1000000ull;
        }
        else
        {
            repeating_ = kNone;  // a non-repeating key pressed last takes the repeat over, as on a keyboard
        }
    }
    else
    {
        const os::Key key = held_[evdev_code];
        held_[evdev_code] = os::Key::Unknown;
        if (key != os::Key::Unknown)
        {
            Queue(InputEventType::KeyUp, key, false, now_ns);
        }
        if (repeating_ == evdev_code)
        {
            repeating_ = kNone;
        }
    }
}

void KeyboardEvents::FocusGained(u64 now_ns)
{
    InputEvent e;
    e.type = InputEventType::FocusGained;
    e.modifiers = keyboard_ != nullptr ? keyboard_->Modifiers() : 0;
    e.time_ns = now_ns;
    queue_->Push(e);
}

void KeyboardEvents::KeyAlreadyDown(u32 evdev_code, u64 now_ns)
{
    if (evdev_code >= kMaxEvdevCode || keyboard_ == nullptr || !keyboard_->Valid())
    {
        return;
    }
    const os::Key key = keyboard_->KeyForEvdevCode(evdev_code);
    held_[evdev_code] = key;
    Queue(InputEventType::KeyDown, key, false, now_ns);
}

void KeyboardEvents::FocusLost(u64 now_ns)
{
    for (u32 code = 0; code < kMaxEvdevCode; ++code)
    {
        if (held_[code] != os::Key::Unknown)
        {
            Queue(InputEventType::KeyUp, held_[code], false, now_ns);
            held_[code] = os::Key::Unknown;
        }
    }
    repeating_ = kNone;
    if (keyboard_ != nullptr)
    {
        keyboard_->ResetCompose();
    }
    InputEvent e;
    e.type = InputEventType::FocusLost;
    e.time_ns = now_ns;
    queue_->Push(e);
}

void KeyboardEvents::Forget()
{
    for (u32 code = 0; code < kMaxEvdevCode; ++code)
    {
        held_[code] = os::Key::Unknown;
    }
    repeating_ = kNone;
    text_length_ = 0;
}

void KeyboardEvents::Tick(u64 now_ns)
{
    if (repeating_ == kNone || rate_ <= 0 || now_ns < next_repeat_ns_)
    {
        return;
    }
    const u64 interval = 1000000000ull / static_cast<u64>(rate_);
    u32 sent = 0;
    while (now_ns >= next_repeat_ns_ && sent < kMaxRepeatsPerTick)
    {
        Queue(InputEventType::KeyDown, held_[repeating_], true, next_repeat_ns_);
        QueueText(next_repeat_ns_);
        next_repeat_ns_ += interval;
        ++sent;
    }
    if (now_ns >= next_repeat_ns_)
    {
        next_repeat_ns_ = now_ns + interval;  // a long stall: skip what was missed
    }
}

}  // namespace vse::os
