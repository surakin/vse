#pragma once

#include "os/input.h"
#include "string/string.h"
#include "types.h"

//! Platform-neutral window interface implemented by each display backend (Wayland, X11).
//!
//! The interface is polled: pump events, then read size, close request and the bound `InputState`.

namespace engine_placeholder::os {

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
};

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
