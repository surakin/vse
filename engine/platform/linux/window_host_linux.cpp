#include "platform/window_host.h"

#include "os/console.h"

#include "debug/crash_state.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/linux/x11/x11_window.h"

namespace vse::os
{

// Both concrete backends, held by value; only the one that opens is used.
struct PlatformWindowHost::Impl
{
    wayland::WaylandPlatformWindow wayland;
    x11::X11Window x11;
};

PlatformWindowHost::PlatformWindowHost() = default;
PlatformWindowHost::~PlatformWindowHost() = default;

namespace
{

bool EnvSet(const char* name)
{
    const char* v = os::GetEnvironment(name);
    return v != nullptr && v[0] != '\0';
}

void LogChosen(const char* backend)
{
    ArenaAllocator scratch(128);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("Window: using ").Append(backend).Append(" backend");
    Log(LogPlatform, LogVerbosity::Info, line.View());
    debug::SetCrashStateText("window", backend);  // the crash report says which display backend this run used
}

}  // namespace

bool PlatformWindowHost::Open(WindowBackend requested, StringView title, u32 width, u32 height, InputState* input)
{
    if (active_ != nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "Window: PlatformWindowHost::Open called twice");
        return false;
    }
    impl_ = MakeUnique<Impl>();
    BackendPlan plan = PlanWindowBackends(requested, EnvSet("WAYLAND_DISPLAY"), EnvSet("DISPLAY"));
    if (plan.count == 0)
    {
        if (plan.refusal != nullptr)
        {
            ArenaAllocator scratch(128);
            StringBuilder<ArenaAllocator> line(scratch);
            line.Append("Window: no usable backend (").Append(plan.refusal).Append(')');
            Log(LogPlatform, LogVerbosity::Error, line.View());
        }
        else
        {
            Log(LogPlatform, LogVerbosity::Error, "Window: no usable backend (WAYLAND_DISPLAY/DISPLAY unset?)");
        }
        return false;
    }
    for (usize i = 0; i < plan.count; ++i)
    {
        if (plan.candidates[i] == WindowBackend::Wayland)
        {
            if (impl_->wayland.Open(title, width, height, input))
            {
                active_ = &impl_->wayland;
                LogChosen("wayland");
                return true;
            }
        }
        else if (plan.candidates[i] == WindowBackend::X11)
        {
            impl_->x11 = x11::X11Window::Create(title, width, height, x11::SizeUnits::Logical);
            if (impl_->x11.Valid())
            {
                impl_->x11.SetInputState(input);
                active_ = &impl_->x11;
                LogChosen("x11");
                return true;
            }
        }
    }
    Log(LogPlatform, LogVerbosity::Error, "Window: every candidate backend failed to open");
    return false;
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
            Log(LogPlatform, LogVerbosity::Error, "Window: unrecognized ENGINE_WINDOW_BACKEND (want auto|wayland|x11)");
            return false;
        }
    }
    return Open(requested, title, width, height, input);
}

}  // namespace vse::os
