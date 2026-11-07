// The window backend choice: the name parser and both platforms' plans run everywhere; the window host is tested on the platform it is
// built for (Linux: Wayland and X11, Windows: Win32, Phase 13 Day 158).
#include <cstdlib>

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/input.h"
#include "platform/window_host.h"
#include "os/os.h"
#include "platform/window.h"

#if defined(_WIN32)
#include "test_env.h"
#else
#include <sys/socket.h>

#include <string>

#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/linux/x11/x11_window.h"
#endif

// The using-directives stay inside this namespace: at file scope they would leak into the files merged after this one in a unity build
// (an X11 header then finds `Window` ambiguous).
namespace window_backend_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;

#if !defined(_WIN32)
namespace {

// Sets/unsets an env var for a scope, restoring the previous value.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        const char* old = std::getenv(name);
        had_ = old != nullptr;
        if (had_) {
            old_ = old;
        }
        if (value != nullptr) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }
    ~ScopedEnv() {
        if (had_) {
            setenv(name_, old_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    bool had_ = false;
    std::string old_;
};

bool HaveEnv(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0';
}

} // namespace
#endif

TEST_CASE("ParseWindowBackend recognizes auto/wayland/x11/win32 and rejects the rest", "[window][os]") {
    REQUIRE(ParseWindowBackend("auto") == WindowBackend::Auto);
    REQUIRE(ParseWindowBackend("wayland") == WindowBackend::Wayland);
    REQUIRE(ParseWindowBackend("x11") == WindowBackend::X11);
    REQUIRE(ParseWindowBackend("win32") == WindowBackend::Win32);
    REQUIRE(ParseWindowBackend("") == WindowBackend::Invalid);
    REQUIRE(ParseWindowBackend("X11") == WindowBackend::Invalid);
    REQUIRE(ParseWindowBackend("Win32") == WindowBackend::Invalid);
}

TEST_CASE("PlanWindowBackends: auto prefers Wayland, falls back to X11", "[window][os]") {
    BackendPlan both = PlanWindowBackends(WindowBackend::Auto, true, true);
    REQUIRE(both.count == 2);
    REQUIRE(both.candidates[0] == WindowBackend::Wayland);
    REQUIRE(both.candidates[1] == WindowBackend::X11);

    BackendPlan wayland_only = PlanWindowBackends(WindowBackend::Auto, true, false);
    REQUIRE(wayland_only.count == 1);
    REQUIRE(wayland_only.candidates[0] == WindowBackend::Wayland);

    // $WAYLAND_DISPLAY unset -> straight to X11.
    BackendPlan x11_only = PlanWindowBackends(WindowBackend::Auto, false, true);
    REQUIRE(x11_only.count == 1);
    REQUIRE(x11_only.candidates[0] == WindowBackend::X11);

    REQUIRE(PlanWindowBackends(WindowBackend::Auto, false, false).count == 0);
}

TEST_CASE("PlanWindowBackends: a forced backend ignores the environment and never falls back", "[window][os]") {
    for (bool w : {false, true}) {
        for (bool x : {false, true}) {
            BackendPlan wl = PlanWindowBackends(WindowBackend::Wayland, w, x);
            REQUIRE(wl.count == 1);
            REQUIRE(wl.candidates[0] == WindowBackend::Wayland);
            BackendPlan x11 = PlanWindowBackends(WindowBackend::X11, w, x);
            REQUIRE(x11.count == 1);
            REQUIRE(x11.candidates[0] == WindowBackend::X11);
        }
    }
    REQUIRE(PlanWindowBackends(WindowBackend::Invalid, true, true).count == 0);
    REQUIRE(PlanWindowBackends(WindowBackend::Invalid, true, true).refusal == nullptr);
}

TEST_CASE("PlanWindowBackends: Linux refuses win32 with the reason, whatever the environment", "[window][os]") {
    for (bool w : {false, true}) {
        for (bool x : {false, true}) {
            const BackendPlan plan = PlanWindowBackends(WindowBackend::Win32, w, x);
            REQUIRE(plan.count == 0);
            REQUIRE(plan.refusal != nullptr);
            CHECK(StringView(plan.refusal) == StringView("win32 is the Windows backend"));
        }
    }
    CHECK(PlanWindowBackends(WindowBackend::Auto, false, false).refusal == nullptr); // nothing set is not a refusal by name
}

TEST_CASE("PlanWin32WindowBackends: auto and win32 give Win32, wayland and x11 are refused with the reason", "[window][os]") {
    for (WindowBackend requested : {WindowBackend::Auto, WindowBackend::Win32}) {
        const BackendPlan plan = PlanWin32WindowBackends(requested);
        REQUIRE(plan.count == 1);
        CHECK(plan.candidates[0] == WindowBackend::Win32);
        CHECK(plan.refusal == nullptr);
    }
    const BackendPlan wayland = PlanWin32WindowBackends(WindowBackend::Wayland);
    CHECK(wayland.count == 0);
    REQUIRE(wayland.refusal != nullptr);
    CHECK(StringView(wayland.refusal) == StringView("wayland is a Linux backend"));
    const BackendPlan x11 = PlanWin32WindowBackends(WindowBackend::X11);
    CHECK(x11.count == 0);
    REQUIRE(x11.refusal != nullptr);
    CHECK(StringView(x11.refusal) == StringView("x11 is a Linux backend"));
    CHECK(PlanWin32WindowBackends(WindowBackend::Invalid).count == 0);
}

#if defined(_WIN32)

namespace {

// Sets ENGINE_WINDOW_BACKEND for a scope and removes it after (the test processes do not set it themselves).
struct ScopedBackendVariable {
    explicit ScopedBackendVariable(const char* value) { test_support::SetEnv("ENGINE_WINDOW_BACKEND", value); }
    ~ScopedBackendVariable() { test_support::UnsetEnv("ENGINE_WINDOW_BACKEND"); }
    ScopedBackendVariable(const ScopedBackendVariable&) = delete;
    ScopedBackendVariable& operator=(const ScopedBackendVariable&) = delete;
};

} // namespace

TEST_CASE("Windows: PlatformWindowHost opens Win32 for auto and for win32", "[window][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    for (WindowBackend requested : {WindowBackend::Auto, WindowBackend::Win32}) {
        InputState input;
        PlatformWindowHost host;
        REQUIRE(host.Open(requested, "host win32", 640, 480, &input));
        os::Window* w = host.Get();
        REQUIRE(w != nullptr);
        REQUIRE(w->Valid());
        CHECK(StringView(w->BackendName()) == StringView("win32"));
        CHECK(w->Width() == 640);
        CHECK(w->Height() == 480);
        CHECK_FALSE(w->ShouldClose());
        CHECK(w->GetNativeInfo().kind == NativeWindowKind::Win32);
        w->PumpEvents();
        REQUIRE_FALSE(host.Open(requested, "second", 320, 240, &input)); // a host opens once
    }
}

TEST_CASE("Windows: PlatformWindowHost refuses Wayland and X11", "[window][os]") {
    InputState input;
    PlatformWindowHost wayland;
    CHECK_FALSE(wayland.Open(WindowBackend::Wayland, "no wayland", 320, 240, &input));
    CHECK(wayland.Get() == nullptr);
    PlatformWindowHost x11;
    CHECK_FALSE(x11.Open(WindowBackend::X11, "no x11", 320, 240, &input));
    CHECK(x11.Get() == nullptr);
}

TEST_CASE("Windows: PlatformWindowHost::OpenFromEnvironment honors ENGINE_WINDOW_BACKEND", "[window][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    {
        ScopedBackendVariable bad("carrier-pigeon");
        PlatformWindowHost host;
        CHECK_FALSE(host.OpenFromEnvironment("bad override", 320, 240, &input));
    }
    {
        ScopedBackendVariable linux_only("x11");
        PlatformWindowHost host;
        CHECK_FALSE(host.OpenFromEnvironment("x11 on windows", 320, 240, &input));
    }
    {
        ScopedBackendVariable win32("win32");
        PlatformWindowHost host;
        REQUIRE(host.OpenFromEnvironment("env win32", 320, 240, &input));
        CHECK(StringView(host.Get()->BackendName()) == StringView("win32"));
    }
    PlatformWindowHost unset;
    REQUIRE(unset.OpenFromEnvironment("env unset", 320, 240, &input));
    CHECK(StringView(unset.Get()->BackendName()) == StringView("win32"));
}

#else // Linux: the window host over Wayland and X11

TEST_CASE("PlatformWindowHost opens the Wayland backend when forced", "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::Wayland, "forced wayland", 640, 480, &input));
    os::Window* w = host.Get();
    REQUIRE(w != nullptr);
    REQUIRE(w->Valid());
    REQUIRE(StringView(w->BackendName()) == StringView("wayland"));
    // The requested size is honored when the compositor doesn't suggest
    // one (as with X11). If this fails on some compositor that always
    // suggests its own size, relax it to > 0 for that case.
    REQUIRE(w->Width() == 640);
    REQUIRE(w->Height() == 480);
    REQUIRE_FALSE(w->ShouldClose());
    w->PumpEvents();
}

TEST_CASE("PlatformWindowHost opens the X11 backend when forced", "[window][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::X11, "forced x11", 640, 480, &input));
    os::Window* w = host.Get();
    REQUIRE(w != nullptr);
    REQUIRE(w->Valid());
    REQUIRE(StringView(w->BackendName()) == StringView("x11"));
    REQUIRE(w->Width() == 640);
    REQUIRE(w->Height() == 480);
    REQUIRE_FALSE(w->ShouldClose());
    w->PumpEvents();
}

TEST_CASE("PlatformWindowHost auto falls back to X11 when WAYLAND_DISPLAY is unset", "[window][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    ScopedEnv no_wayland("WAYLAND_DISPLAY", nullptr);
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::Auto, "auto fallback", 320, 240, &input));
    REQUIRE(StringView(host.Get()->BackendName()) == StringView("x11"));
}

TEST_CASE("PlatformWindowHost auto falls back to X11 when the Wayland connection fails", "[window][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    // WAYLAND_DISPLAY is set but names a socket that doesn't exist.
    ScopedEnv bad_wayland("WAYLAND_DISPLAY", "wayland-does-not-exist");
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::Auto, "auto failed wayland", 320, 240, &input));
    REQUIRE(StringView(host.Get()->BackendName()) == StringView("x11"));
}

TEST_CASE("A forced backend does not fall back when it fails", "[window][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    ScopedEnv bad_wayland("WAYLAND_DISPLAY", "wayland-does-not-exist");
    InputState input;
    PlatformWindowHost host;
    REQUIRE_FALSE(host.Open(WindowBackend::Wayland, "forced, broken", 320, 240, &input));
    REQUIRE(host.Get() == nullptr);
}

TEST_CASE("PlatformWindowHost::OpenFromEnvironment honors ENGINE_WINDOW_BACKEND", "[window][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    {
        ScopedEnv bad("ENGINE_WINDOW_BACKEND", "carrier-pigeon");
        PlatformWindowHost host;
        REQUIRE_FALSE(host.OpenFromEnvironment("bad override", 320, 240, &input));
    }
    if (HaveEnv("DISPLAY")) {
        ScopedEnv force("ENGINE_WINDOW_BACKEND", "x11");
        PlatformWindowHost host;
        REQUIRE(host.OpenFromEnvironment("env x11", 320, 240, &input));
        REQUIRE(StringView(host.Get()->BackendName()) == StringView("x11"));
    }
    if (HaveEnv("WAYLAND_DISPLAY")) {
        ScopedEnv force("ENGINE_WINDOW_BACKEND", "wayland");
        PlatformWindowHost host;
        REQUIRE(host.OpenFromEnvironment("env wayland", 320, 240, &input));
        REQUIRE(StringView(host.Get()->BackendName()) == StringView("wayland"));
    }
}

TEST_CASE("PlatformWindowHost rejects opening twice and a null InputState for Wayland", "[window][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::X11, "first", 320, 240, &input));
    REQUIRE_FALSE(host.Open(WindowBackend::X11, "second", 320, 240, &input));
    REQUIRE(host.Get() != nullptr);

    if (HaveEnv("WAYLAND_DISPLAY")) {
        PlatformWindowHost w;
        REQUIRE_FALSE(w.Open(WindowBackend::Wayland, "no input", 320, 240, nullptr));
    }
}

TEST_CASE("The Wayland backend ends up with a keyboard and keymap (seat capabilities not lost to window setup)",
          "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    wayland::WaylandPlatformWindow window;
    REQUIRE(window.Open("input after window", 320, 240, &input));
    // A desktop seat has a keyboard and pointer; if this fails the
    // capabilities event was delivered before the listener existed.
    REQUIRE(window.Input().HasKeyboard());
    REQUIRE(window.Input().HasKeymap());
    REQUIRE(window.Input().HasPointer());
}

TEST_CASE("WaylandPlatformWindow reports a dead compositor connection as ShouldClose", "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    wayland::WaylandPlatformWindow window;
    REQUIRE(window.Open("connection loss", 320, 240, &input));
    window.PumpEvents();
    REQUIRE_FALSE(window.ShouldClose());
    REQUIRE_FALSE(window.ConnectionLost());

    // Sever the socket underneath libwayland, as a crashed compositor
    // would. Pumping must notice and flip ShouldClose instead of running
    // on a dead window forever.
    REQUIRE(::shutdown(wl_display_get_fd(window.NativeDisplay()), SHUT_RDWR) == 0);
    for (int i = 0; i < 20 && !window.ShouldClose(); ++i) {
        window.PumpEvents();
        engine_placeholder::os::SleepNanos(5'000'000);
    }
    REQUIRE(window.ConnectionLost());
    REQUIRE(window.ShouldClose());
    REQUIRE_FALSE(window.Valid());
    // The three native-surface accessors agree that the window is gone.
    REQUIRE(window.GetNativeInfo().kind == NativeWindowKind::Invalid);
    REQUIRE(window.IsMinimized());
    REQUIRE(window.ResizeSerial() == 0);
    window.PumpEvents(); // further pumps are harmless no-ops
}

TEST_CASE("WaylandPlatformWindow::Close tears everything down and is idempotent", "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    wayland::WaylandPlatformWindow window;
    REQUIRE(window.Open("close me", 320, 240, &input));
    REQUIRE(window.Valid());
    REQUIRE(window.Input().HasKeyboard());
    const u64 serial_before_close = window.ResizeSerial();
    REQUIRE(serial_before_close != 0);

    window.Close();
    REQUIRE_FALSE(window.Valid());
    REQUIRE(window.ResizeSerial() == 0);
    REQUIRE(window.IsMinimized());
    REQUIRE_FALSE(window.Input().Valid());
    REQUIRE_FALSE(window.Input().HasKeyboard());
    REQUIRE(window.NativeDisplay() == nullptr);
    window.Close(); // idempotent
    window.PumpEvents();

    // A fresh Open on the same object works (new connection, new seat).
    const u64 before_close = serial_before_close;
    REQUIRE(window.Open("reopened", 320, 240, &input));
    REQUIRE(window.Valid());
    REQUIRE(window.Input().HasKeyboard());
    // A swapchain remembering the old window's serial must see the reopened window as changed.
    REQUIRE(window.ResizeSerial() != before_close);
}

TEST_CASE("A Wayland window exposes its native display and surface", "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::Wayland, "native info", 320, 240, &input));
    os::Window* w = host.Get();
    REQUIRE(w != nullptr);

    NativeWindowInfo info = w->GetNativeInfo();
    REQUIRE(info.kind == NativeWindowKind::Wayland);
    REQUIRE(info.display != nullptr);
    REQUIRE(info.surface != nullptr);
    REQUIRE(info.window_id == 0);
    REQUIRE_FALSE(w->IsMinimized());
    REQUIRE(w->ResizeSerial() != 0);
    // Stable across calls and pumps (the handles belong to the window, not to the call).
    w->PumpEvents();
    NativeWindowInfo again = w->GetNativeInfo();
    REQUIRE(again.display == info.display);
    REQUIRE(again.surface == info.surface);
}

TEST_CASE("A closed or never-opened Wayland window has no native info and counts as minimized", "[window][wayland][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    engine_placeholder::os::wayland::WaylandPlatformWindow closed;
    NativeWindowInfo info = closed.GetNativeInfo();
    REQUIRE(info.kind == NativeWindowKind::Invalid);
    REQUIRE(info.display == nullptr);
    REQUIRE(info.surface == nullptr);
    REQUIRE(closed.IsMinimized());  // zero area
    REQUIRE(closed.ResizeSerial() == 0);

    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    engine_placeholder::os::wayland::WaylandPlatformWindow opened;
    REQUIRE(opened.Open("native info close", 200, 200, &input));
    REQUIRE(opened.GetNativeInfo().kind == NativeWindowKind::Wayland);
    opened.Close();
    REQUIRE(opened.GetNativeInfo().kind == NativeWindowKind::Invalid);
}

TEST_CASE("An X11 window exposes its native display and window id", "[window][x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::X11, "native info x11", 320, 240, &input));
    os::Window* w = host.Get();
    REQUIRE(w != nullptr);

    NativeWindowInfo info = w->GetNativeInfo();
    REQUIRE(info.kind == NativeWindowKind::Xlib);
    REQUIRE(info.display != nullptr);
    REQUIRE(info.window_id != 0);
    REQUIRE(info.surface == nullptr);
    REQUIRE_FALSE(w->IsMinimized());
}

#endif

} // namespace window_backend_test
