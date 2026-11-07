// Phase 8 Day 107: the fullscreen request of os::Window on the real display backends.
#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/input.h"
#include "os/os.h"
#include "platform/window.h"

#if defined(_WIN32)
#include "platform/windows/win32_window.h"
#else
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/linux/x11/x11_window.h"
#endif

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

#if defined(_WIN32)

// Phase 13 Day 158: borderless over the window's monitor (platform/windows/FULLSCREEN.md), and back to the size it had.
TEST_CASE("A Win32 window toggles fullscreen", "[window][fullscreen]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::win32::Win32Window window;
    REQUIRE(window.Open("fullscreen test", 320, 240, &input));
    ToggleFullscreen(window);
    CHECK(window.Width() == 320); // the placement it had is given back
    CHECK(window.Height() == 240);
}

TEST_CASE("The default Window reports fullscreen unsupported", "[window][fullscreen]") {
    os::win32::Win32Window invalid; // a window that is not open supports nothing
    CHECK_FALSE(invalid.SupportsFullscreen());
    CHECK_FALSE(invalid.SetFullscreen(true));
    CHECK_FALSE(invalid.IsFullscreen());
}

#else

TEST_CASE("An X11 window toggles fullscreen", "[x11][window][fullscreen]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    os::x11::X11Window window = os::x11::X11Window::Create("fullscreen test", 320, 240);
    if (!window.Valid()) {
        SKIP("no X server");
    }
    ToggleFullscreen(window);
}

TEST_CASE("A Wayland window toggles fullscreen", "[wayland][window][fullscreen]") {
    SKIP_IF_NO_DISPLAY_SESSION();
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

#endif
