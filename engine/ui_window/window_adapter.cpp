#include "ui_window/window_adapter.h"

#include "platform/input_event.h"

namespace engine_placeholder::ui {

usize WindowAdapter::PumpInto(os::Window& window, Surface& surface) {
    usize posted = 0;
    const f32 scale = surface.Scale();
    const f64 inverse = 1.0 / static_cast<f64>(scale);
    os::InputEvent in;
    while (window.PollEvent(&in)) {
        Event e;
        e.modifiers = in.modifiers;
        switch (in.type) {
            case os::InputEventType::KeyDown:
            case os::InputEventType::KeyUp:
                e.type = in.type == os::InputEventType::KeyDown ? EventType::KeyDown : EventType::KeyUp;
                e.key = in.key;
                e.repeat = in.repeat;
                break;
            case os::InputEventType::Text:
                e.type = EventType::Text;
                e.text_length = in.text_length;
                for (u8 i = 0; i < 4; ++i) e.text[i] = in.text[i];
                break;
            case os::InputEventType::PointerMove:
            case os::InputEventType::PointerEnter:
                e.type = EventType::PointerMove;
                e.position = Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse));
                pointer_ = e.position;
                break;
            case os::InputEventType::PointerButton:
                e.type = in.down ? EventType::PointerDown : EventType::PointerUp;
                e.button = in.button;
                e.position = Point(static_cast<f32>(in.x * inverse), static_cast<f32>(in.y * inverse));
                pointer_ = e.position;
                break;
            case os::InputEventType::Wheel:
                e.type = EventType::Wheel;
                e.position = pointer_; // a window wheel event carries no position: it turns where the pointer last was
                e.wheel_x = static_cast<f32>(in.x);
                e.wheel_y = static_cast<f32>(in.y);
                break;
            case os::InputEventType::PointerLeave:
                e.type = EventType::PointerLeave;
                break;
            case os::InputEventType::FocusGained:
                e.type = EventType::WindowFocusGained;
                break;
            case os::InputEventType::FocusLost:
                e.type = EventType::WindowFocusLost;
                break;
            case os::InputEventType::Resize:
                if (in.width > 0 && in.height > 0) surface.Resize(static_cast<f32>(in.width * inverse), static_cast<f32>(in.height * inverse), scale);
                continue;
            default:
                continue;
        }
        surface.Post(e);
        ++posted;
    }
    return posted;
}

bool WindowAdapter::UpdateCursor(os::Window& window, const Surface& surface) {
    const os::CursorShape wanted = surface.RequestedCursor();
    if (wanted == last_cursor_) return false;
    if (!window.SetCursor(wanted)) return true;
    last_cursor_ = wanted;
    ++cursor_changes_;
    return true;
}

} // namespace engine_placeholder::ui
