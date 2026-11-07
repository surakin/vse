#pragma once

#include "containers/unique_ptr.h"
#include "platform/input.h"
#include "platform/window.h"
#include "string_view.h"
#include "types.h"

//! The window host: picks, opens and owns the display backend of the running platform and exposes it as a [[os::Window]]. The header is
//! the same on every platform; the implementation lives in `platform/<platform>/` (Wayland or X11 on Linux), so a program that includes only
//! this header builds unchanged on each.

namespace engine_placeholder::os {

/// Picks and owns the window backend of the running platform, exposing whichever opened as an [[os::Window]].
///
/// The backend is created by [[os::PlatformWindowHost::Open]] and destroyed with the host. Not movable or copyable.
///
/// Thread-safety: not-thread-safe. Use from the thread that opened it.
/// Ownership: owns the opened backend window and its connection; the `InputState`
///   passed to `Open` is borrowed and must outlive the host.
class PlatformWindowHost {
public:
    /// Creates a host with no window; call [[os::PlatformWindowHost::Open]] to open one.
    PlatformWindowHost();
    /// Closes the window and the connection to the display, if a backend opened.
    ~PlatformWindowHost();
    PlatformWindowHost(const PlatformWindowHost&) = delete;
    PlatformWindowHost& operator=(const PlatformWindowHost&) = delete;

    /// Opens a window on the backend chosen by `requested`, trying fallbacks per [[os::PlanWindowBackends]].
    ///
    /// For `WindowBackend::Auto` it reads the platform's display environment (on Linux `$WAYLAND_DISPLAY` and `$DISPLAY`) to pick candidates.
    /// Param requested: backend to use, or `Auto` to try the platform's backends in order.
    /// Param title: window title.
    /// Param width: requested client width in pixels (a hint; the compositor may override it).
    /// Param height: requested client height in pixels (a hint; the compositor may override it).
    /// Param input: receives keyboard and pointer state; must outlive the host.
    /// Returns: `true` if a backend opened.
    /// Fails: returns `false` (logged) if no candidate backend opened.
    bool Open(WindowBackend requested, StringView title, u32 width, u32 height, InputState* input);

    /// Opens a window like [[os::PlatformWindowHost::Open]], taking the backend from `$ENGINE_WINDOW_BACKEND`.
    ///
    /// The variable accepts `auto`, `wayland` or `x11`; unset or empty means `auto`.
    /// Param title: window title.
    /// Param width: requested client width in pixels.
    /// Param height: requested client height in pixels.
    /// Param input: receives keyboard and pointer state; must outlive the host.
    /// Returns: `true` if a backend opened.
    /// Fails: returns `false` (logged) if the variable is unrecognized or no backend opened.
    bool OpenFromEnvironment(StringView title, u32 width, u32 height, InputState* input);

    /// Returns the opened window, or null until a backend has opened.
    ///
    /// Ownership: the pointer is borrowed from the host and valid for its lifetime.
    Window* Get() { return active_; }

private:
    struct Impl; // the concrete backends, defined by the platform's implementation file
    UniquePtr<Impl> impl_;
    Window* active_ = nullptr;
};

} // namespace engine_placeholder::os
