#include "lang/utility.h"
#include "containers/vector.h"

#include "lang/new.h"              // before the X11 headers: <X11/X.h> defines `BadAlloc`, which lang/new.h names
#include "platform/input_event.h"  // before the X11 headers: they define `None`
#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#undef CursorShape  // <X11/X.h> defines it as a number; the engine has os::CursorShape

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/linux/key_translate.h"
#include "platform/linux/x11/x11_window.h"
#include "os/os.h"

using engine_placeholder::os::x11::X11Window;

namespace
{

// Pumps `window` until `done()` or ~2s pass.
template <typename Pred> bool PumpUntil(X11Window& window, Pred done)
{
    for (int i = 0; i < 200; ++i)
    {
        window.PumpEvents(nullptr);
        if (done())
        {
            return true;
        }
        engine_placeholder::os::SleepNanos(10'000'000);
    }
    return false;
}

}  // namespace

TEST_CASE("X11Window::Create opens a mapped window on the real X server", "[x11][os]")
{
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
    REQUIRE(PumpUntil(window,
                      [&]
                      {
                          XWindowAttributes attrs;
                          return XGetWindowAttributes(display, window.NativeWindow(), &attrs) != 0 &&
                                 attrs.map_state == IsViewable;
                      }));
}

TEST_CASE("An X11 window follows the desktop's Xft.dpi scale, and a logical size is scaled to it", "[x11][os][scale]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("scale test", 640, 480);
    REQUIRE(window.Valid());
    // Read the resource database the way a toolkit does, independently of the window's own code path.
    Display* display = static_cast<Display*>(window.NativeDisplay());
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    engine_placeholder::byte* data = nullptr;
    float expected = 1.0f;
    if (XGetWindowProperty(display, RootWindow(display, DefaultScreen(display)),
                           XInternAtom(display, "RESOURCE_MANAGER", False), 0, 16384, False, XA_STRING, &type, &format,
                           &count, &after, &data) == Success &&
        data != nullptr)
    {
        expected = engine_placeholder::os::x11::ContentScaleFromResources(
            engine_placeholder::StringView(reinterpret_cast<const char*>(data), count));
        XFree(data);
    }
    CHECK(window.ContentScale() == expected);
    CHECK(window.ContentScale() >= 1.0f);
    CHECK(window.ContentScale() <= 4.0f);
    // The default unit is device pixels: exactly the size asked for, whatever the desktop scale is.
    CHECK(window.Width() == 640);
    CHECK(window.Height() == 480);

    X11Window scaled = X11Window::Create("scale test logical", 320, 241,
                                         engine_placeholder::os::x11::SizeUnits::Logical);
    REQUIRE(scaled.Valid());
    CHECK(scaled.ContentScale() == expected);
    CHECK(scaled.Width() == static_cast<engine_placeholder::u32>(320.0f * expected + 0.5f));
    CHECK(scaled.Height() == static_cast<engine_placeholder::u32>(241.0f * expected + 0.5f));
    // A moved window keeps its scale.
    X11Window moved(static_cast<X11Window&&>(scaled));
    CHECK(moved.ContentScale() == expected);
}

TEST_CASE("X11Window::Create fails gracefully for a zero-sized window", "[x11][os]")
{
    X11Window window = X11Window::Create("zero", 0, 480);
    REQUIRE_FALSE(window.Valid());
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("A default-constructed X11Window is invalid and pumping it is a no-op", "[x11][os]")
{
    X11Window window;
    REQUIRE_FALSE(window.Valid());
    window.PumpEvents(nullptr);
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("X11Window sets ShouldClose when the WM sends WM_DELETE_WINDOW", "[x11][os]")
{
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

TEST_CASE("X11Window ignores unrelated ClientMessages", "[x11][os]")
{
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

    for (int i = 0; i < 20; ++i)
    {
        window.PumpEvents(nullptr);
        engine_placeholder::os::SleepNanos(5'000'000);
    }
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("X11Window tracks resizes from ConfigureNotify", "[x11][os]")
{
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

TEST_CASE("X11Window move transfers ownership without double-close", "[x11][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window a = X11Window::Create("move test", 200, 200);
    REQUIRE(a.Valid());
    unsigned long native = a.NativeWindow();

    X11Window b(engine_placeholder::Move(a));
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(b.NativeWindow() == native);

    X11Window c;
    c = engine_placeholder::Move(b);
    REQUIRE_FALSE(b.Valid());
    REQUIRE(c.Valid());
    REQUIRE(c.NativeWindow() == native);
}

// ---- Day 25: input ----------------------------------------------------

#include <X11/keysym.h>

#include "os/console.h"
#include "string_view.h"

#include "platform/input.h"

using engine_placeholder::os::InputState;
using engine_placeholder::os::Key;
using engine_placeholder::os::KeyFromKeysym;

namespace
{

// A second client connection that injects synthetic input events into a
// window, the way a real server would deliver them.
class Injector
{
public:
    Injector() : display_(XOpenDisplay(nullptr)) {}
    ~Injector()
    {
        if (display_ != nullptr)
        {
            XCloseDisplay(display_);
        }
    }
    bool Valid() const { return display_ != nullptr; }
    Display* Dpy() const { return display_; }

    void Key(unsigned long window, unsigned int keycode, unsigned int state, bool press)
    {
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

    void Button(unsigned long window, unsigned int button, int x, int y, unsigned int state, bool press)
    {
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

    void SendFocusIn(unsigned long window)
    {
        XFocusChangeEvent e = {};
        e.type = FocusIn;
        e.display = display_;
        e.window = window;
        e.mode = NotifyNormal;
        e.detail = NotifyNonlinear;
        Send(window, reinterpret_cast<XEvent&>(e), FocusChangeMask);
    }

    void SendFocusOut(unsigned long window, int mode, int detail)
    {
        XFocusChangeEvent e = {};
        e.type = FocusOut;
        e.display = display_;
        e.window = window;
        e.mode = mode;
        e.detail = detail;
        Send(window, reinterpret_cast<XEvent&>(e), FocusChangeMask);
    }

    void Motion(unsigned long window, int x, int y)
    {
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
    void Send(unsigned long window, XEvent& e, long mask)
    {
        XSendEvent(display_, window, False, mask, &e);
        XSync(display_, False);
    }
    Display* display_;
};

void PumpFor(X11Window& window, InputState* input, int iterations = 20)
{
    for (int i = 0; i < iterations; ++i)
    {
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
X11Window CreateSettled(const char* title, engine_placeholder::u32 w, engine_placeholder::u32 h)
{
    X11Window window = X11Window::Create(title, w, h);
    if (!window.Valid())
    {
        return window;
    }
    Display* display = static_cast<Display*>(window.NativeDisplay());
    PumpUntil(window,
              [&]
              {
                  XWindowAttributes attrs;
                  return XGetWindowAttributes(display, window.NativeWindow(), &attrs) != 0 &&
                         attrs.map_state == IsViewable;
              });
    InputState discard;
    for (int i = 0; i < 40; ++i)
    {  // ~200ms of quiet
        window.PumpEvents(&discard);
        engine_placeholder::os::SleepNanos(5'000'000);
    }
    return window;
}

}  // namespace

TEST_CASE("X11Window translates key press/release into Key via the server's keymap", "[x11][input][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("key test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    struct Case
    {
        KeySym sym;
        Key expected;
    };
    // Layout-independent: keycode is looked up *from* the keysym, so this
    // holds under whatever layout the server currently uses.
    const Case cases[] = {
        {XK_a, Key::A},   {XK_m, Key::M},       {XK_space, Key::Space},  {XK_Escape, Key::Escape},
        {XK_F1, Key::F1}, {XK_Left, Key::Left}, {XK_Return, Key::Enter}, {XK_1, Key::Num1},
    };
    for (const Case& c : cases)
    {
        unsigned int code = XKeysymToKeycode(inj.Dpy(), c.sym);
        if (code == 0)
        {
            continue;  // not on this layout
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

TEST_CASE("X11Window releases the Key a code was pressed as even if modifiers changed", "[x11][input][os]")
{
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

TEST_CASE("X11Window maps event state bits to engine modifiers", "[x11][input][os]")
{
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

TEST_CASE("X11Window mirrors pointer position, buttons and scroll into InputState", "[x11][input][os]")
{
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
    REQUIRE(input.scroll_dy == 10.0);  // down, down, up = +10 +10 -10
    REQUIRE(input.scroll_dx == 10.0);
    REQUIRE(input.mouse_buttons == (kMouseMiddle | kMouseBack | kMouseForward));  // wheel isn't a held button
    input.BeginFrame();
    REQUIRE(input.scroll_dy == 0.0);
}

TEST_CASE("X11Window input events are ignored (no crash) when no InputState is supplied", "[x11][input][os]")
{
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
          "[x11][input][os][layout]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    const char* layout = engine_placeholder::os::GetEnvironment("ENGINE_X11_TEST_LAYOUT");
    if (layout == nullptr)
    {
        SKIP("ENGINE_X11_TEST_LAYOUT not set");
    }
    X11Window window = CreateSettled("layout test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;

    // X keycodes = evdev + 8: Q position 24, Z position 52, M position 58.
    struct Phys
    {
        unsigned int keycode;
        Key us, fr, de;
    };
    const Phys phys[] = {
        {24, Key::Q, Key::A, Key::Q},  // top-left letter
        {52, Key::Z, Key::W, Key::Y},  // bottom-left letter
        {58, Key::M, Key::Comma, Key::M},
    };
    for (const Phys& p : phys)
    {
        Key expected = engine_placeholder::StringView(layout) == engine_placeholder::StringView("fr")   ? p.fr
                       : engine_placeholder::StringView(layout) == engine_placeholder::StringView("de") ? p.de
                                                                                                        : p.us;
        inj.Key(window.NativeWindow(), p.keycode, 0, true);
        PumpFor(window, &input);
        INFO("layout " << layout << " keycode " << p.keycode);
        CHECK(input.keyboard.IsDown(expected));
        inj.Key(window.NativeWindow(), p.keycode, 0, false);
        PumpFor(window, &input);
        CHECK_FALSE(input.keyboard.IsDown(expected));
    }
}

TEST_CASE("X11Window releases keys and modifiers on a real focus loss but not during WM grabs", "[x11][input][os]")
{
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

TEST_CASE("X11Window reports native info, and none once invalid or moved from", "[x11][os]")
{
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

    X11Window moved(engine_placeholder::Move(window));
    REQUIRE(moved.GetNativeInfo().window_id == info.window_id);
    REQUIRE(window.GetNativeInfo().kind == NativeWindowKind::Invalid);
}

TEST_CASE("X11Window's resize serial changes on every size change and never repeats", "[x11][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("serial test", 300, 200);
    REQUIRE(window.Valid());
    REQUIRE(window.ResizeSerial() != 0);

    // Let the window manager's initial configure burst finish: wait until the serial has been stable for a
    // while, rather than assuming a fixed number of pumps is enough.
    engine_placeholder::u64 settled = window.ResizeSerial();
    int stable = 0;
    for (int i = 0; i < 300 && stable < 20; ++i)
    {
        window.PumpEvents(nullptr);
        engine_placeholder::os::SleepNanos(5'000'000);
        if (window.ResizeSerial() == settled)
        {
            ++stable;
        }
        else
        {
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

    X11Window moved(engine_placeholder::Move(window));
    REQUIRE(moved.ResizeSerial() != 0);
    REQUIRE(window.ResizeSerial() == 0);
    REQUIRE(window.IsMinimized());
}

TEST_CASE("X11Window reports itself minimized while its window is unmapped", "[x11][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = X11Window::Create("unmap test", 300, 200);
    REQUIRE(window.Valid());
    PumpUntil(window, [] { return false; });
    REQUIRE_FALSE(window.IsMinimized());

    // A window manager iconifies a window by unmapping it. Synthetic UnmapNotify/MapNotify events deliver the
    // same notifications without depending on the window manager or on the window having become viewable.
    auto send = [&](int type)
    {
        XEvent e = {};
        e.type = type;
        if (type == UnmapNotify)
        {
            e.xunmap.event = window.NativeWindow();
            e.xunmap.window = window.NativeWindow();
        }
        else
        {
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

// ---- Day 185: the input event stream -------------------------------------------------------------------

namespace
{

using engine_placeholder::os::InputEvent;
using engine_placeholder::os::InputEventQueue;
using engine_placeholder::os::InputEventType;

engine_placeholder::Vector<InputEvent> PollAll(X11Window& window)
{
    engine_placeholder::Vector<InputEvent> out;
    InputEvent e;
    while (window.PollEvent(&e))
    {
        out.Push(e);
    }
    return out;
}

}  // namespace

TEST_CASE("X11Window queues key, text, pointer and wheel events in order", "[x11][input][events][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("events test", 320, 240);
    REQUIRE(window.Valid());
    PollAll(window);
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;
    const unsigned long w = window.NativeWindow();

    const unsigned int a = XKeysymToKeycode(inj.Dpy(), XK_a);
    REQUIRE(a != 0);
    inj.Key(w, a, 0, true);
    inj.Key(w, a, 0, false);
    inj.Motion(w, 33, 44);
    inj.Button(w, 1, 10, 20, 0, true);
    inj.Button(w, 1, 10, 20, 0, false);
    inj.Button(w, 5, 10, 20, 0, true);  // wheel down
    inj.Button(w, 5, 10, 20, 0, false);
    inj.Button(w, 4, 10, 20, 0, true);  // wheel up
    inj.Button(w, 7, 10, 20, 0, true);  // wheel right
    PumpFor(window, &input);
    const auto ev = PollAll(window);

    REQUIRE(ev.Size() >= 9);
    CHECK(ev[0].type == InputEventType::KeyDown);
    CHECK(ev[0].key == Key::A);
    CHECK(ev[1].type == InputEventType::Text);
    CHECK(ev[1].text_length == 1);
    CHECK(ev[1].text[0] == 'a');
    CHECK(ev[2].type == InputEventType::KeyUp);
    CHECK(ev[3].type == InputEventType::PointerMove);
    CHECK(ev[3].x == 33.0);
    CHECK(ev[3].y == 44.0);
    CHECK(ev[4].type == InputEventType::PointerButton);
    CHECK(ev[4].down);
    CHECK(ev[4].button == engine_placeholder::os::kMouseLeft);
    CHECK(ev[5].type == InputEventType::PointerButton);
    CHECK_FALSE(ev[5].down);
    // The wheel presses are wheel events, never buttons; down is positive, as in InputState.
    CHECK(ev[6].type == InputEventType::Wheel);
    CHECK(ev[6].y == 1.0);
    CHECK(ev[7].type == InputEventType::Wheel);
    CHECK(ev[7].y == -1.0);
    CHECK(ev[8].type == InputEventType::Wheel);
    CHECK(ev[8].x == 1.0);
    // The polled state agrees with the stream (InputState counts ten units per notch; the net wheel is right by one).
    CHECK(input.scroll_dy == 0.0);
    CHECK(input.scroll_dx == 10.0);
    CHECK(input.mouse_x == 10.0);    // the last pointer position the server reported: the button events at (10, 20)
    CHECK(PollAll(window).Empty());  // nothing is delivered twice
}

TEST_CASE("X11Window queues KeyUp for held keys before FocusLost, and FocusGained on focus in",
          "[x11][input][events][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("focus events test", 320, 240);
    REQUIRE(window.Valid());
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;
    const unsigned long w = window.NativeWindow();

    inj.SendFocusIn(w);
    PumpFor(window, &input);
    PollAll(window);
    inj.Key(w, XKeysymToKeycode(inj.Dpy(), XK_a), 0, true);
    PumpFor(window, &input);
    PollAll(window);
    inj.SendFocusOut(w, NotifyNormal, NotifyNonlinear);
    PumpFor(window, &input);
    auto ev = PollAll(window);
    REQUIRE(ev.Size() >= 2);
    CHECK(ev[ev.Size() - 2].type == InputEventType::KeyUp);
    CHECK(ev[ev.Size() - 1].type == InputEventType::FocusLost);
    CHECK_FALSE(input.keyboard.IsDown(Key::A));
    inj.SendFocusIn(w);
    PumpFor(window, &input);
    ev = PollAll(window);
    REQUIRE_FALSE(ev.Empty());
    CHECK(ev[ev.Size() - 1].type == InputEventType::FocusGained);
}

TEST_CASE("X11Window queues a Resize event with the new size", "[x11][input][events][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("resize events test", 300, 200);
    REQUIRE(window.Valid());
    PollAll(window);
    Display* other = XOpenDisplay(nullptr);
    REQUIRE(other != nullptr);
    XResizeWindow(other, window.NativeWindow(), 411, 273);
    XSync(other, False);
    XCloseDisplay(other);
    InputState input;
    PumpFor(window, &input, 40);
    bool found = false;
    const auto resized = PollAll(window);
    for (engine_placeholder::usize i = 0; i < resized.Size(); ++i)
    {
        if (resized[i].type == InputEventType::Resize && resized[i].width == 411 && resized[i].height == 273)
        {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("X11Window drops and counts events when the program never polls", "[x11][input][events][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("overflow events test", 320, 240);
    REQUIRE(window.Valid());
    PollAll(window);
    Injector inj;
    REQUIRE(inj.Valid());
    InputState input;
    for (int i = 0; i < 1100; ++i)
    {
        inj.Motion(window.NativeWindow(), i % 300, 5);
        if (i % 100 == 99)
        {
            PumpFor(window, &input, 2);
        }
    }
    PumpFor(window, &input, 10);
    CHECK(window.DroppedEvents() > 0);
    CHECK(PollAll(window).Size() == InputEventQueue::kCapacity);
}

// ---- Day 186: cursor shapes, custom cursor images and the pointer lock ---------------------------------

namespace
{

engine_placeholder::Vector<engine_placeholder::byte> CursorPixels(unsigned int w, unsigned int h)
{
    engine_placeholder::Vector<engine_placeholder::byte> px;
    px.Resize(static_cast<engine_placeholder::usize>(w) * h * 4);
    for (engine_placeholder::usize i = 0; i < px.Size(); i += 4)
    {
        px[i] = 0x40;
        px[i + 1] = 0x20;
        px[i + 2] = 0x10;
        px[i + 3] = 0x80;  // premultiplied
    }
    return px;
}

engine_placeholder::os::CursorImage CursorImageOf(const engine_placeholder::Vector<engine_placeholder::byte>& px,
                                                  unsigned int w, unsigned int h)
{
    engine_placeholder::os::CursorImage image;
    image.rgba = px.Data();
    image.width = w;
    image.height = h;
    return image;
}

// True if another client can take the pointer grab (so this window does not hold it); releases the grab at once.
bool OtherClientCanGrab(Injector& other, unsigned long window)
{
    const int result = XGrabPointer(other.Dpy(), window, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None,
                                    None, CurrentTime);
    if (result == GrabSuccess)
    {
        XUngrabPointer(other.Dpy(), CurrentTime);
        XSync(other.Dpy(), False);
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("X11Window sets every cursor shape, repeatedly, without leaking", "[x11][cursor][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("cursor shapes", 200, 150);
    REQUIRE(window.Valid());
    REQUIRE(window.SupportsCursorShape());
    using engine_placeholder::os::CursorShape;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < static_cast<int>(CursorShape::Count); ++i)
        {
            INFO("shape " << i);
            CHECK(window.SetCursor(static_cast<CursorShape>(i)));
        }
    }
    const unsigned int made = window.CursorObjectCount();
    CHECK(made <=
          static_cast<unsigned int>(CursorShape::Count));  // one cursor per shape at most: the second pass made none
    CHECK_FALSE(window.SetCursor(CursorShape::Count));
    CHECK(window.SetCursor(CursorShape::Arrow));
    CHECK(window.CursorObjectCount() == made);
}

TEST_CASE("X11Window sets, replaces and clears a custom cursor image", "[x11][cursor][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("cursor image", 200, 150);
    REQUIRE(window.Valid());
    REQUIRE(window.SupportsCursorImage());
    const unsigned int base = window.CursorObjectCount();
    const unsigned int sizes[] = {1, 32, 128};
    unsigned int expected = base;
    bool first = true;
    for (unsigned int size : sizes)
    {
        const auto px = CursorPixels(size, size);
        INFO("size " << size);
        CHECK(window.SetCursorImage(CursorImageOf(px, size, size)));
        if (first)
        {
            ++expected;
            first = false;
        }  // the first image adds one cursor; each later one replaces it
        CHECK(window.CursorObjectCount() >= expected);
        CHECK(window.CursorObjectCount() <= expected + 1);  // the blank cursor may be made once
    }
    const unsigned int with_image = window.CursorObjectCount();
    const auto px = CursorPixels(128, 128);
    CHECK(window.SetCursorImage(CursorImageOf(px, 128, 128)));  // the same image again makes no new cursor
    CHECK(window.CursorObjectCount() == with_image);
    CHECK(window.SetCursor(engine_placeholder::os::CursorShape::Hand));  // a shape change keeps the image
    window.ClearCursorImage();
    CHECK(window.CursorObjectCount() <= with_image);  // the image's cursor is freed; the Hand cursor the image was
                                                      // hiding is made now, so no more than before
    window.ClearCursorImage();                        // clearing twice is fine
    InputState input;
    Injector inj;
    REQUIRE(inj.Valid());
    inj.SendFocusIn(window.NativeWindow());  // a focus change after the image changes nothing bad
    PumpFor(window, &input);
}

TEST_CASE("X11Window refuses invalid cursor images and keeps the old one", "[x11][cursor][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("cursor image refused", 200, 150);
    REQUIRE(window.Valid());
    const auto good = CursorPixels(16, 16);
    REQUIRE(window.SetCursorImage(CursorImageOf(good, 16, 16)));
    const unsigned int count = window.CursorObjectCount();
    const auto big = CursorPixels(129, 8);
    CHECK_FALSE(window.SetCursorImage(CursorImageOf(big, 129, 8)));
    auto outside = CursorImageOf(good, 16, 16);
    outside.hotspot_x = 16;
    CHECK_FALSE(window.SetCursorImage(outside));
    engine_placeholder::os::CursorImage none;
    none.width = 4;
    none.height = 4;
    CHECK_FALSE(window.SetCursorImage(none));
    CHECK(window.CursorObjectCount() == count);
}

TEST_CASE("X11Window takes the pointer grab on request, releases it, and releases it when destroyed",
          "[x11][cursor][lock][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    Injector other;
    REQUIRE(other.Valid());
    unsigned long native = 0;
    {
        X11Window window = CreateSettled("pointer lock", 200, 150);
        REQUIRE(window.Valid());
        REQUIRE(window.SupportsPointerLock());
        native = window.NativeWindow();
        InputState input;
        Injector inj;
        REQUIRE(inj.Valid());

        // Without focus the request is remembered, not applied.
        inj.SendFocusOut(native, NotifyNormal, NotifyNonlinear);
        PumpFor(window, &input);
        CHECK(window.SetPointerLock(true));
        CHECK_FALSE(window.PointerLocked());
        CHECK(OtherClientCanGrab(other, native));

        inj.SendFocusIn(native);
        PumpFor(window, &input);
        if (!window.PointerLocked())
        {
            SKIP(
                "the X server refused the pointer grab (a compositor may not grant grabs to a window it did not focus)");
        }
        CHECK_FALSE(OtherClientCanGrab(other, native));  // this window holds the grab
        CHECK(window.SetPointerLock(true));              // a second request is a no-op
        CHECK(window.PointerLocked());

        // Losing focus releases; regaining it takes the grab again while still wanted.
        inj.SendFocusOut(native, NotifyNormal, NotifyNonlinear);
        PumpFor(window, &input);
        CHECK_FALSE(window.PointerLocked());
        CHECK(OtherClientCanGrab(other, native));
        inj.SendFocusIn(native);
        PumpFor(window, &input);
        CHECK(window.PointerLocked());

        CHECK(window.SetPointerLock(false));
        CHECK_FALSE(window.PointerLocked());
        CHECK(OtherClientCanGrab(other, native));

        CHECK(window.SetPointerLock(true));  // and leave it locked: destroying the window must release it
        CHECK(window.PointerLocked());
    }
    XSync(other.Dpy(), False);
    const int result = XGrabPointer(other.Dpy(), DefaultRootWindow(other.Dpy()), False, ButtonPressMask, GrabModeAsync,
                                    GrabModeAsync, None, None, CurrentTime);
    CHECK(result == GrabSuccess);
    XUngrabPointer(other.Dpy(), CurrentTime);
    XSync(other.Dpy(), False);
}

TEST_CASE("X11Window reports RelativeMotion only while the pointer is locked", "[x11][cursor][lock][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window window = CreateSettled("relative motion", 200, 150);
    REQUIRE(window.Valid());
    PollAll(window);
    InputState input;
    Injector inj;
    REQUIRE(inj.Valid());
    inj.Motion(window.NativeWindow(), 50, 50);
    PumpFor(window, &input);
    const auto moves = PollAll(window);
    for (engine_placeholder::usize i = 0; i < moves.Size(); ++i)
    {
        CHECK(moves[i].type != InputEventType::RelativeMotion);  // unlocked: never sent
    }
}

#include "unity/x11_disable.h"

// Day 187: a program that asks for the clipboard and exits before the answer must not take the owner down with it
// (Xlib's default error handler ends the process on the BadWindow of the answer's destination).
TEST_CASE("X11Window survives a clipboard requestor that vanished before the answer", "[x11][clipboard][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    X11Window owner = X11Window::Create("clipboard owner", 200, 100);
    REQUIRE(owner.Valid());
    const char text[] = "still here";
    engine_placeholder::os::ClipboardOffer offer;
    offer.data = reinterpret_cast<const engine_placeholder::byte*>(text);
    offer.size = sizeof(text) - 1;
    REQUIRE(owner.SetClipboard(engine_placeholder::os::ClipboardKind::Clipboard, offer));
    for (int round = 0; round < 3; ++round)
    {
        Display* peer = XOpenDisplay(nullptr);
        REQUIRE(peer != nullptr);
        const Window window = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0, 0, 10, 10, 0, 0, 0);
        XConvertSelection(peer, XInternAtom(peer, "CLIPBOARD", 0), XInternAtom(peer, "UTF8_STRING", 0),
                          XInternAtom(peer, "PEER_PROP", 0), window, 0 /* CurrentTime */);
        XFlush(peer);
        XCloseDisplay(peer);  // the request is on its way; its sender is gone
        for (int i = 0; i < 20; ++i)
        {
            owner.PumpEvents(nullptr);
            engine_placeholder::os::SleepNanos(5'000'000);
        }
        REQUIRE(owner.Valid());
    }
}
