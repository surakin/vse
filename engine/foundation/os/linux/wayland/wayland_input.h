#pragma once

#include <wayland-client.h>

#include "os/input.h"
#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/xkb_keyboard.h"
#include "types.h"

// Phase 1 Day 21: wl_seat keyboard input. Listens for the seat's
// capabilities, gets a wl_keyboard when one is present, interprets the
// compositor-provided keymap through libxkbcommon (XkbKeyboard), and
// maintains a KeyboardState. Pointer input joins this class in Day 22.
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
    // capabilities and keymap have arrived. `keyboard_state` must outlive
    // this object. Returns false (logged) if the connection is invalid;
    // a seat with no keyboard is not a failure (HasKeyboard() == false).
    bool Init(WaylandConnection& connection, KeyboardState* keyboard_state);

    bool Valid() const { return seat_ != nullptr; }
    bool HasKeyboard() const { return keyboard_ != nullptr; }
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

    static const wl_seat_listener kSeatListener;
    static const wl_keyboard_listener kKeyboardListener;

    void ApplyKey(u32 evdev_code, bool pressed);
    void ReleaseAllKeys();

    // Highest evdev keycode tracked (KEY_MAX is 0x2ff).
    static constexpr u32 kMaxEvdevCode = 0x300;

    wl_seat* seat_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    KeyboardState* keyboard_state_ = nullptr;
    XkbKeyboard xkb_;
    // The engine Key each held evdev code was *pressed* as. A release
    // clears that Key rather than re-translating, since layout/modifier
    // state can change between press and release (e.g. releasing Shift
    // first on a layout whose unshifted sym differs) and re-translating
    // would leave the original Key stuck down.
    Key held_[kMaxEvdevCode] = {};
};

} // namespace engine_placeholder::os::wayland
