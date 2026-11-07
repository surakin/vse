// The window host on Windows (Phase 13 Day 158): the one backend is Win32 (PlanWin32WindowBackends); Wayland and X11 are refused.
#include "platform/window_host.h"

#include <cstdlib>

#include "debug/crash_state.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "platform/windows/win32_window.h"
#include "string/string.h"

namespace engine_placeholder::os {

struct PlatformWindowHost::Impl {
    win32::Win32Window win32;
};

PlatformWindowHost::PlatformWindowHost() = default;
PlatformWindowHost::~PlatformWindowHost() = default;

bool PlatformWindowHost::Open(WindowBackend requested, StringView title, u32 width, u32 height, InputState* input) {
    if (active_ != nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Window: PlatformWindowHost::Open called twice");
        return false;
    }
    const BackendPlan plan = PlanWin32WindowBackends(requested);
    if (plan.count == 0) {
        ArenaAllocator scratch(128);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("Window: no usable backend (").Append(plan.refusal != nullptr ? plan.refusal : "none asked for").Append(')');
        Log(LogPlatform, LogVerbosity::Error, line.View());
        return false;
    }
    impl_ = MakeUnique<Impl>();
    if (!impl_->win32.Open(title, width, height, input)) {
        Log(LogPlatform, LogVerbosity::Error, "Window: the Win32 window did not open");
        return false;
    }
    active_ = &impl_->win32;
    Log(LogPlatform, LogVerbosity::Info, "Window: using win32 backend");
    debug::SetCrashStateText("window", "win32"); // the crash report says which display backend this run used
    return true;
}

bool PlatformWindowHost::OpenFromEnvironment(StringView title, u32 width, u32 height, InputState* input) {
    WindowBackend requested = WindowBackend::Auto;
    const char* value = std::getenv("ENGINE_WINDOW_BACKEND");
    if (value != nullptr && value[0] != '\0') {
        requested = ParseWindowBackend(StringView(value));
        if (requested == WindowBackend::Invalid) {
            Log(LogPlatform, LogVerbosity::Error, "Window: unrecognized ENGINE_WINDOW_BACKEND (want auto|win32)");
            return false;
        }
    }
    return Open(requested, title, width, height, input);
}

} // namespace engine_placeholder::os
