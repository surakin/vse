// Phase 8 Day 107: the fullscreen request of os::Window on the real display backends.
#include <catch2/catch_test_macros.hpp>

#include "os/input.h"
#include "os/linux/wayland/wayland_platform_window.h"
#include "os/linux/x11/x11_window.h"
#include "os/os.h"
#include "os/window.h"

using namespace engine_placeholder;

namespace {

template <typename Pred>
bool PumpUntil(os::Window& window, Pred done) {
    for (int i = 0; i < 300; ++i) {
        window.PumpEvents();
        if (done()) {
            return true;
        }
        os::SleepNanos(10'000'000);
    }
    return false;
}

void ToggleFullscreen(os::Window& window) {
    REQUIRE(window.Valid());
    REQUIRE(window.SupportsFullscreen());
    CHECK_FALSE(window.IsFullscreen());
    REQUIRE(window.SetFullscreen(true));
    if (!PumpUntil(window, [&] { return window.IsFullscreen(); })) {
        SKIP("the window manager or compositor did not honor the fullscreen request in 3 s");
    }
    REQUIRE(window.SetFullscreen(false));
    CHECK(PumpUntil(window, [&] { return !window.IsFullscreen(); }));
}

} // namespace

TEST_CASE("An X11 window toggles fullscreen", "[x11][window][fullscreen]") {
    os::x11::X11Window window = os::x11::X11Window::Create("fullscreen test", 320, 240);
    if (!window.Valid()) {
        SKIP("no X server");
    }
    ToggleFullscreen(window);
}

TEST_CASE("A Wayland window toggles fullscreen", "[wayland][window][fullscreen]") {
    os::InputState input;
    os::wayland::WaylandPlatformWindow window;
    if (!window.Open("fullscreen test", 320, 240, &input)) {
        SKIP("no Wayland compositor");
    }
    ToggleFullscreen(window);
}

TEST_CASE("The default Window reports fullscreen unsupported", "[window][fullscreen]") {
    os::x11::X11Window invalid; // a closed window supports nothing
    CHECK_FALSE(invalid.SupportsFullscreen());
    CHECK_FALSE(invalid.SetFullscreen(true));
    CHECK_FALSE(invalid.IsFullscreen());
}
