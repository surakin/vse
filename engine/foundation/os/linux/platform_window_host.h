#pragma once

#include "os/linux/wayland/wayland_platform_window.h"
#include "os/linux/x11/x11_window.h"
#include "os/window.h"

//! Linux window host: picks, opens and owns the Wayland or X11 window backend.

namespace engine_placeholder::os {

/// Picks and owns the Linux window backend, exposing whichever opened as an [[os::Window]].
///
/// Holds both concrete backends by value (no heap allocation). Not movable or copyable.
///
/// Thread-safety: not-thread-safe. Use from the thread that opened it.
/// Ownership: owns the opened backend window and its connection; the `InputState`
///   passed to `Open` is borrowed and must outlive the host.
class PlatformWindowHost {
public:
    PlatformWindowHost() = default;
    PlatformWindowHost(const PlatformWindowHost&) = delete;
    PlatformWindowHost& operator=(const PlatformWindowHost&) = delete;

    /// Opens a window on the backend chosen by `requested`, trying fallbacks per [[os::PlanWindowBackends]].
    ///
    /// For `WindowBackend::Auto` it reads `$WAYLAND_DISPLAY` and `$DISPLAY` to pick candidates.
    /// Param requested: backend to use, or `Auto` to try Wayland then X11.
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
    wayland::WaylandPlatformWindow wayland_;
    x11::X11Window x11_;
    Window* active_ = nullptr;
};

} // namespace engine_placeholder::os
