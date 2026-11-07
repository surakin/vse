#include "platform/window.h"

#include <atomic>

namespace engine_placeholder::os {

namespace detail {
u64 NextResizeSerial() {
    static std::atomic<u64> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}
} // namespace detail

WindowBackend ParseWindowBackend(StringView name) {
    if (name == StringView("auto")) return WindowBackend::Auto;
    if (name == StringView("wayland")) return WindowBackend::Wayland;
    if (name == StringView("x11")) return WindowBackend::X11;
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
    case WindowBackend::Invalid:
        break;
    }
    return plan;
}

} // namespace engine_placeholder::os
