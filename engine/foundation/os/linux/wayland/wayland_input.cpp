#include "os/linux/wayland/wayland_input.h"

#include <sys/mman.h>
#include <unistd.h>

#include "log/log.h"

namespace engine_placeholder::os::wayland {

const wl_seat_listener WaylandInput::kSeatListener = {
    .capabilities = WaylandInput::HandleSeatCapabilities,
    .name = WaylandInput::HandleSeatName,
};

const wl_keyboard_listener WaylandInput::kKeyboardListener = {
    .keymap = WaylandInput::HandleKeyboardKeymap,
    .enter = WaylandInput::HandleKeyboardEnter,
    .leave = WaylandInput::HandleKeyboardLeave,
    .key = WaylandInput::HandleKeyboardKey,
    .modifiers = WaylandInput::HandleKeyboardModifiers,
    .repeat_info = WaylandInput::HandleKeyboardRepeatInfo,
};

bool WaylandInput::Init(WaylandConnection& connection, KeyboardState* keyboard_state) {
    if (!connection.Valid() || keyboard_state == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandInput::Init called with an invalid connection/state");
        return false;
    }
    seat_ = connection.Seat();
    keyboard_state_ = keyboard_state;
    wl_seat_add_listener(seat_, &kSeatListener, this);
    // First roundtrip delivers wl_seat.capabilities (which creates the
    // wl_keyboard); the second delivers the keymap/modifiers the new
    // keyboard object sends on creation.
    wl_display_roundtrip(connection.Display());
    wl_display_roundtrip(connection.Display());
    return true;
}

WaylandInput::~WaylandInput() {
    if (keyboard_ != nullptr) {
        // wl_keyboard.release is a v3 request; the seat is bound at v1,
        // so plain proxy destroy is the only option here.
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
    // seat_ is owned by the WaylandConnection; just stop callbacks from
    // reaching a dead object (the seat can't have its listener removed,
    // so re-point it at nothing).
    if (seat_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(seat_), nullptr);
        seat_ = nullptr;
    }
}

void WaylandInput::HandleSeatName(void* data, wl_seat* seat, const char* name) {
    (void)data;
    (void)seat;
    (void)name;
}

void WaylandInput::HandleSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
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
}

void WaylandInput::HandleKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd,
                                        uint32_t size) {
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
        // A new keymap invalidates any held-key translations.
        self->ReleaseAllKeys();
    }
    ::munmap(mapped, size);
}

void WaylandInput::HandleKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
                                       wl_array* keys) {
    (void)keyboard;
    (void)serial;
    (void)surface;
    auto* self = static_cast<WaylandInput*>(data);
    // `keys` lists the evdev codes already held when focus arrived.
    auto* begin = static_cast<uint32_t*>(keys->data);
    auto* end = begin + keys->size / sizeof(uint32_t);
    for (uint32_t* it = begin; it < end; ++it) {
        self->ApplyKey(*it, true);
    }
}

void WaylandInput::HandleKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface) {
    (void)keyboard;
    (void)serial;
    (void)surface;
    // Without focus we won't see the releases; don't leave keys stuck.
    static_cast<WaylandInput*>(data)->ReleaseAllKeys();
}

void WaylandInput::HandleKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key,
                                     uint32_t state) {
    (void)keyboard;
    (void)serial;
    (void)time;
    static_cast<WaylandInput*>(data)->ApplyKey(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

void WaylandInput::HandleKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial,
                                           uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
                                           uint32_t group) {
    (void)keyboard;
    (void)serial;
    auto* self = static_cast<WaylandInput*>(data);
    self->xkb_.UpdateMask(mods_depressed, mods_latched, mods_locked, group);
    self->keyboard_state_->modifiers = self->xkb_.Modifiers();
}

void WaylandInput::HandleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay) {
    // Only sent to v4+ keyboards; key repeat is the input layer's job.
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

void WaylandInput::ApplyKey(u32 evdev_code, bool pressed) {
    if (evdev_code >= kMaxEvdevCode || !xkb_.Valid()) {
        return;
    }
    if (pressed) {
        Key key = xkb_.KeyForEvdevCode(evdev_code);
        held_[evdev_code] = key;
        keyboard_state_->SetDown(key, true);
    } else {
        keyboard_state_->SetDown(held_[evdev_code], false);
        held_[evdev_code] = Key::Unknown;
    }
}

void WaylandInput::ReleaseAllKeys() {
    for (u32 i = 0; i < kMaxEvdevCode; ++i) {
        held_[i] = Key::Unknown;
    }
    if (keyboard_state_ != nullptr) {
        keyboard_state_->ReleaseAll();
    }
}

} // namespace engine_placeholder::os::wayland
