#pragma once

#include "platform/linux/wayland/wayland_connection.h"
#include "platform/linux/wayland/wayland_input.h"
#include "platform/linux/wayland/wayland_window.h"
#include "platform/window.h"

//! Wayland platform window: the Wayland backend packaged as an `os::Window`.

namespace engine_placeholder::os::wayland {

/// Wayland backend as an [[os::Window]]: owns the connection, xdg toplevel window and seat input together.
///
/// Not movable or copyable, because the input listeners capture addresses.
///
/// Thread-safety: owner-thread-only. Open, pump and close on one thread; listener callbacks run inside `PumpEvents` on that thread.
/// Ownership: owns the connection, window and input; the `InputState` passed to
///   `Open` is borrowed and must outlive this object.
class WaylandPlatformWindow final : public Window {
public:
    /// Constructs a closed window; call [[os::wayland::WaylandPlatformWindow::Open]] to connect.
    WaylandPlatformWindow() = default;

    /// Connects to the compositor, maps a window and hooks input into `input`.
    /// Param title: window title.
    /// Param width: requested width in pixels; a hint the compositor may override with its suggested size.
    /// Param height: requested height in pixels; a hint the compositor may override.
    /// Param input: receives keyboard and pointer state; must outlive this object.
    /// Returns: `true` if the window is open.
    /// Fails: returns `false` (logged) on any failure, leaving the object invalid with nothing open.
    bool Open(StringView title, u32 width, u32 height, InputState* input);

    /// Tears everything down in the safe order: input proxies, window, then the connection.
    ///
    /// Idempotent. `Open` calls it on failure, so a failed open leaves no half-open
    /// connection or live listeners pointing at the caller's `InputState`.
    void Close();

    /// Closes the window; see [[os::wayland::WaylandPlatformWindow::Close]].
    ~WaylandPlatformWindow() override { Close(); }

    /// Returns whether the window is open and the compositor connection is still alive.
    bool Valid() const override { return window_.Valid() && !connection_lost_; }
    /// Reads and dispatches pending compositor events without blocking, then commits any pending resize.
    ///
    /// Listener callbacks run inside this call. A failed read, flush or dispatch, or a
    /// protocol error, marks the connection lost; later calls then do nothing.
    void PumpEvents() override;

    /// Removes the oldest queued input event; see [[os::Window::PollEvent]].
    bool PollEvent(InputEvent* out) override { return input_.Events().Pop(out); }
    /// Returns the number of events dropped because the queue was full.
    u64 DroppedEvents() const override { return input_.Events().Dropped(); }

    /// Returns true when the compositor offers `wp_cursor_shape_v1` and the seat has a pointer.
    bool SupportsCursorShape() const override { return input_.Cursor().SupportsShape(); }
    /// Shows a standard cursor shape (set with the serial of the last pointer enter, and again on every enter); see [[os::Window::SetCursor]].
    bool SetCursor(CursorShape shape) override { return Valid() && input_.Cursor().SetShape(shape); }
    /// Returns true when the seat has a pointer: the image is a `wl_shm` buffer on a cursor surface.
    bool SupportsCursorImage() const override { return input_.Cursor().SupportsImage(); }
    /// Shows a custom cursor image, scaled by its `scale`; see [[os::Window::SetCursorImage]].
    bool SetCursorImage(const CursorImage& image) override { return Valid() && input_.Cursor().SetImage(image); }
    /// Removes the custom cursor image.
    void ClearCursorImage() override { input_.Cursor().ClearImage(); }
    /// Returns true when the compositor offers `zwp_pointer_constraints_v1` and `zwp_relative_pointer_manager_v1`.
    bool SupportsPointerLock() const override { return input_.Cursor().SupportsLock(); }
    /// Locks or unlocks the pointer; see [[os::Window::SetPointerLock]].
    bool SetPointerLock(bool locked) override { return Valid() && input_.Cursor().SetLock(locked); }
    /// Returns whether the compositor has granted the lock.
    bool PointerLocked() const override { return input_.Cursor().Locked(); }

    /// Returns whether the user closed the window or the compositor connection was lost.
    ///
    /// Also true once the compositor has exited, crashed or raised a protocol error:
    /// no window is left to wait on, so the frame loop must be able to exit.
    bool ShouldClose() const override { return window_.ShouldClose() || connection_lost_; }
    /// Returns whether the compositor connection died (exit, crash or protocol error).
    bool ConnectionLost() const { return connection_lost_; }
    /// Returns the underlying `wl_display`, or null if not open.
    ///
    /// Ownership: borrowed; owned by this window's connection and valid until [[os::wayland::WaylandPlatformWindow::Close]].
    wl_display* NativeDisplay() const { return connection_.Display(); }
    /// Returns the current window width in pixels.
    u32 Width() const override { return window_.Width(); }
    /// Returns the current window height in pixels.
    u32 Height() const override { return window_.Height(); }
    /// Returns the backend id, `"wayland"`.
    const char* BackendName() const override { return "wayland"; }
    /// Returns the `wl_display*` and `wl_surface*`, with kind `Wayland`; `Invalid` if the window is not open.
    NativeWindowInfo GetNativeInfo() const override {
        NativeWindowInfo info;
        if (Valid()) {
            info.kind = NativeWindowKind::Wayland;
            info.display = connection_.Display();
            info.surface = window_.Surface();
            window_.MarkSurfaceExported();
        }
        return info;
    }
    /// Returns whether the window is closed, the connection was lost, or it has zero area; Wayland exposes no minimized state to clients.
    bool IsMinimized() const override { return !Valid() || window_.Width() == 0 || window_.Height() == 0; }
    /// Returns a counter that changes each time a configure changes the window size; 0 when not open.
    u64 ResizeSerial() const override { return Valid() ? window_.ResizeSerial() : 0; }
    /// Returns true on an open window: fullscreen is part of xdg-shell, which every compositor with a window has.
    bool SupportsFullscreen() const override { return Valid(); }
    /// Asks the compositor for fullscreen on or off; see [[os::wayland::WaylandWindow::SetFullscreen]].
    /// Param fullscreen: the state wanted.
    /// Returns: true if the request was sent.
    bool SetFullscreen(bool fullscreen) override {
        return Valid() && window_.SetFullscreen(fullscreen); // sent with the next PumpEvents
    }
    /// Returns whether the compositor's latest configure said fullscreen.
    bool IsFullscreen() const override { return Valid() && window_.IsFullscreen(); }

    /// Returns the seat input handler, for querying keyboard, pointer and keymap availability.
    const WaylandInput& Input() const { return input_; }

private:
    WaylandConnection connection_;
    WaylandWindow window_;
    WaylandInput input_;
    u64 last_resize_serial_ = 0;
    bool connection_lost_ = false;

    void MarkConnectionLost(const char* why);
};

} // namespace engine_placeholder::os::wayland
