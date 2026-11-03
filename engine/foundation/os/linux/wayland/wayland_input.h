#pragma once

#include <wayland-client.h>

#include "os/input.h"
#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/xkb_keyboard.h"
#include "types.h"

// Phase 1 Days 21-22: wl_seat keyboard and pointer input. Listens for the seat's
// capabilities, gets a wl_keyboard when one is present, interprets the
// compositor-provided keymap through libxkbcommon (XkbKeyboard), and
// maintains the keyboard half of an InputState; Day 22 adds the
// wl_pointer half (position, button mask, scroll).
//
// Not movable/copyable: the wl_seat/wl_keyboard listeners capture `this`
// at registration time. Use Init() on an in-place object.
namespace engine_placeholder::os::wayland {

class WaylandInput {
public:
    WaylandInput() = default;
    ~WaylandInput();

    WaylandInput(const WaylandInput&) = delete;
    WaylandInput& operator=(const WaylandInput&) = delete;
    WaylandInput(WaylandInput&&) = delete;
    WaylandInput& operator=(WaylandInput&&) = delete;

    // Hooks `connection`'s seat and blocks (two roundtrips) until the
    // capabilities and keymap have arrived. `input_state` must outlive
    // this object. Returns false (logged) if the connection is invalid;
    // a seat lacking a keyboard or pointer is not a failure
    // (HasKeyboard()/HasPointer() == false).
    bool Init(WaylandConnection& connection, InputState* input_state);

    bool Valid() const { return seat_ != nullptr; }
    bool HasKeyboard() const { return keyboard_ != nullptr; }
    bool HasPointer() const { return pointer_ != nullptr; }
    // True once a keymap has been received and compiled.
    bool HasKeymap() const { return xkb_.Valid(); }

private:
    static void HandleSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void HandleSeatName(void* data, wl_seat* seat, const char* name);
    static void HandleKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
    static void HandleKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
                                    wl_array* keys);
    static void HandleKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void HandleKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key,
                                  uint32_t state);
    static void HandleKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed,
                                        uint32_t mods_latched, uint32_t mods_locked, uint32_t group);
    static void HandleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);

    static void HandlePointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
                                   wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void HandlePointerMotion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time,
                                    uint32_t button, uint32_t state);
    static void HandlePointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);

    static const wl_pointer_listener kPointerListener;
    static const wl_seat_listener kSeatListener;
    static const wl_keyboard_listener kKeyboardListener;

    void ApplyKey(u32 evdev_code, bool pressed);
    void ReleaseAllKeys();

    // Highest evdev keycode tracked (KEY_MAX is 0x2ff).
    static constexpr u32 kMaxEvdevCode = 0x300;

    wl_seat* seat_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    InputState* input_state_ = nullptr;
    XkbKeyboard xkb_;
    // The engine Key each held evdev code was *pressed* as. A release
    // clears that Key rather than re-translating, since layout/modifier
    // state can change between press and release (e.g. releasing Shift
    // first on a layout whose unshifted sym differs) and re-translating
    // would leave the original Key stuck down.
    Key held_[kMaxEvdevCode] = {};
};

} // namespace engine_placeholder::os::wayland
