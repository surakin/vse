#pragma once

#include "platform/input.h"
#include "string/string.h"
#include "types.h"

//! Platform-neutral window interface implemented by each display backend (Wayland, X11).
//!
//! The interface is polled: pump events, then read size, close request and the bound `InputState`.

namespace engine_placeholder::os {

/// Which native windowing system a window belongs to, so a graphics layer knows how to read [[os::NativeWindowInfo]].
enum class NativeWindowKind : u8 {
    Invalid = 0, ///< No native window (an invalid or closed window); named to avoid the X11 `None` macro.
    Wayland,  ///< A Wayland surface: `display` is a `wl_display*`, `surface` is a `wl_surface*`.
    Xlib,     ///< An X11 window: `display` is a `Display*`, `window_id` is the `Window` (XID).
    Win32,    ///< Reserved for the Windows port: `display` is the `HINSTANCE`, `surface` the `HWND`.
    Cocoa,    ///< Reserved for the macOS port: `surface` is the `NSView*` (or `CAMetalLayer*`).
    Android,  ///< Reserved for the Android port: `surface` is the `ANativeWindow*`.
};

/// The native handles of a window, as a plain value a graphics layer can create a presentation surface from.
///
/// This is how the RHI's swapchain gets at the window without including any platform header: the
/// `kind` says which fields are meaningful, and the pointers are opaque here. Unused fields are null or 0.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: every pointer is borrowed from the window that returned it and is valid until that window is
///   destroyed (or, for Wayland, closed); a graphics surface made from it must be destroyed first.
struct NativeWindowInfo {
    /// The windowing system; decides which of the other fields are meaningful.
    NativeWindowKind kind = NativeWindowKind::Invalid;
    /// The display or connection handle (`wl_display*`, `Display*`, `HINSTANCE`), or null.
    void* display = nullptr;
    /// The surface or window handle (`wl_surface*`, `HWND`, `NSView*`, `ANativeWindow*`), or null.
    void* surface = nullptr;
    /// An integer window identifier, for systems whose windows are not pointers (the X11 `Window`); otherwise 0.
    u64 window_id = 0;
};

/// Abstract window implemented by each platform backend, so upper layers never name a backend.
///
/// Input is delivered into the `InputState` the backend was bound to when the window was opened.
///
/// Thread-safety: thread-compatible. Calls are not synchronized; use one window from one thread at a time.
/// Ownership: the window owns its native surface and releases it on destruction.
class Window {
public:
    virtual ~Window() = default;

    /// Returns whether the window was created successfully and is still usable.
    virtual bool Valid() const = 0;

    /// Drains all pending platform events without blocking and updates the bound `InputState`.
    virtual void PumpEvents() = 0;

    /// Returns true once the user or window manager has asked the window to close.
    virtual bool ShouldClose() const = 0;

    /// Returns the current window width in pixels.
    virtual u32 Width() const = 0;

    /// Returns the current window height in pixels.
    virtual u32 Height() const = 0;

    /// Returns the stable lowercase backend id (`"wayland"`, `"x11"`), for logs and tests.
    /// Ownership: the string is static and is never freed by the caller.
    virtual const char* BackendName() const = 0;

    /// Returns the native handles a graphics layer needs to create a presentation surface for this window.
    ///
    /// Returns: the handles; `kind` is `Invalid` and the rest empty if the window is invalid or closed.
    /// Ownership: the pointers are borrowed from this window; see [[os::NativeWindowInfo]].
    virtual NativeWindowInfo GetNativeInfo() const = 0;

    /// Returns whether there is nothing to draw to: the window is not open, has no area, or is reported hidden.
    ///
    /// A window that is invalid, closed or has lost its connection counts, as does one with zero width or
    /// height. X11 also reports a window the window manager has unmapped or iconified (which includes a window
    /// on a hidden desktop). Wayland has no client-visible minimized or occluded state, so there only the
    /// first two cases apply.
    virtual bool IsMinimized() const = 0;

    /// Returns a counter that changes every time the window size changes, so a swapchain can cheaply tell that it is stale.
    ///
    /// The value is opaque: compare it for equality with a value read earlier. Values come from one
    /// process-wide counter, so an open window never reports 0, and a value never repeats across size changes,
    /// windows, or closing and reopening. An invalid or closed window reports 0.
    virtual u64 ResizeSerial() const = 0;

    /// Returns whether this window can be made fullscreen (the platform and, where it can be known, the compositor allow it).
    ///
    /// The default is false; a backend that implements [[os::Window::SetFullscreen]] overrides it. A settings layer greys
    /// the `fullscreen` setting out when this is false.
    virtual bool SupportsFullscreen() const { return false; }

    /// Asks for fullscreen on or off.
    ///
    /// The change is a request: the window manager or compositor answers with a new size, which shows up as a change of
    /// [[os::Window::ResizeSerial]] after `PumpEvents`. Some compositors refuse; then nothing changes.
    ///
    /// Param fullscreen: the state wanted.
    /// Returns: true if the request was sent.
    /// Fails: returns false, changing nothing, if the window is invalid or fullscreen is unsupported.
    virtual bool SetFullscreen(bool fullscreen) {
        (void)fullscreen;
        return false;
    }

    /// Returns whether the window is fullscreen as far as the last `PumpEvents` could tell.
    virtual bool IsFullscreen() const { return false; }
};

namespace detail {
/// Returns the next value of the process-wide resize counter shared by all windows; never 0.
u64 NextResizeSerial();
} // namespace detail

/// Selects which display backend to use for a window.
enum class WindowBackend : u8 {
    Auto = 0, ///< Choose from the environment, Wayland first.
    Wayland,  ///< Force the Wayland backend.
    X11,      ///< Force the X11 backend.
    Invalid,  ///< Unrecognized name, or an empty candidate slot.
};

/// Parses `"auto"`, `"wayland"` or `"x11"` into a [[os::WindowBackend]].
///
/// Anything else, including an empty string, yields `WindowBackend::Invalid`, so callers can tell an
/// unrecognized name from `"auto"`.
/// Param name: backend name, case-sensitive.
WindowBackend ParseWindowBackend(StringView name);

/// Ordered list of backends to try when opening a window.
///
/// Thread-safety: thread-compatible. A plain value type.
struct BackendPlan {
    /// Backends in the order to try; only the first `count` entries are meaningful.
    WindowBackend candidates[2] = {WindowBackend::Invalid, WindowBackend::Invalid};
    /// Number of valid entries in `candidates`.
    usize count = 0;
};

/// Builds the ordered list of backends to try for the requested backend and environment.
///
/// With `Auto`, Wayland comes first when its environment is set, then X11 when its environment is set, so a
/// Wayland failure falls through to X11. A forced backend yields exactly that one candidate regardless
/// of the environment, with no fallback. `Invalid` yields an empty plan.
/// Param requested: the backend asked for.
/// Param wayland_env_set: whether `$WAYLAND_DISPLAY` is set.
/// Param x11_env_set: whether `$DISPLAY` is set.
/// Returns: the plan; `count` is 0 when no backend is eligible.
BackendPlan PlanWindowBackends(WindowBackend requested, bool wayland_env_set, bool x11_env_set);

} // namespace engine_placeholder::os
