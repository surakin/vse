#pragma once

#include <wayland-client.h>

#include "os/input.h"
#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/xkb_keyboard.h"
#include "types.h"

//! Wayland seat input: keyboard and pointer events from a `wl_seat` into an `InputState`.

namespace engine_placeholder::os::wayland {

/// Listens to a `wl_seat` and maintains the keyboard and pointer state of an `InputState`.
///
/// Gets a `wl_keyboard` and `wl_pointer` when the seat has them, interprets the compositor
/// keymap through [[os::XkbKeyboard]], and tracks pointer position, button mask and scroll.
/// Not movable or copyable, because the listeners capture `this`; call `Init` on an in-place object.
///
/// Thread-safety: owner-thread-only. Listener callbacks run on the thread that dispatches the connection's events, which must be the one that calls `Init` and `Shutdown`.
/// Ownership: owns the keyboard and pointer proxies; the seat belongs to the connection and
///   the `InputState` is borrowed from the caller.
class WaylandInput {
public:
    /// Constructs an uninitialized input handler.
    WaylandInput() = default;

    /// Calls [[os::wayland::WaylandInput::Shutdown]].
    ~WaylandInput();

    WaylandInput(const WaylandInput&) = delete;
    WaylandInput& operator=(const WaylandInput&) = delete;
    WaylandInput(WaylandInput&&) = delete;
    WaylandInput& operator=(WaylandInput&&) = delete;

    /// Hooks the connection's seat and blocks, with two roundtrips, until the capabilities and keymap have arrived.
    ///
    /// A seat lacking a keyboard or pointer is not a failure; check
    /// [[os::wayland::WaylandInput::HasKeyboard]] and [[os::wayland::WaylandInput::HasPointer]].
    /// Param connection: connection whose seat to listen to; must stay valid until `Shutdown`.
    /// Param input_state: state to fill; must outlive this object.
    /// Returns: `true` if the seat was hooked.
    /// Fails: returns `false` (logged) if the connection is invalid.
    bool Init(WaylandConnection& connection, InputState* input_state);

    /// Destroys the keyboard and pointer proxies and detaches from the seat.
    ///
    /// Must run while the display is still connected. Idempotent. A `wl_seat` accepts
    /// only one listener, so `Init` cannot be repeated on the same seat; reconnect for a fresh one.
    void Shutdown();

    /// Returns whether the handler is attached to a seat.
    bool Valid() const { return seat_ != nullptr; }
    /// Returns whether the seat offered a keyboard.
    bool HasKeyboard() const { return keyboard_ != nullptr; }
    /// Returns whether the seat offered a pointer.
    bool HasPointer() const { return pointer_ != nullptr; }
    /// Returns whether a keymap has been received and compiled.
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
