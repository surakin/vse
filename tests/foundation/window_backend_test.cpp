#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "os/os.h"
#include "os/window.h"

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
    if (!HaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::Wayland, "forced wayland", 640, 480, &input));
    Window* w = host.Get();
    REQUIRE(w != nullptr);
    REQUIRE(w->Valid());
    REQUIRE(StringView(w->BackendName()) == StringView("wayland"));
    REQUIRE(w->Width() > 0);
    REQUIRE_FALSE(w->ShouldClose());
    w->PumpEvents();
}

TEST_CASE("PlatformWindowHost opens the X11 backend when forced", "[window][x11][os]") {
    if (!HaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    InputState input;
    PlatformWindowHost host;
    REQUIRE(host.Open(WindowBackend::X11, "forced x11", 640, 480, &input));
    Window* w = host.Get();
    REQUIRE(w != nullptr);
    REQUIRE(w->Valid());
    REQUIRE(StringView(w->BackendName()) == StringView("x11"));
    REQUIRE(w->Width() == 640);
    REQUIRE(w->Height() == 480);
    REQUIRE_FALSE(w->ShouldClose());
    w->PumpEvents();
}

TEST_CASE("PlatformWindowHost auto falls back to X11 when WAYLAND_DISPLAY is unset", "[window][x11][os]") {
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
    ScopedEnv bad_wayland("WAYLAND_DISPLAY", "wayland-does-not-exist");
    InputState input;
    PlatformWindowHost host;
    REQUIRE_FALSE(host.Open(WindowBackend::Wayland, "forced, broken", 320, 240, &input));
    REQUIRE(host.Get() == nullptr);
}

TEST_CASE("PlatformWindowHost::OpenFromEnvironment honors ENGINE_WINDOW_BACKEND", "[window][os]") {
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
