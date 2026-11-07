#include <sys/socket.h>

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/input.h"
#include "platform/window_host.h"
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/linux/x11/x11_window.h"
#include "os/os.h"
#include "platform/window.h"

using namespace engine_placeholder;
using namespace engine_placeholder::os;

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

TEST_CASE("ParseWindowBackend recognizes auto/wayland/x11 and rejects the rest", "[window][os]") {
    REQUIRE(ParseWindowBackend("auto") == WindowBackend::Auto);
    REQUIRE(ParseWindowBackend("wayland") == WindowBackend::Wayland);
    REQUIRE(ParseWindowBackend("x11") == WindowBackend::X11);
    REQUIRE(ParseWindowBackend("") == WindowBackend::Invalid);
    REQUIRE(ParseWindowBackend("X11") == WindowBackend::Invalid);
    REQUIRE(ParseWindowBackend("win32") == WindowBackend::Invalid);
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
}

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
