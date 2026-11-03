#pragma once

#include "os/input.h"
#include "string/string.h"
#include "types.h"

// Opaque Xlib handles, forward-declared so including this header doesn't
// pull in <X11/Xlib.h> (its Bool/Status/None/KeyPress macros collide
// with ordinary identifiers). Window and Atom are plain XIDs
// (unsigned long) in Xlib.
struct _XDisplay;
struct _XIC;
struct _XIM;

// Phase 1 Days 24-25: the X11 fallback backend (libX11, the reference X
// client library — PLAN.md assumption #3). Day 24 owns the connection,
// window, WM_DELETE_WINDOW close and resize logging; Day 25 adds
// keyboard/pointer translation into the same InputState the Wayland
// backend fills. Unlike the Wayland classes nothing here registers a
// callback capturing `this` (events are polled), so the type is freely
// movable.
namespace engine_placeholder::os::x11 {

class X11Window {
public:
    // Opens the display named by $DISPLAY, creates and maps a toplevel
    // window, and registers WM_DELETE_WINDOW. Returns an invalid window
    // (Valid() == false) if the display can't be opened or a request
    // fails — logged via LogPlatform, never crashes.
    static X11Window Create(StringView title, u32 width, u32 height);

    X11Window() = default;
    ~X11Window();

    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;
    X11Window(X11Window&& other) noexcept;
    X11Window& operator=(X11Window&& other) noexcept;

    bool Valid() const { return display_ != nullptr; }

    // Drains every event already available (never blocks), handling
    // Expose/ConfigureNotify/ClientMessage (Day 24) and, when `input`
    // is non-null, key/button/motion events into it (Day 25).
    void PumpEvents(InputState* input);

    bool ShouldClose() const { return should_close_; }
    u32 Width() const { return width_; }
    u32 Height() const { return height_; }

    _XDisplay* NativeDisplay() const { return display_; }
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
    u32 width_ = 0;
    u32 height_ = 0;
    bool should_close_ = false;
    // The Key each held X keycode was *pressed* as, so a release clears
    // that Key even if modifiers changed in between (same rationale as
    // WaylandInput::held_). X keycodes are 8-bit.
    Key held_[256] = {};
};

} // namespace engine_placeholder::os::x11
