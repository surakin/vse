#include "platform/window.h"

#include "sync/atomic.h"

namespace engine_placeholder::os {

namespace detail {
u64 NextResizeSerial() {
    static Atomic<u64> counter{0};
    return counter.FetchAdd(1, MemoryOrder::Relaxed) + 1;
}
} // namespace detail

WindowBackend ParseWindowBackend(StringView name) {
    if (name == StringView("auto")) return WindowBackend::Auto;
    if (name == StringView("wayland")) return WindowBackend::Wayland;
    if (name == StringView("x11")) return WindowBackend::X11;
    if (name == StringView("win32")) return WindowBackend::Win32;
    return WindowBackend::Invalid;
}

BackendPlan PlanWindowBackends(WindowBackend requested, bool wayland_env_set, bool x11_env_set) {
    BackendPlan plan;
    switch (requested) {
    case WindowBackend::Wayland:
    case WindowBackend::X11:
        plan.candidates[plan.count++] = requested;
        break;
    case WindowBackend::Auto:
        if (wayland_env_set) plan.candidates[plan.count++] = WindowBackend::Wayland;
        if (x11_env_set) plan.candidates[plan.count++] = WindowBackend::X11;
        break;
    case WindowBackend::Win32:
        plan.refusal = "win32 is the Windows backend";
        break;
    case WindowBackend::Invalid:
        break;
    }
    return plan;
}

BackendPlan PlanWin32WindowBackends(WindowBackend requested) {
    BackendPlan plan;
    switch (requested) {
    case WindowBackend::Auto:
    case WindowBackend::Win32:
        plan.candidates[plan.count++] = WindowBackend::Win32;
        break;
    case WindowBackend::Wayland:
        plan.refusal = "wayland is a Linux backend";
        break;
    case WindowBackend::X11:
        plan.refusal = "x11 is a Linux backend";
        break;
    case WindowBackend::Invalid:
        break;
    }
    return plan;
}

} // namespace engine_placeholder::os
