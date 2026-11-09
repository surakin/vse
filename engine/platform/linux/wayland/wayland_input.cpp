#include "platform/linux/wayland/wayland_input.h"

#include "os/console.h"
#include "lang/initializer_list.h"
#include <sys/mman.h>
#include <unistd.h>

#include "log/log.h"
#include "os/os.h"

namespace engine_placeholder::os::wayland {

namespace {

// The locale whose Compose file decides dead keys and compose sequences: the first of LC_ALL, LC_CTYPE, LANG that is set, as the C library would choose; "C" otherwise.
const char* ComposeLocale() {
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const char* value = os::GetEnvironment(name);
        if (value != nullptr && value[0] != '\0') return value;
    }
    return "C";
}

u64 NowNs() { return os::MonotonicTimeNanos(); }

} // namespace

const wl_seat_listener WaylandInput::kSeatListener = {
    .capabilities = WaylandInput::HandleSeatCapabilities,
    .name = WaylandInput::HandleSeatName,
};

// Built by assignment: newer protocol versions add members (frame, axis_source, ...) this input does not handle, and a designated
// initializer that leaves them out is a warning.
const wl_pointer_listener WaylandInput::kPointerListener = [] {
    wl_pointer_listener listener{};
    listener.enter = WaylandInput::HandlePointerEnter;
    listener.leave = WaylandInput::HandlePointerLeave;
    listener.motion = WaylandInput::HandlePointerMotion;
    listener.button = WaylandInput::HandlePointerButton;
    listener.axis = WaylandInput::HandlePointerAxis;
    listener.frame = WaylandInput::HandlePointerFrame;
    listener.axis_source = WaylandInput::HandlePointerAxisSource;
    listener.axis_stop = WaylandInput::HandlePointerAxisStop;
    listener.axis_discrete = WaylandInput::HandlePointerAxisDiscrete;
    return listener;
}();

const wl_keyboard_listener WaylandInput::kKeyboardListener = {
    .keymap = WaylandInput::HandleKeyboardKeymap,
    .enter = WaylandInput::HandleKeyboardEnter,
    .leave = WaylandInput::HandleKeyboardLeave,
    .key = WaylandInput::HandleKeyboardKey,
    .modifiers = WaylandInput::HandleKeyboardModifiers,
    .repeat_info = WaylandInput::HandleKeyboardRepeatInfo,
};

bool WaylandInput::Init(WaylandConnection& connection, InputState* input_state) {
    if (!connection.Valid() || input_state == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandInput::Init called with an invalid connection/state");
        return false;
    }
    seat_ = connection.Seat();
    input_state_ = input_state;
    keyboard_events_.Init(&xkb_, &events_);
    cursor_.Init(&connection, &events_);
    clipboard_.Init(connection, &events_); // no data device manager: the clipboard stays unsupported
    wl_seat_add_listener(seat_, &kSeatListener, this);
    // First roundtrip delivers wl_seat.capabilities (which creates the
    // wl_keyboard/wl_pointer); the second delivers the keymap/modifiers
    // the new keyboard object sends on creation.
    wl_display_roundtrip(connection.Display());
    wl_display_roundtrip(connection.Display());
    return true;
}

WaylandInput::~WaylandInput() {
    Shutdown();
}

void WaylandInput::Shutdown() {
    if (keyboard_ != nullptr) {
        // wl_keyboard.release is a v3 request; the seat is bound at v1,
        // so plain proxy destroy is the only option here.
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
    clipboard_.Shutdown();
    cursor_.Shutdown(); // before the pointer: the shape device and relative pointer belong to it
    if (pointer_ != nullptr) {
        wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }
    // seat_ is owned by the WaylandConnection; just stop callbacks from
    // reaching a dead object (the seat can't have its listener removed,
    // so re-point it at nothing).
    if (seat_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(seat_), nullptr);
        seat_ = nullptr;
    }
    input_state_ = nullptr;
}

void WaylandInput::HandleSeatName(void* data, wl_seat* seat, const char* name) {
    (void)data;
    (void)seat;
    (void)name;
}

void WaylandInput::HandleSeatCapabilities(void* data, wl_seat* seat, u32 capabilities) {
    auto* self = static_cast<WaylandInput*>(data);
    if (self == nullptr) {
        return;
    }
    bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (has_keyboard && self->keyboard_ == nullptr) {
        self->keyboard_ = wl_seat_get_keyboard(seat);
        if (self->keyboard_ != nullptr) {
            wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
        }
    } else if (!has_keyboard && self->keyboard_ != nullptr) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
        self->ReleaseAllKeys();
    }

    bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (has_pointer && self->pointer_ == nullptr) {
        self->pointer_ = wl_seat_get_pointer(seat);
        if (self->pointer_ != nullptr) {
            wl_pointer_add_listener(self->pointer_, &kPointerListener, self);
            self->cursor_.AttachPointer(self->pointer_);
        }
    } else if (!has_pointer && self->pointer_ != nullptr) {
        self->cursor_.DetachPointer();
        wl_pointer_destroy(self->pointer_);
        self->pointer_ = nullptr;
        self->input_state_->pointer_inside = false;
        self->input_state_->mouse_buttons = 0;
    }
}

void WaylandInput::HandleKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd,
                                        u32 size) {
    (void)keyboard;
    auto* self = static_cast<WaylandInput*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: unsupported keymap format");
        ::close(fd);
        return;
    }
    void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: mmap of keymap fd failed");
        return;
    }
    if (self->xkb_.InitFromKeymapText(static_cast<const char*>(mapped), size)) {
        Log(LogPlatform, LogVerbosity::Info, "Wayland: keymap loaded");
        self->xkb_.InitCompose(ComposeLocale());
        // A new keymap invalidates any held-key translations.
        self->ReleaseAllKeys();
        self->keyboard_events_.Forget();
    }
    ::munmap(mapped, size);
}

void WaylandInput::HandleKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface,
                                       wl_array* keys) {
    (void)keyboard;
    (void)surface;
    auto* self = static_cast<WaylandInput*>(data);
    self->clipboard_.RecordSerial(serial);
    // `keys` lists the evdev codes already held when focus arrived.
    self->cursor_.OnFocus(true);
    self->keyboard_events_.FocusGained(NowNs());
    auto* begin = static_cast<u32*>(keys->data);
    auto* end = begin + keys->size / sizeof(u32);
    for (u32* it = begin; it < end; ++it) {
        self->ApplyKey(*it, true);
        self->keyboard_events_.KeyAlreadyDown(*it, NowNs());
    }
}

void WaylandInput::HandleKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface) {
    (void)keyboard;
    (void)serial;
    (void)surface;
    // Without focus we won't see the releases; don't leave keys stuck.
    auto* self = static_cast<WaylandInput*>(data);
    self->cursor_.OnFocus(false);
    self->keyboard_events_.FocusLost(NowNs());
    self->ReleaseAllKeys();
}

void WaylandInput::HandleKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key,
                                     u32 state) {
    (void)keyboard;
    (void)time;
    auto* self = static_cast<WaylandInput*>(data);
    self->clipboard_.RecordSerial(serial);
    const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    self->ApplyKey(key, pressed);
    self->keyboard_events_.Key(key, pressed, NowNs());
}

void WaylandInput::HandleKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial,
                                           u32 mods_depressed, u32 mods_latched, u32 mods_locked,
                                           u32 group) {
    (void)keyboard;
    (void)serial;
    auto* self = static_cast<WaylandInput*>(data);
    self->xkb_.UpdateMask(mods_depressed, mods_latched, mods_locked, group);
    self->input_state_->keyboard.modifiers = self->xkb_.Modifiers();
}

void WaylandInput::HandleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay) {
    // Only sent to v4+ keyboards; the compositor does not repeat for clients, so KeyboardEvents does.
    (void)keyboard;
    static_cast<WaylandInput*>(data)->keyboard_events_.SetRepeatInfo(rate, delay);
}

void WaylandInput::HandlePointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface,
                                      wl_fixed_t x, wl_fixed_t y) {
    (void)pointer;
    (void)serial;
    (void)surface;
    InputState* state = static_cast<WaylandInput*>(data)->input_state_;
    state->pointer_inside = true;
    static_cast<WaylandInput*>(data)->cursor_.OnPointerEnter(serial);
    state->mouse_x = wl_fixed_to_double(x);
    state->mouse_y = wl_fixed_to_double(y);
    static_cast<WaylandInput*>(data)->PushPointerEvent(InputEventType::PointerEnter, state->mouse_x, state->mouse_y);
}

void WaylandInput::HandlePointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    // Buttons are left alone: under the implicit grab a held button's
    // release is still delivered after the pointer leaves the surface.
    auto* self = static_cast<WaylandInput*>(data);
    self->input_state_->pointer_inside = false;
    self->PushPointerEvent(InputEventType::PointerLeave, 0.0, 0.0);
}

void WaylandInput::HandlePointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t x,
                                       wl_fixed_t y) {
    (void)pointer;
    (void)time;
    InputState* state = static_cast<WaylandInput*>(data)->input_state_;
    state->mouse_x = wl_fixed_to_double(x);
    state->mouse_y = wl_fixed_to_double(y);
    static_cast<WaylandInput*>(data)->PushPointerEvent(InputEventType::PointerMove, state->mouse_x, state->mouse_y);
}

void WaylandInput::HandlePointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time,
                                       u32 button, u32 state) {
    (void)pointer;
    (void)time;
    static_cast<WaylandInput*>(data)->clipboard_.RecordSerial(serial);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) static_cast<WaylandInput*>(data)->clipboard_.RecordButtonSerial(serial);
    // Linux evdev BTN_* codes (input-event-codes.h).
    constexpr u32 kBtnLeft = 0x110;
    constexpr u32 kBtnRight = 0x111;
    constexpr u32 kBtnMiddle = 0x112;
    constexpr u32 kBtnSide = 0x113;
    constexpr u32 kBtnExtra = 0x114;

    u32 bit = 0;
    switch (button) {
    case kBtnLeft: bit = kMouseLeft; break;
    case kBtnRight: bit = kMouseRight; break;
    case kBtnMiddle: bit = kMouseMiddle; break;
    case kBtnSide: bit = kMouseBack; break;
    case kBtnExtra: bit = kMouseForward; break;
    default: return;
    }
    auto* self = static_cast<WaylandInput*>(data);
    InputState* input = self->input_state_;
    InputEvent event;
    event.type = InputEventType::PointerButton;
    event.button = bit;
    event.down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    event.x = input->mouse_x;
    event.y = input->mouse_y;
    event.modifiers = self->xkb_.Modifiers();
    event.time_ns = NowNs();
    self->events_.Push(event);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        input->mouse_buttons |= bit;
    } else {
        input->mouse_buttons &= ~bit;
    }
}

void WaylandInput::HandlePointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis,
                                     wl_fixed_t value) {
    (void)pointer;
    (void)time;
    InputState* state = static_cast<WaylandInput*>(data)->input_state_;
    double amount = wl_fixed_to_double(value);
    // About 10 units per wheel notch (the same convention as InputState); the event reports notches.
    constexpr f64 kUnitsPerNotch = 10.0;
    InputEvent event;
    event.type = InputEventType::Wheel;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        state->scroll_dy += amount;
        event.y = amount / kUnitsPerNotch;
    } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        state->scroll_dx += amount;
        event.x = amount / kUnitsPerNotch;
    } else {
        return;
    }
    auto* self = static_cast<WaylandInput*>(data);
    event.modifiers = self->xkb_.Modifiers();
    event.time_ns = NowNs();
    self->events_.Push(event);
}

void WaylandInput::HandlePointerFrame(void*, wl_pointer*) {}
void WaylandInput::HandlePointerAxisSource(void*, wl_pointer*, u32) {}
void WaylandInput::HandlePointerAxisStop(void*, wl_pointer*, u32, u32) {}
void WaylandInput::HandlePointerAxisDiscrete(void*, wl_pointer*, u32, i32) {}

bool WaylandInput::ListenersComplete() {
    const wl_pointer_listener& p = kPointerListener;
    const wl_keyboard_listener& k = kKeyboardListener;
    const wl_seat_listener& s = kSeatListener;
    return p.enter && p.leave && p.motion && p.button && p.axis && p.frame && p.axis_source && p.axis_stop && p.axis_discrete && k.keymap && k.enter && k.leave &&
           k.key && k.modifiers && k.repeat_info && s.capabilities && s.name && WaylandCursor::ListenersComplete() && WaylandClipboard::ListenersComplete();
}

void WaylandInput::PushPointerEvent(InputEventType type, f64 x, f64 y) {
    InputEvent event;
    event.type = type;
    event.x = x;
    event.y = y;
    event.modifiers = xkb_.Modifiers();
    event.time_ns = NowNs();
    events_.Push(event);
}

void WaylandInput::Tick() {
    keyboard_events_.Tick(NowNs());
    clipboard_.Tick(NowNs());
}

void WaylandInput::ApplyKey(u32 evdev_code, bool pressed) {
    if (evdev_code >= kMaxEvdevCode || !xkb_.Valid()) {
        return;
    }
    if (pressed) {
        Key key = xkb_.KeyForEvdevCode(evdev_code);
        held_[evdev_code] = key;
        input_state_->keyboard.SetDown(key, true);
    } else {
        input_state_->keyboard.SetDown(held_[evdev_code], false);
        held_[evdev_code] = Key::Unknown;
    }
}

void WaylandInput::ReleaseAllKeys() {
    for (u32 i = 0; i < kMaxEvdevCode; ++i) {
        held_[i] = Key::Unknown;
    }
    if (input_state_ != nullptr) {
        input_state_->keyboard.ReleaseAll();
    }
}

} // namespace engine_placeholder::os::wayland
