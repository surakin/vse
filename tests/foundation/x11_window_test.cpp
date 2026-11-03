#include <utility>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <catch2/catch_test_macros.hpp>

#include "os/linux/x11/x11_window.h"
#include "os/os.h"

using engine_placeholder::os::x11::X11Window;

namespace {

// Pumps `window` until `done()` or ~2s pass.
template <typename Pred>
bool PumpUntil(X11Window& window, Pred done) {
    for (int i = 0; i < 200; ++i) {
        window.PumpEvents(nullptr);
        if (done()) {
            return true;
        }
        engine_placeholder::os::SleepNanos(10'000'000);
    }
    return false;
}

} // namespace

TEST_CASE("X11Window::Create opens a mapped window on the real X server", "[x11][os]") {
    X11Window window = X11Window::Create("engine_placeholder x11 test", 640, 480);
    REQUIRE(window.Valid());
    REQUIRE(window.NativeDisplay() != nullptr);
    REQUIRE(window.NativeWindow() != 0);
    REQUIRE(window.Width() == 640);
    REQUIRE(window.Height() == 480);
    REQUIRE_FALSE(window.ShouldClose());

    // Query the server independently of our own bookkeeping. With a
    // window manager running, XMapWindow is a request the WM completes
    // asynchronously, so poll (pumping events) until it's viewable.
    Display* display = static_cast<Display*>(window.NativeDisplay());
    REQUIRE(PumpUntil(window, [&] {
        XWindowAttributes attrs;
        return XGetWindowAttributes(display, window.NativeWindow(), &attrs) != 0 && attrs.map_state == IsViewable;
    }));
}

TEST_CASE("X11Window::Create fails gracefully for a zero-sized window", "[x11][os]") {
    X11Window window = X11Window::Create("zero", 0, 480);
    REQUIRE_FALSE(window.Valid());
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("A default-constructed X11Window is invalid and pumping it is a no-op", "[x11][os]") {
    X11Window window;
    REQUIRE_FALSE(window.Valid());
    window.PumpEvents(nullptr);
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("X11Window sets ShouldClose when the WM sends WM_DELETE_WINDOW", "[x11][os]") {
    X11Window window = X11Window::Create("close test", 320, 240);
    REQUIRE(window.Valid());

    // Act as the window manager's close button would: a second client
    // connection sends the WM_PROTOCOLS/WM_DELETE_WINDOW ClientMessage.
    Display* other = XOpenDisplay(nullptr);
    REQUIRE(other != nullptr);
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.window = window.NativeWindow();
    event.xclient.message_type = XInternAtom(other, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(XInternAtom(other, "WM_DELETE_WINDOW", False));
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(other, window.NativeWindow(), False, NoEventMask, &event);
    XSync(other, False);
    XCloseDisplay(other);

    REQUIRE(PumpUntil(window, [&] { return window.ShouldClose(); }));
}

TEST_CASE("X11Window ignores unrelated ClientMessages", "[x11][os]") {
    X11Window window = X11Window::Create("ignore test", 320, 240);
    REQUIRE(window.Valid());

    Display* other = XOpenDisplay(nullptr);
    REQUIRE(other != nullptr);
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.window = window.NativeWindow();
    event.xclient.message_type = XInternAtom(other, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(XInternAtom(other, "WM_TAKE_FOCUS", False));
    XSendEvent(other, window.NativeWindow(), False, NoEventMask, &event);
    XSync(other, False);
    XCloseDisplay(other);

    for (int i = 0; i < 20; ++i) {
        window.PumpEvents(nullptr);
        engine_placeholder::os::SleepNanos(5'000'000);
    }
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("X11Window tracks resizes from ConfigureNotify", "[x11][os]") {
    X11Window window = X11Window::Create("resize test", 300, 200);
    REQUIRE(window.Valid());

    Display* other = XOpenDisplay(nullptr);
    REQUIRE(other != nullptr);
    XResizeWindow(other, window.NativeWindow(), 411, 273);
    XSync(other, False);
    XCloseDisplay(other);

    REQUIRE(PumpUntil(window, [&] { return window.Width() == 411 && window.Height() == 273; }));
}

TEST_CASE("X11Window move transfers ownership without double-close", "[x11][os]") {
    X11Window a = X11Window::Create("move test", 200, 200);
    REQUIRE(a.Valid());
    unsigned long native = a.NativeWindow();

    X11Window b(std::move(a));
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(b.NativeWindow() == native);

    X11Window c;
    c = std::move(b);
    REQUIRE_FALSE(b.Valid());
    REQUIRE(c.Valid());
    REQUIRE(c.NativeWindow() == native);
}
