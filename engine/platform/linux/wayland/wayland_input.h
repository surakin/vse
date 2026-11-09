#pragma once

#include <wayland-client.h>

#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/linux/keyboard_events.h"
#include "platform/linux/wayland/wayland_cursor.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "platform/linux/xkb_keyboard.h"
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

    /// The input events queued by the listeners; the window pops them and pushes its own (resize) here.
    InputEventQueue& Events() { return events_; }
    /// Read-only view of the queue.
    const InputEventQueue& Events() const { return events_; }

    /// The cursor and pointer lock of the seat's pointer.
    WaylandCursor& Cursor() { return cursor_; }
    /// Read-only view of the cursor state.
    const WaylandCursor& Cursor() const { return cursor_; }

    /// Queues the key repeats that are due (`wl_keyboard` repeat is the client's job); call once per pump, after dispatching.
    void Tick();

    /// Returns whether the handler is attached to a seat.
    bool Valid() const { return seat_ != nullptr; }
    /// Returns whether the seat offered a keyboard.
    bool HasKeyboard() const { return keyboard_ != nullptr; }
    /// Returns whether every event of the `wl_pointer` and `wl_keyboard` versions the seat is bound at has a listener function.
    ///
    /// libwayland aborts the process when a compositor sends an event whose listener slot is null, so a listener table that falls behind the bound seat
    /// version is a crash; a test checks this.
    static bool ListenersComplete();
    /// Returns whether the seat offered a pointer.
    bool HasPointer() const { return pointer_ != nullptr; }
    /// Returns whether a keymap has been received and compiled.
    bool HasKeymap() const { return xkb_.Valid(); }

private:
    static void HandleSeatCapabilities(void* data, wl_seat* seat, u32 capabilities);
    static void HandleSeatName(void* data, wl_seat* seat, const char* name);
    static void HandleKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size);
    static void HandleKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface,
                                    wl_array* keys);
    static void HandleKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface);
    static void HandleKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key,
                                  u32 state);
    static void HandleKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 mods_depressed,
                                        u32 mods_latched, u32 mods_locked, u32 group);
    static void HandleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay);

    static void HandlePointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface,
                                   wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface);
    static void HandlePointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t x, wl_fixed_t y);
    static void HandlePointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time,
                                    u32 button, u32 state);
    static void HandlePointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value);
    // wl_pointer v5 events we do not use. libwayland aborts the process on an event whose listener slot is null, so each needs a function.
    static void HandlePointerFrame(void* data, wl_pointer* pointer);
    static void HandlePointerAxisSource(void* data, wl_pointer* pointer, u32 axis_source);
    static void HandlePointerAxisStop(void* data, wl_pointer* pointer, u32 time, u32 axis);
    static void HandlePointerAxisDiscrete(void* data, wl_pointer* pointer, u32 axis, i32 discrete);
    void PushPointerEvent(InputEventType type, f64 x, f64 y);

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
    KeyboardEvents keyboard_events_;
    WaylandCursor cursor_;
    InputEventQueue events_;
    // The engine Key each held evdev code was *pressed* as. A release
    // clears that Key rather than re-translating, since layout/modifier
    // state can change between press and release (e.g. releasing Shift
    // first on a layout whose unshifted sym differs) and re-translating
    // would leave the original Key stuck down.
    Key held_[kMaxEvdevCode] = {};
};

} // namespace engine_placeholder::os::wayland
