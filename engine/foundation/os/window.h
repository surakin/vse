#pragma once

#include "os/input.h"
#include "string/string.h"
#include "types.h"

// Phase 1 Day 26: the internal window interface every platform backend
// implements (Wayland and X11 now; Win32/Cocoa/Android/... later), so
// upper layers never name a backend. Deliberately small and polled:
// open (construction by a backend), resize (Width/Height), close
// (ShouldClose), and input delivered into the InputState the backend
// was bound to at open. Per PLAN.md assumption #11 the shape must not
// preclude touch/gamepad (more members on InputState) or lifecycle
// events beyond open/resize/close (suspend/resume/focus on mobile and
// consoles): those arrive as additive virtuals or state, not by
// reshaping this interface.
namespace engine_placeholder::os {

class Window {
public:
    virtual ~Window() = default;

    virtual bool Valid() const = 0;

    // Drains all pending platform events without blocking, updating the
    // InputState bound at open.
    virtual void PumpEvents() = 0;

    // True once the user/WM asked the window to close.
    virtual bool ShouldClose() const = 0;

    virtual u32 Width() const = 0;
    virtual u32 Height() const = 0;

    // Stable lowercase backend id ("wayland", "x11", ...), for logs and
    // tests.
    virtual const char* BackendName() const = 0;
};

enum class WindowBackend : u8 { Auto = 0, Wayland, X11, Invalid };

// "auto"/"wayland"/"x11" -> enum; anything else (including empty) ->
// WindowBackend::Invalid so callers can tell "unrecognized" from "auto".
WindowBackend ParseWindowBackend(StringView name);

// Ordered backends to try. `requested == Auto`: Wayland first when
// $WAYLAND_DISPLAY is set, then X11 when $DISPLAY is set (Wayland
// failing falls through to X11). A forced backend yields exactly that
// one candidate regardless of environment (no fallback — the override
// exists to test one backend in isolation).
struct BackendPlan {
    WindowBackend candidates[2] = {WindowBackend::Invalid, WindowBackend::Invalid};
    usize count = 0;
};
BackendPlan PlanWindowBackends(WindowBackend requested, bool wayland_env_set, bool x11_env_set);

} // namespace engine_placeholder::os
