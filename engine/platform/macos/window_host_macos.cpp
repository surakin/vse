// The window host on macOS (Phase 21 Day 295): the one backend is Cocoa (PlanCocoaWindowBackends); Wayland, X11 and
// Win32 are refused.
#include "platform/window_host.h"

#include "debug/crash_state.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/console.h"
#include "platform/macos/cocoa_window.h"
#include "string/string.h"

namespace engine_placeholder::os
{

struct PlatformWindowHost::Impl
{
    cocoa::CocoaWindow cocoa;
};

PlatformWindowHost::PlatformWindowHost() = default;
PlatformWindowHost::~PlatformWindowHost() = default;

bool PlatformWindowHost::Open(WindowBackend requested, StringView title, u32 width, u32 height, InputState* input)
{
    if (active_ != nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "Window: PlatformWindowHost::Open called twice");
        return false;
    }
    const BackendPlan plan = PlanCocoaWindowBackends(requested);
    if (plan.count == 0)
    {
        ArenaAllocator scratch(128);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("Window: no usable backend (")
            .Append(plan.refusal != nullptr ? plan.refusal : "none asked for")
            .Append(')');
        Log(LogPlatform, LogVerbosity::Error, line.View());
        return false;
    }
    impl_ = MakeUnique<Impl>();
    if (!impl_->cocoa.Open(title, width, height, input))
    {
        Log(LogPlatform, LogVerbosity::Error, "Window: the Cocoa window did not open");
        return false;
    }
    active_ = &impl_->cocoa;
    Log(LogPlatform, LogVerbosity::Info, "Window: using cocoa backend");
    debug::SetCrashStateText("window", "cocoa");  // the crash report says which display backend this run used
    return true;
}

bool PlatformWindowHost::OpenFromEnvironment(StringView title, u32 width, u32 height, InputState* input)
{
    WindowBackend requested = WindowBackend::Auto;
    const char* value = os::GetEnvironment("ENGINE_WINDOW_BACKEND");
    if (value != nullptr && value[0] != '\0')
    {
        requested = ParseWindowBackend(StringView(value));
        if (requested == WindowBackend::Invalid)
        {
            Log(LogPlatform, LogVerbosity::Error, "Window: unrecognized ENGINE_WINDOW_BACKEND (want auto|cocoa)");
            return false;
        }
    }
    return Open(requested, title, width, height, input);
}

}  // namespace engine_placeholder::os
