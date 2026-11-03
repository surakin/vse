#pragma once

#include "os/linux/wayland/wayland_platform_window.h"
#include "os/linux/x11/x11_window.h"
#include "os/window.h"

// Phase 1 Day 26: picks and owns the Linux window backend. Holds both
// concrete backends by value (no heap) and exposes whichever opened as
// an os::Window*. Not movable.
namespace engine_placeholder::os {

class PlatformWindowHost {
public:
    PlatformWindowHost() = default;
    PlatformWindowHost(const PlatformWindowHost&) = delete;
    PlatformWindowHost& operator=(const PlatformWindowHost&) = delete;

    // Opens a window per `requested` (see PlanWindowBackends). Reads
    // $WAYLAND_DISPLAY/$DISPLAY for Auto. `input` must outlive the host.
    // False (logged) if no candidate backend opened.
    bool Open(WindowBackend requested, StringView title, u32 width, u32 height, InputState* input);

    // Same, with the backend taken from the ENGINE_WINDOW_BACKEND env var
    // (auto|wayland|x11; unset/empty = auto; unrecognized = error).
    bool OpenFromEnvironment(StringView title, u32 width, u32 height, InputState* input);

    // Null until a backend has opened.
    Window* Get() { return active_; }

private:
    wayland::WaylandPlatformWindow wayland_;
    x11::X11Window x11_;
    Window* active_ = nullptr;
};

} // namespace engine_placeholder::os
