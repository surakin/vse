#include <utility>
#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/linux/x11/x11_window.h"
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
    SKIP_IF_NO_DISPLAY_SESSION();
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
    SKIP_IF_NO_DISPLAY_SESSION();
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
    SKIP_IF_NO_DISPLAY_SESSION();
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
    SKIP_IF_NO_DISPLAY_SESSION();
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
    SKIP_IF_NO_DISPLAY_SESSION();
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

// ---- Day 25: input ----------------------------------------------------

#include <X11/keysym.h>

#include <cstdlib>
#include <string_view>

#include "platform/input.h"

using engine_placeholder::os::InputState;
using engine_placeholder::os::Key;
using engine_placeholder::os::KeyFromKeysym;

namespace {

// A second client connection that injects synthetic input events into a
// window, the way a real server would deliver them.
class Injector {
public:
    Injector() : display_(XOpenDisplay(nullptr)) {}
    ~Injector() {
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
    }
    bool Valid() const { return display_ != nullptr; }
    Display* Dpy() const { return display_; }

    void Key(unsigned long window, unsigned int keycode, unsigned int state, bool press) {
        XKeyEvent e = {};
        e.type = press ? KeyPress : KeyRelease;
        e.display = display_;
        e.window = window;
        e.root = DefaultRootWindow(display_);
        e.keycode = keycode;
        e.state = state;
        e.same_screen = True;
        Send(window, reinterpret_cast<XEvent&>(e), press ? KeyPressMask : KeyReleaseMask);
    }

    void Button(unsigned long window, unsigned int button, int x, int y, unsigned int state, bool press) {
        XButtonEvent e = {};
        e.type = press ? ButtonPress : ButtonRelease;
        e.display = display_;
        e.window = window;
        e.root = DefaultRootWindow(display_);
        e.button = button;
        e.x = x;
        e.y = y;
        e.state = state;
        e.same_screen = True;
        Send(window, reinterpret_cast<XEvent&>(e), press ? ButtonPressMask : ButtonReleaseMask);
    }

    void SendFocusOut(unsigned long window, int mode, int detail) {
        XFocusChangeEvent e = {};
        e.type = FocusOut;
        e.display = display_;
        e.window = window;
        e.mode = mode;
        e.detail = detail;
        Send(window, reinterpret_cast<XEvent&>(e), FocusChangeMask);
    }

    void Motion(unsigned long window, int x, int y) {
        XMotionEvent e = {};
        e.type = MotionNotify;
        e.display = display_;
        e.window = window;
        e.root = DefaultRootWindow(display_);
        e.x = x;
        e.y = y;
        e.same_screen = True;
        Send(window, reinterpret_cast<XEvent&>(e), PointerMotionMask);
    }

private:
    void Send(unsigned long window, XEvent& e, long mask) {
        XSendEvent(display_, window, False, mask, &e);
        XSync(display_, False);
    }
    Display* display_;
};

void PumpFor(X11Window& window, InputState* input, int iterations = 20) {
    for (int i = 0; i < iterations; ++i) {
        window.PumpEvents(input);
        engine_placeholder::os::SleepNanos(5'000'000);
    }
}

// Creates a window and lets the desktop's own focus churn finish before a
// test injects input. A real window manager sends FocusIn/FocusOut around
// window creation (and when a previous test's window disappears); a real
// FocusOut legitimately clears held keys and modifiers, so injecting
// before it lands makes input tests flaky. Wait until the window is
// viewable, then drain events for a quiet period.
X11Window CreateSettled(const char* title, engine_placeholder::u32 w, engine_placeholder::u32 h) {
    X11Window window = X11Window::Create(title, w, h);
    if (!window.Valid()) {
        return window;
    }
    Display* display = static_cast<Display*>(window.NativeDisplay());
    PumpUntil(window, [&] {
        XWindowAttributes attrs;
        return XGetWindowAttributes(display, window.NativeWindow(), &attrs) != 0 && attrs.map_state == IsViewable;
    });
    InputState discard;
    for (int i = 0; i < 40; ++i) { // ~200ms of quiet
        window.PumpEvents(&discard);
        engine_placeholder::os::SleepNanos(5'000'000);
    }
    return window;
}

} // namespace

TEST_CASE("X11Window translates key press/release into Key via the server's keymap", "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("key test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    struct Case {
        KeySym sym;
        Key expected;
    };
    // Layout-independent: keycode is looked up *from* the keysym, so this
    // holds under whatever layout the server currently uses.
    const Case cases[] = {
        {XK_a, Key::A}, {XK_m, Key::M}, {XK_space, Key::Space}, {XK_Escape, Key::Escape},
        {XK_F1, Key::F1}, {XK_Left, Key::Left}, {XK_Return, Key::Enter}, {XK_1, Key::Num1},
    };
    for (const Case& c : cases) {
        unsigned int code = XKeysymToKeycode(inj.Dpy(), c.sym);
        if (code == 0) {
            continue; // not on this layout
        }
        inj.Key(window.NativeWindow(), code, 0, true);
        PumpFor(window, &input);
        INFO("keysym " << c.sym);
        CHECK(input.keyboard.IsDown(c.expected));
        inj.Key(window.NativeWindow(), code, 0, false);
        PumpFor(window, &input);
        CHECK_FALSE(input.keyboard.IsDown(c.expected));
    }
}

TEST_CASE("X11Window releases the Key a code was pressed as even if modifiers changed", "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("held key test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    unsigned int code = XKeysymToKeycode(inj.Dpy(), XK_a);
    REQUIRE(code != 0);
    inj.Key(window.NativeWindow(), code, 0, true);
    PumpFor(window, &input);
    REQUIRE(input.keyboard.IsDown(Key::A));
    // Release arrives with Shift now active (would translate to 'A' too,
    // but the contract is: clear what was pressed).
    inj.Key(window.NativeWindow(), code, ShiftMask, false);
    PumpFor(window, &input);
    REQUIRE_FALSE(input.keyboard.IsDown(Key::A));
    REQUIRE((input.keyboard.modifiers & engine_placeholder::os::kModShift) != 0);
}

TEST_CASE("X11Window maps event state bits to engine modifiers", "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("mods test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    unsigned int code = XKeysymToKeycode(inj.Dpy(), XK_a);
    REQUIRE(code != 0);
    inj.Key(window.NativeWindow(), code, ShiftMask | ControlMask | Mod1Mask | Mod4Mask, true);
    PumpFor(window, &input);
    using namespace engine_placeholder::os;
    REQUIRE(input.keyboard.modifiers == (kModShift | kModCtrl | kModAlt | kModSuper));
}

TEST_CASE("X11Window mirrors pointer position, buttons and scroll into InputState", "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    using namespace engine_placeholder::os;
    X11Window window = CreateSettled("pointer test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    inj.Motion(window.NativeWindow(), 37, 52);
    PumpFor(window, &input);
    REQUIRE(input.mouse_x == 37.0);
    REQUIRE(input.mouse_y == 52.0);
    REQUIRE(input.pointer_inside);

    inj.Button(window.NativeWindow(), Button1, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), Button3, 40, 50, Button1Mask, true);
    PumpFor(window, &input);
    REQUIRE(input.mouse_buttons == (kMouseLeft | kMouseRight));
    REQUIRE(input.mouse_x == 40.0);

    inj.Button(window.NativeWindow(), Button1, 40, 50, 0, false);
    PumpFor(window, &input);
    REQUIRE(input.mouse_buttons == kMouseRight);
    inj.Button(window.NativeWindow(), Button3, 40, 50, 0, false);
    inj.Button(window.NativeWindow(), Button2, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), 8, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), 9, 40, 50, 0, true);
    PumpFor(window, &input);
    REQUIRE(input.mouse_buttons == (kMouseMiddle | kMouseBack | kMouseForward));

    // Wheel: buttons 4/5 vertical, 6/7 horizontal; only presses count and
    // BeginFrame() clears the accumulation.
    input.BeginFrame();
    inj.Button(window.NativeWindow(), Button5, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), Button5, 40, 50, 0, false);
    inj.Button(window.NativeWindow(), Button5, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), Button4, 40, 50, 0, true);
    inj.Button(window.NativeWindow(), 7, 40, 50, 0, true);
    PumpFor(window, &input);
    REQUIRE(input.scroll_dy == 10.0); // down, down, up = +10 +10 -10
    REQUIRE(input.scroll_dx == 10.0);
    REQUIRE(input.mouse_buttons == (kMouseMiddle | kMouseBack | kMouseForward)); // wheel isn't a held button
    input.BeginFrame();
    REQUIRE(input.scroll_dy == 0.0);
}

TEST_CASE("X11Window input events are ignored (no crash) when no InputState is supplied", "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("no input state", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    unsigned int code = XKeysymToKeycode(inj.Dpy(), XK_a);
    inj.Key(window.NativeWindow(), code, 0, true);
    inj.Motion(window.NativeWindow(), 5, 5);
    inj.Button(window.NativeWindow(), Button1, 5, 5, 0, true);
    PumpFor(window, nullptr);
    REQUIRE_FALSE(window.ShouldClose());
}

// Manual/CI-configurable cross-layout check: run the server with a given
// layout (e.g. `Xvfb :97 & DISPLAY=:97 setxkbmap fr`) and set
// ENGINE_X11_TEST_LAYOUT=us|fr|de. Skipped when unset, because this
// machine's own layout isn't known to the test.
TEST_CASE("X11Window yields layout-correct Keys for physical keycodes (needs ENGINE_X11_TEST_LAYOUT)",
          "[x11][input][os][layout]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    const char* layout = std::getenv("ENGINE_X11_TEST_LAYOUT");
    if (layout == nullptr) {
        SKIP("ENGINE_X11_TEST_LAYOUT not set");
    }
    X11Window window = CreateSettled("layout test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    // X keycodes = evdev + 8: Q position 24, Z position 52, M position 58.
    struct Phys {
        unsigned int keycode;
        Key us, fr, de;
    };
    const Phys phys[] = {
        {24, Key::Q, Key::A, Key::Q}, // top-left letter
        {52, Key::Z, Key::W, Key::Y}, // bottom-left letter
        {58, Key::M, Key::Comma, Key::M},
    };
    for (const Phys& p : phys) {
        Key expected = std::string_view(layout) == "fr" ? p.fr : std::string_view(layout) == "de" ? p.de : p.us;
        inj.Key(window.NativeWindow(), p.keycode, 0, true);
        PumpFor(window, &input);
        INFO("layout " << layout << " keycode " << p.keycode);
        CHECK(input.keyboard.IsDown(expected));
        inj.Key(window.NativeWindow(), p.keycode, 0, false);
        PumpFor(window, &input);
        CHECK_FALSE(input.keyboard.IsDown(expected));
    }
}

TEST_CASE("X11Window releases keys and modifiers on a real focus loss but not during WM grabs",
          "[x11][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    using namespace engine_placeholder::os;
    X11Window window = CreateSettled("focus test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    unsigned int code = XKeysymToKeycode(inj.Dpy(), XK_w);
    REQUIRE(code != 0);
    inj.Key(window.NativeWindow(), code, ControlMask, true);
    PumpFor(window, &input);
    REQUIRE(input.keyboard.IsDown(Key::W));
    REQUIRE((input.keyboard.modifiers & kModCtrl) != 0);

    // Alt-Tab style transient grab: keys are still physically held.
    inj.SendFocusOut(window.NativeWindow(), NotifyGrab, NotifyAncestor);
    inj.SendFocusOut(window.NativeWindow(), NotifyWhileGrabbed, NotifyAncestor);
    inj.SendFocusOut(window.NativeWindow(), NotifyNormal, NotifyInferior);
    PumpFor(window, &input);
    REQUIRE(input.keyboard.IsDown(Key::W));
    REQUIRE((input.keyboard.modifiers & kModCtrl) != 0);

    // Genuine focus loss: releases would be missed, so clear everything.
    inj.SendFocusOut(window.NativeWindow(), NotifyNormal, NotifyNonlinear);
    PumpFor(window, &input);
    REQUIRE_FALSE(input.keyboard.IsDown(Key::W));
    REQUIRE(input.keyboard.modifiers == 0);
}

TEST_CASE("X11Window reports native info, and none once invalid or moved from", "[x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    using engine_placeholder::os::NativeWindowKind;
    X11Window none;
    REQUIRE(none.GetNativeInfo().kind == NativeWindowKind::Invalid);
    REQUIRE(none.GetNativeInfo().display == nullptr);
    REQUIRE(none.IsMinimized());  // zero area
    REQUIRE(none.ResizeSerial() == 0);

    X11Window window = X11Window::Create("native info", 320, 240);
    REQUIRE(window.Valid());
    auto info = window.GetNativeInfo();
    REQUIRE(info.kind == NativeWindowKind::Xlib);
    REQUIRE(info.display == static_cast<void*>(window.NativeDisplay()));
    REQUIRE(info.window_id == window.NativeWindow());
    REQUIRE(info.surface == nullptr);
    REQUIRE_FALSE(window.IsMinimized());

    X11Window moved(std::move(window));
    REQUIRE(moved.GetNativeInfo().window_id == info.window_id);
    REQUIRE(window.GetNativeInfo().kind == NativeWindowKind::Invalid);
}

TEST_CASE("X11Window's resize serial changes on every size change and never repeats", "[x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("serial test", 300, 200);
    REQUIRE(window.Valid());
    REQUIRE(window.ResizeSerial() != 0);

    // Let the window manager's initial configure burst finish: wait until the serial has been stable for a
    // while, rather than assuming a fixed number of pumps is enough.
    engine_placeholder::u64 settled = window.ResizeSerial();
    int stable = 0;
    for (int i = 0; i < 300 && stable < 20; ++i) {
        window.PumpEvents(nullptr);
        engine_placeholder::os::SleepNanos(5'000'000);
        if (window.ResizeSerial() == settled) {
            ++stable;
        } else {
            settled = window.ResizeSerial();
            stable = 0;
        }
    }
    const engine_placeholder::u32 start_w = window.Width();
    const engine_placeholder::u32 start_h = window.Height();

    Display* other = XOpenDisplay(nullptr);
    REQUIRE(other != nullptr);
    XResizeWindow(other, window.NativeWindow(), 411, 273);
    XSync(other, False);
    REQUIRE(PumpUntil(window, [&] { return window.Width() == 411 && window.Height() == 273; }));
    const engine_placeholder::u64 resized = window.ResizeSerial();
    REQUIRE(resized != settled);

    XResizeWindow(other, window.NativeWindow(), start_w, start_h);  // back to the size it started at
    XSync(other, False);
    XCloseDisplay(other);
    REQUIRE(PumpUntil(window, [&] { return window.Width() == start_w && window.Height() == start_h; }));
    REQUIRE(window.ResizeSerial() != resized);
    REQUIRE(window.ResizeSerial() != settled);  // a serial never repeats, even for a size seen before

    // Serials are process-wide: two windows never report the same value.
    X11Window second = X11Window::Create("serial test 2", 300, 200);
    REQUIRE(second.Valid());
    REQUIRE(second.ResizeSerial() != window.ResizeSerial());

    X11Window moved(std::move(window));
    REQUIRE(moved.ResizeSerial() != 0);
    REQUIRE(window.ResizeSerial() == 0);
    REQUIRE(window.IsMinimized());
}

TEST_CASE("X11Window reports itself minimized while its window is unmapped", "[x11][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("unmap test", 300, 200);
    REQUIRE(window.Valid());
    PumpUntil(window, [] { return false; });
    REQUIRE_FALSE(window.IsMinimized());

    // A window manager iconifies a window by unmapping it. Synthetic UnmapNotify/MapNotify events deliver the
    // same notifications without depending on the window manager or on the window having become viewable.
    auto send = [&](int type) {
        XEvent e = {};
        e.type = type;
        if (type == UnmapNotify) {
            e.xunmap.event = window.NativeWindow();
            e.xunmap.window = window.NativeWindow();
        } else {
            e.xmap.event = window.NativeWindow();
            e.xmap.window = window.NativeWindow();
        }
        XSendEvent(window.NativeDisplay(), window.NativeWindow(), False, StructureNotifyMask, &e);
        XFlush(window.NativeDisplay());
    };
    send(UnmapNotify);
    REQUIRE(PumpUntil(window, [&] { return window.IsMinimized(); }));
    send(MapNotify);
    REQUIRE(PumpUntil(window, [&] { return !window.IsMinimized(); }));

    // An unmap of some other window does not count.
    XEvent other = {};
    other.type = UnmapNotify;
    other.xunmap.event = window.NativeWindow();
    other.xunmap.window = window.NativeWindow() + 1;
    XSendEvent(window.NativeDisplay(), window.NativeWindow(), False, StructureNotifyMask, &other);
    XFlush(window.NativeDisplay());
    PumpUntil(window, [] { return false; });
    REQUIRE_FALSE(window.IsMinimized());
}

#include "unity/x11_disable.h"
