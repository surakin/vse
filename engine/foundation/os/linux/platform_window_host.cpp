#include "os/linux/platform_window_host.h"

#include <cstdlib>

#include "debug/crash_state.h"
#include "log/log.h"
#include "mem/arena_allocator.h"

namespace engine_placeholder::os {

namespace {

bool EnvSet(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0';
}

void LogChosen(const char* backend) {
    ArenaAllocator scratch(128);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("Window: using ").Append(backend).Append(" backend");
    Log(LogPlatform, LogVerbosity::Info, line.View());
    debug::SetCrashStateText("window", backend); // the crash report says which display backend this run used
}

} // namespace

bool PlatformWindowHost::Open(WindowBackend requested, StringView title, u32 width, u32 height,
                              InputState* input) {
    if (active_ != nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Window: PlatformWindowHost::Open called twice");
        return false;
    }
    BackendPlan plan = PlanWindowBackends(requested, EnvSet("WAYLAND_DISPLAY"), EnvSet("DISPLAY"));
    if (plan.count == 0) {
        Log(LogPlatform, LogVerbosity::Error, "Window: no usable backend (WAYLAND_DISPLAY/DISPLAY unset?)");
        return false;
    }
    for (usize i = 0; i < plan.count; ++i) {
        if (plan.candidates[i] == WindowBackend::Wayland) {
            if (wayland_.Open(title, width, height, input)) {
                active_ = &wayland_;
                LogChosen("wayland");
                return true;
            }
        } else if (plan.candidates[i] == WindowBackend::X11) {
            x11_ = x11::X11Window::Create(title, width, height);
            if (x11_.Valid()) {
                x11_.SetInputState(input);
                active_ = &x11_;
                LogChosen("x11");
                return true;
            }
        }
    }
    Log(LogPlatform, LogVerbosity::Error, "Window: every candidate backend failed to open");
    return false;
}

bool PlatformWindowHost::OpenFromEnvironment(StringView title, u32 width, u32 height, InputState* input) {
    WindowBackend requested = WindowBackend::Auto;
    const char* value = std::getenv("ENGINE_WINDOW_BACKEND");
    if (value != nullptr && value[0] != '\0') {
        requested = ParseWindowBackend(StringView(value));
        if (requested == WindowBackend::Invalid) {
            Log(LogPlatform, LogVerbosity::Error, "Window: unrecognized ENGINE_WINDOW_BACKEND (want auto|wayland|x11)");
            return false;
        }
    }
    return Open(requested, title, width, height, input);
}

} // namespace engine_placeholder::os
