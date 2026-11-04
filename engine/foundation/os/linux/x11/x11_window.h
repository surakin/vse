#pragma once

#include "os/input.h"
#include "os/window.h"
#include "string/string.h"
#include "types.h"

//! X11 window backend: the fallback `os::Window` built on libX11.

// Opaque Xlib handles, forward-declared so including this header doesn't
// pull in <X11/Xlib.h> (its Bool/Status/None/KeyPress macros collide
// with ordinary identifiers). Window and Atom are plain XIDs
// (unsigned long) in Xlib.
struct _XDisplay;
struct _XIC;
struct _XIM;

namespace engine_placeholder::os::x11 {

/// Toplevel window on an X11 display, translating X events into an `InputState`.
///
/// Events are polled rather than delivered through callbacks capturing `this`,
/// so the type is movable.
///
/// Thread-safety: owner-thread-only. Create, pump and destroy it on one thread.
/// Ownership: owns the X display connection, window, and input method and context;
///   all are released on destruction. A moved-from window is invalid.
class X11Window final : public Window {
public:
    /// Opens the display named by `$DISPLAY`, creates and maps a toplevel window, and registers `WM_DELETE_WINDOW`.
    /// Param title: window title.
    /// Param width: initial client width in pixels.
    /// Param height: initial client height in pixels.
    /// Returns: the window; check [[os::x11::X11Window::Valid]].
    /// Fails: returns an invalid window (logged via `LogPlatform`) if the display cannot be opened or a request fails.
    static X11Window Create(StringView title, u32 width, u32 height);

    /// Constructs an invalid window that owns nothing.
    X11Window() = default;

    /// Destroys the window and closes the display connection.
    ~X11Window() override;

    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;
    /// Takes over the resources of `other`, leaving it invalid.
    X11Window(X11Window&& other) noexcept;

    /// Releases this window's resources, then takes over those of `other`, leaving it invalid.
    X11Window& operator=(X11Window&& other) noexcept;

    /// Returns whether the display connection is open.
    bool Valid() const override { return display_ != nullptr; }

    /// Drains every event already queued without blocking, updating window state and `input`.
    ///
    /// Handles Expose, ConfigureNotify and ClientMessage (close request), and,
    /// when `input` is non-null, key, button and motion events.
    /// Param input: input state to update, or null to handle events but ignore input.
    void PumpEvents(InputState* input);

    /// Drains pending events into the `InputState` set by [[os::x11::X11Window::SetInputState]], if any.
    void PumpEvents() override { PumpEvents(input_); }
    /// Sets the input state that [[os::x11::X11Window::PumpEvents()]] fills.
    ///
    /// Param input: state to fill, or null to ignore input; not owned and must outlive its use here.
    void SetInputState(InputState* input) { input_ = input; }
    /// Returns whether the window manager asked the window to close.
    bool ShouldClose() const override { return should_close_; }
    /// Returns the current client width in pixels.
    u32 Width() const override { return width_; }
    /// Returns the current client height in pixels.
    u32 Height() const override { return height_; }
    /// Returns the backend id, `"x11"`.
    const char* BackendName() const override { return "x11"; }
    /// Returns the `Display*` and window id, with kind `Xlib`; `Invalid` if the window is invalid.
    NativeWindowInfo GetNativeInfo() const override {
        NativeWindowInfo info;
        if (display_ != nullptr) {
            info.kind = NativeWindowKind::Xlib;
            info.display = display_;
            info.window_id = window_;
        }
        return info;
    }
    /// Returns whether the window is closed, has zero area, or the window manager has unmapped or iconified it.
    bool IsMinimized() const override { return display_ == nullptr || width_ == 0 || height_ == 0 || unmapped_; }
    /// Returns a counter that changes each time the size changes; 0 for an invalid window.
    u64 ResizeSerial() const override { return resize_serial_; }

    /// Returns the underlying Xlib `Display*`, or null if invalid.
    ///
    /// Ownership: borrowed; owned by this window and closed with it.
    _XDisplay* NativeDisplay() const { return display_; }
    /// Returns the underlying X window id (an XID), or 0 if invalid.
    ///
    /// Ownership: borrowed; owned by this window and destroyed with it.
    unsigned long NativeWindow() const { return window_; }

private:
    void ReleaseAll();
    void HandleKey(const void* xkey_event, bool pressed, InputState* input);
    void ReleaseAllKeys(InputState* input);

    _XDisplay* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long wm_delete_window_ = 0;
    _XIM* input_method_ = nullptr;
    _XIC* input_context_ = nullptr;
    InputState* input_ = nullptr;
    u32 width_ = 0;
    u32 height_ = 0;
    bool should_close_ = false;
    // Set by UnmapNotify (iconified or hidden), cleared by MapNotify.
    bool unmapped_ = false;
    u64 resize_serial_ = 0;
    // The Key each held X keycode was *pressed* as, so a release clears
    // that Key even if modifiers changed in between (same rationale as
    // WaylandInput::held_). X keycodes are 8-bit.
    Key held_[256] = {};
};

} // namespace engine_placeholder::os::x11
