// The Win32 input event stream (Phase 15 Day 205): a real window fed the messages Windows sends, checked against the
// stream the Linux backends produce (`engine/platform/BACKEND.md`): keys with repeat and the modifiers held before
// them, text from WM_CHAR (a surrogate pair joined, control characters and Ctrl dropped), the pointer's enter, moves
// and leave, buttons, the wheel in notches, focus with the release of what was held, resize, and the overflow counter.
// The messages are posted to the window rather than typed with SendInput (which goes to whatever window has the focus).
// Dead keys are composed by the system's TranslateMessage; testing one would need a layout loaded into the user's
// layout list, which is a change to the system's settings, so that check is left to a person (the Day 205 by-eye list).
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "display_session.h"
#include "os/windows/win32.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/windows/win32_window.h"
#include "string_view.h"
#include "lang/initializer_list.h"

using namespace vse;
using os::InputEvent;
using os::InputEventType;
using os::Key;
using os::win32::Win32Window;

namespace
{

// The lParam of a key message: repeat count 1, the scan code, the extended flag, the previous state (bit 30) and the
// transition (bit 31).
LPARAM EvKeyParam(u32 scan, bool released, bool repeat)
{
    u32 p = 1u | ((scan & 0xFFu) << 16);
    if (released || repeat)
    {
        p |= 1u << 30;
    }
    if (released)
    {
        p |= 1u << 31;
    }
    return static_cast<LPARAM>(p);
}

void EvPostKey(HWND hwnd, bool down, u32 vk, u32 scan, bool repeat = false)
{
    PostMessageW(hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, EvKeyParam(scan, !down, repeat));
}

LPARAM EvPoint(i32 x, i32 y)
{
    return MAKELPARAM(static_cast<u16>(static_cast<i16>(x)), static_cast<u16>(static_cast<i16>(y)));
}

struct EventWindow
{
    os::InputState input;
    Win32Window window;
    bool Open() { return window.Open("event test", 320, 240, &input); }
    HWND Hwnd() const { return window.NativeHandle(); }
    // Pumps, then takes every queued event.
    Vector<InputEvent> Drain()
    {
        window.PumpEvents();
        Vector<InputEvent> out;
        InputEvent e;
        while (window.PollEvent(&e))
        {
            out.Push(e);
        }
        return out;
    }
    // Drains and keeps only the events of these types (the system adds focus, enter and leave events of its own while a
    // test runs).
    Vector<InputEvent> Only(InitializerList<InputEventType> types)
    {
        Vector<InputEvent> all = Drain();
        Vector<InputEvent> out;
        for (usize i = 0; i < all.Size(); ++i)
        {
            for (InputEventType t : types)
            {
                if (all[i].type == t)
                {
                    out.Push(all[i]);
                    break;
                }
            }
        }
        return out;
    }
};

StringView TextOf(const InputEvent& e)
{
    return StringView(e.text, e.text_length);
}

}  // namespace

TEST_CASE("Windows events: the stream starts with the window's size and a resize is reported",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    InputEvent first;
    REQUIRE(w.window.PollEvent(&first));  // before any pump: queued by Open
    CHECK(first.type == InputEventType::Resize);
    CHECK(first.width == w.window.Width());
    CHECK(first.height == w.window.Height());
    CHECK(first.time_ns != 0);
    w.Drain();
    RECT outer{};
    GetWindowRect(w.Hwnd(), &outer);
    SetWindowPos(w.Hwnd(), nullptr, 0, 0, outer.right - outer.left + 40, outer.bottom - outer.top + 20,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const Vector<InputEvent> events = w.Only({InputEventType::Resize});
    REQUIRE(events.Size() == 1);
    CHECK(events[0].width == w.window.Width());
    CHECK(events[0].height == w.window.Height());
}

TEST_CASE("Windows events: keys go down, repeat and up with the modifiers held before them", "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    EvPostKey(w.Hwnd(), true, VK_SHIFT, 0x2A);     // left Shift
    EvPostKey(w.Hwnd(), true, VK_F5, 0x3F);        // F5 with Shift held
    EvPostKey(w.Hwnd(), true, VK_F5, 0x3F, true);  // auto-repeat
    EvPostKey(w.Hwnd(), true, VK_F5, 0x3F, true);
    EvPostKey(w.Hwnd(), false, VK_F5, 0x3F);
    EvPostKey(w.Hwnd(), false, VK_SHIFT, 0x2A);
    const Vector<InputEvent> e = w.Only({InputEventType::KeyDown, InputEventType::KeyUp});
    REQUIRE(e.Size() == 6);
    CHECK(e[0].type == InputEventType::KeyDown);
    CHECK(e[0].key == Key::LeftShift);
    CHECK((e[0].modifiers & os::kModShift) == 0);  // held before it: nothing
    CHECK_FALSE(e[0].repeat);
    CHECK(e[1].key == Key::F5);
    CHECK_FALSE(e[1].repeat);
    CHECK((e[1].modifiers & os::kModShift) != 0);
    CHECK(e[2].key == Key::F5);
    CHECK(e[2].repeat);
    CHECK(e[3].repeat);
    CHECK(e[4].type == InputEventType::KeyUp);
    CHECK(e[4].key == Key::F5);
    CHECK(e[5].type == InputEventType::KeyUp);
    CHECK(e[5].key == Key::LeftShift);
    CHECK(e[0].time_ns <= e[5].time_ns);
}

TEST_CASE("Windows events: both Shift keys end with one release, and the stream says so for each",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    EvPostKey(w.Hwnd(), true, VK_SHIFT, 0x2A);
    EvPostKey(w.Hwnd(), true, VK_SHIFT, 0x36);
    EvPostKey(w.Hwnd(), false, VK_SHIFT,
              0x36);  // Windows sends no release for the first Shift let go, only for the last
    const Vector<InputEvent> e = w.Only({InputEventType::KeyUp});
    REQUIRE(e.Size() == 2);
    CHECK(((e[0].key == Key::RightShift && e[1].key == Key::LeftShift) ||
           (e[0].key == Key::LeftShift && e[1].key == Key::RightShift)));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftShift));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::RightShift));
}

TEST_CASE("Windows events: WM_CHAR becomes Text, a surrogate pair is one scalar, control characters and Ctrl give none",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    PostMessageW(w.Hwnd(), WM_CHAR, 'a', 1);
    PostMessageW(w.Hwnd(), WM_CHAR, 0x00E9, 1);  // é: two bytes
    PostMessageW(w.Hwnd(), WM_CHAR, 0x20AC, 1);  // €: three bytes
    PostMessageW(w.Hwnd(), WM_CHAR, 0xD83D, 1);  // U+1F600, high surrogate first
    PostMessageW(w.Hwnd(), WM_CHAR, 0xDE00, 1);
    PostMessageW(w.Hwnd(), WM_CHAR, 0xDE00, 1);  // a low surrogate alone: nothing
    PostMessageW(w.Hwnd(), WM_CHAR, '\r', 1);    // Enter, Tab, Backspace, Escape: keys, not text
    PostMessageW(w.Hwnd(), WM_CHAR, '\t', 1);
    PostMessageW(w.Hwnd(), WM_CHAR, '\b', 1);
    PostMessageW(w.Hwnd(), WM_CHAR, 0x1B, 1);
    PostMessageW(w.Hwnd(), WM_CHAR, 0x7F, 1);
    const Vector<InputEvent> e = w.Only({InputEventType::Text});
    REQUIRE(e.Size() == 4);
    CHECK(TextOf(e[0]) == StringView("a"));
    CHECK(TextOf(e[1]) == StringView("\xC3\xA9"));
    CHECK(TextOf(e[2]) == StringView("\xE2\x82\xAC"));
    CHECK(TextOf(e[3]) == StringView("\xF0\x9F\x98\x80"));

    // With Ctrl held a character is a shortcut (WM_CHAR then carries a control code anyway, but a printable one must
    // not slip through).
    EvPostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
    PostMessageW(w.Hwnd(), WM_CHAR, 'x', 1);
    EvPostKey(w.Hwnd(), false, VK_CONTROL, 0x1D);
    PostMessageW(w.Hwnd(), WM_CHAR, 'y', 1);
    const Vector<InputEvent> after = w.Only({InputEventType::Text});
    REQUIRE(after.Size() == 1);
    CHECK(TextOf(after[0]) == StringView("y"));
}

TEST_CASE("Windows events: Ctrl and Alt together are Windows' AltGr and type text; Alt alone types none",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    // Ctrl+Alt+Q on a German layout: WM_CHAR '@' with the left Ctrl and the left Alt held.
    EvPostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
    EvPostKey(w.Hwnd(), true, VK_MENU, 0x38);
    PostMessageW(w.Hwnd(), WM_CHAR, '@', 1);
    EvPostKey(w.Hwnd(), false, VK_CONTROL, 0x1D);
    PostMessageW(w.Hwnd(), WM_CHAR, 'z', 1);  // Alt alone: a shortcut, not text
    EvPostKey(w.Hwnd(), false, VK_MENU, 0x38);
    const Vector<InputEvent> e = w.Only({InputEventType::Text});
    REQUIRE(e.Size() == 1);
    CHECK(TextOf(e[0]) == StringView("@"));
    CHECK((e[0].modifiers & (os::kModCtrl | os::kModAlt)) == (os::kModCtrl | os::kModAlt));
}

TEST_CASE("Windows window: the content scale is the DPI over 96 and follows a DPI change", "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    CHECK(w.window.ContentScale() == 1.0f);  // not open
    REQUIRE(w.Open());
    REQUIRE(w.window.Dpi() != 0);
    CHECK(w.window.ContentScale() == static_cast<f32>(w.window.Dpi()) / 96.0f);
    // Windows moving the window to a 200% monitor: WM_DPICHANGED with the new DPI and the suggested outer rectangle.
    RECT outer{};
    GetWindowRect(w.Hwnd(), &outer);
    SendMessageW(w.Hwnd(), WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&outer));
    CHECK(w.window.Dpi() == 192);
    CHECK(w.window.ContentScale() == 2.0f);
    w.window.Close();
    CHECK(w.window.ContentScale() == 1.0f);
}

TEST_CASE("Windows events: the pointer enters, moves, presses, scrolls and leaves in order", "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    w.input.pointer_inside = false;  // whatever the real cursor did meanwhile
    PostMessageW(w.Hwnd(), WM_MOUSEMOVE, 0, EvPoint(10, 20));
    PostMessageW(w.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, EvPoint(10, 20));
    PostMessageW(w.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, EvPoint(10, 20));  // a second press of a held button: no event
    PostMessageW(w.Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<u16>(WHEEL_DELTA)), EvPoint(10, 20));
    PostMessageW(w.Hwnd(), WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<u16>(static_cast<i16>(-WHEEL_DELTA / 2))),
                 EvPoint(10, 20));
    PostMessageW(w.Hwnd(), WM_LBUTTONUP, 0, EvPoint(12, 22));
    PostMessageW(w.Hwnd(), WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), EvPoint(12, 22));
    PostMessageW(w.Hwnd(), WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), EvPoint(12, 22));
    PostMessageW(w.Hwnd(), WM_MOUSELEAVE, 0, 0);
    const Vector<InputEvent> e = w.Only({InputEventType::PointerEnter, InputEventType::PointerMove,
                                         InputEventType::PointerButton, InputEventType::Wheel,
                                         InputEventType::PointerLeave});
    REQUIRE(e.Size() >= 9);
    usize i = 0;
    CHECK(e[i].type == InputEventType::PointerEnter);
    CHECK(e[i].x == 10.0);
    CHECK(e[i].y == 20.0);
    ++i;
    CHECK(e[i].type == InputEventType::PointerMove);
    CHECK(e[i].x == 10.0);
    ++i;
    CHECK(e[i].type == InputEventType::PointerButton);
    CHECK(e[i].button == os::kMouseLeft);
    CHECK(e[i].down);
    CHECK(e[i].x == 10.0);  // a button event says where it happened (the UI clicks there)
    CHECK(e[i].y == 20.0);
    ++i;
    CHECK(e[i].type == InputEventType::Wheel);
    CHECK(e[i].y == -1.0);  // away from the user is up: negative
    CHECK(e[i].x == 0.0);
    ++i;
    CHECK(e[i].type == InputEventType::Wheel);
    CHECK(e[i].x == -0.5);  // tilted left half a notch
    ++i;
    CHECK(e[i].type == InputEventType::PointerButton);
    CHECK(e[i].button == os::kMouseLeft);
    CHECK_FALSE(e[i].down);
    CHECK(e[i].x == 12.0);
    CHECK(e[i].y == 22.0);
    ++i;
    CHECK(e[i].type == InputEventType::PointerButton);
    CHECK(e[i].button == os::kMouseForward);
    CHECK(e[i].down);
    ++i;
    CHECK(e[i].button == os::kMouseForward);
    CHECK_FALSE(e[i].down);
    // A leave follows (the posted WM_MOUSELEAVE, or Windows' own when it saw the real cursor elsewhere). The real mouse
    // moving over the window while the test runs adds moves of its own, so the leave is looked for after the buttons
    // rather than taken as the very last event.
    bool left = false;
    for (usize k = i + 1; k < e.Size(); ++k)
    {
        left = left || e[k].type == InputEventType::PointerLeave;
    }
    CHECK(left);
    CHECK(w.input.scroll_dy == -10.0);  // InputState keeps the Linux units
    CHECK(w.input.scroll_dx == -5.0);
}

TEST_CASE("Windows events: losing the focus releases every held key in the stream, then reports FocusLost",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    SendMessageW(w.Hwnd(), WM_SETFOCUS, 0, 0);
    EvPostKey(w.Hwnd(), true, VK_SHIFT, 0x2A);
    EvPostKey(w.Hwnd(), true, VK_F7, 0x41);
    w.window.PumpEvents();
    SendMessageW(w.Hwnd(), WM_KILLFOCUS, 0, 0);
    const Vector<InputEvent> e = w.Only(
        {InputEventType::FocusGained, InputEventType::FocusLost, InputEventType::KeyDown, InputEventType::KeyUp});
    // FocusGained may have come before (Open activates the window); what matters is the order at the end.
    REQUIRE(e.Size() >= 5);
    const usize n = e.Size();
    CHECK(e[n - 1].type == InputEventType::FocusLost);
    CHECK(e[n - 2].type == InputEventType::KeyUp);
    CHECK(e[n - 3].type == InputEventType::KeyUp);
    const bool both = (e[n - 2].key == Key::F7 || e[n - 3].key == Key::F7) &&
                      (e[n - 2].key == Key::LeftShift || e[n - 3].key == Key::LeftShift);
    CHECK(both);
    CHECK_FALSE(w.input.keyboard.IsDown(Key::F7));
    // A second loss without a gain in between reports nothing more.
    SendMessageW(w.Hwnd(), WM_KILLFOCUS, 0, 0);
    CHECK(w.Only({InputEventType::FocusLost, InputEventType::KeyUp}).Size() == 0);
    SendMessageW(w.Hwnd(), WM_SETFOCUS, 0, 0);
    const Vector<InputEvent> gained = w.Only({InputEventType::FocusGained});
    CHECK(gained.Size() == 1);
}

TEST_CASE("Windows events: a window opened without an InputState still reports its events", "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    Win32Window window;
    REQUIRE(window.Open("no input events", 320, 240, nullptr));
    PostMessageW(window.NativeHandle(), WM_CHAR, 'q', 1);
    PostMessageW(window.NativeHandle(), WM_LBUTTONDOWN, MK_LBUTTON, EvPoint(3, 4));
    PostMessageW(window.NativeHandle(), WM_LBUTTONUP, 0, EvPoint(3, 4));
    window.PumpEvents();
    usize text = 0;
    usize buttons = 0;
    InputEvent e;
    while (window.PollEvent(&e))
    {
        if (e.type == InputEventType::Text)
        {
            ++text;
        }
        if (e.type == InputEventType::PointerButton)
        {
            ++buttons;
        }
    }
    CHECK(text == 1);
    CHECK(buttons == 2);
}

TEST_CASE("Windows events: an undrained queue keeps the oldest events in order and counts the dropped ones",
          "[window][win32][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    w.Drain();
    const usize n = os::InputEventQueue::kCapacity + 200;
    for (usize i = 0; i < n; ++i)
    {
        SendMessageW(w.Hwnd(), WM_CHAR, 'a' + (i % 26), 1);
    }
    CHECK(w.window.DroppedEvents() > 0);
    usize seen = 0;
    usize in_order = 0;
    InputEvent e;
    while (w.window.PollEvent(&e))
    {
        if (e.type != InputEventType::Text)
        {
            continue;
        }
        if (e.text_length == 1 && e.text[0] == static_cast<char>('a' + (seen % 26)))
        {
            ++in_order;
        }
        ++seen;
    }
    CHECK(seen <= os::InputEventQueue::kCapacity);
    CHECK(in_order == seen);
}

// ---- the cursor and the pointer lock
// -------------------------------------------------------------------------------------------------

namespace
{

// Asks the window which cursor it shows over its client area, as Windows does when the pointer moves there.
HCURSOR EvCursorShown(HWND hwnd)
{
    SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    return GetCursor();
}

// A 2x2 premultiplied RGBA image of one colour and alpha.
struct EvImage
{
    byte rgba[16];
    os::CursorImage Make(byte r, byte g, byte b, byte a)
    {
        for (int i = 0; i < 4; ++i)
        {
            rgba[i * 4 + 0] = r;
            rgba[i * 4 + 1] = g;
            rgba[i * 4 + 2] = b;
            rgba[i * 4 + 3] = a;
        }
        os::CursorImage image;
        image.rgba = rgba;
        image.width = 2;
        image.height = 2;
        image.hotspot_x = 1;
        image.hotspot_y = 1;
        return image;
    }
};

// The lock clips the real desktop cursor: whatever a test does, the clip goes when the guard does.
struct EvClipGuard
{
    ~EvClipGuard() { ClipCursor(nullptr); }
};

bool EvClipIsPoint()
{
    RECT clip{};
    GetClipCursor(&clip);
    return clip.right - clip.left == 1 && clip.bottom - clip.top == 1;
}

}  // namespace

TEST_CASE("Windows cursor: every shape is the system's cursor, Hidden is none, and an out-of-range shape is refused",
          "[window][win32][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    CHECK(w.window.SupportsCursorShape());
    const struct
    {
        os::CursorShape shape;
        u32 id;
    } kShapes[] = {{os::CursorShape::Arrow, 32512},
                   {os::CursorShape::IBeam, 32513},
                   {os::CursorShape::Hand, 32649},
                   {os::CursorShape::ResizeHorizontal, 32644},
                   {os::CursorShape::ResizeVertical, 32645},
                   {os::CursorShape::ResizeDiagonalNWSE, 32642},
                   {os::CursorShape::ResizeDiagonalNESW, 32643},
                   {os::CursorShape::ResizeAll, 32646},
                   {os::CursorShape::NotAllowed, 32648}};
    for (const auto& s : kShapes)
    {
        INFO("shape " << static_cast<int>(s.shape));
        REQUIRE(w.window.SetCursor(s.shape));
        CHECK(EvCursorShown(w.Hwnd()) == LoadCursorW(nullptr, MAKEINTRESOURCEW(s.id)));
    }
    REQUIRE(w.window.SetCursor(os::CursorShape::Hidden));
    CHECK(EvCursorShown(w.Hwnd()) == nullptr);
    CHECK_FALSE(w.window.SetCursor(os::CursorShape::Count));
    CHECK(EvCursorShown(w.Hwnd()) == nullptr);  // unchanged
    // Over the frame the system's own cursors show: the window leaves WM_SETCURSOR to the default procedure there,
    // which shows the resize arrow.
    SendMessageW(w.Hwnd(), WM_SETCURSOR, reinterpret_cast<WPARAM>(w.Hwnd()), MAKELPARAM(HTLEFT, WM_MOUSEMOVE));
    CHECK(GetCursor() == LoadCursorW(nullptr, MAKEINTRESOURCEW(32644)));  // IDC_SIZEWE
}

TEST_CASE(
    "Windows cursor: a custom image shows, an equal one is not made again, a new one replaces it, and nothing leaks",
    "[window][win32][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EventWindow w;
    REQUIRE(w.Open());
    CHECK(w.window.SupportsCursorImage());
    EvImage a;
    EvImage b;
    REQUIRE(w.window.SetCursorImage(a.Make(128, 0, 0, 128)));
    CHECK(w.window.CursorObjects() == 1);
    const HCURSOR first = EvCursorShown(w.Hwnd());
    CHECK(first != nullptr);
    CHECK(first != LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    REQUIRE(w.window.SetCursorImage(a.Make(128, 0, 0, 128)));  // equal: the same cursor
    CHECK(EvCursorShown(w.Hwnd()) == first);
    REQUIRE(w.window.SetCursorImage(b.Make(0, 0, 255, 255)));
    CHECK(w.window.CursorObjects() == 1);
    CHECK(EvCursorShown(w.Hwnd()) != first);
    // The image outranks the shape; clearing it brings the shape back.
    REQUIRE(w.window.SetCursor(os::CursorShape::Hand));
    CHECK(EvCursorShown(w.Hwnd()) != LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
    w.window.ClearCursorImage();
    CHECK(w.window.CursorObjects() == 0);
    CHECK(EvCursorShown(w.Hwnd()) == LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
    // Invalid images are refused and change nothing.
    os::CursorImage bad = a.Make(1, 1, 1, 1);
    bad.hotspot_x = 5;
    CHECK_FALSE(w.window.SetCursorImage(bad));
    bad = a.Make(1, 1, 1, 1);
    bad.rgba = nullptr;
    CHECK_FALSE(w.window.SetCursorImage(bad));
    CHECK(w.window.CursorObjects() == 0);
    // Closing with an image showing destroys it.
    REQUIRE(w.window.SetCursorImage(a.Make(0, 255, 0, 255)));
    w.window.Close();
    CHECK(w.window.CursorObjects() == 0);
}

TEST_CASE("Windows lock: held only while focused, asked again on focus, released by unlock and by closing",
          "[window][win32][cursor][lock]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    EvClipGuard guard;
    EventWindow w;
    REQUIRE(w.Open());
    CHECK(w.window.SupportsPointerLock());
    SendMessageW(w.Hwnd(), WM_KILLFOCUS, 0, 0);  // start unfocused, whatever the desktop did
    REQUIRE(w.window.SetPointerLock(true));
    CHECK_FALSE(w.window.PointerLocked());  // waits for focus
    CHECK_FALSE(EvClipIsPoint());
    SendMessageW(w.Hwnd(), WM_SETFOCUS, 0, 0);
    CHECK(w.window.PointerLocked());
    CHECK(EvClipIsPoint());
    CHECK(EvCursorShown(w.Hwnd()) == nullptr);  // hidden while locked
    SendMessageW(w.Hwnd(), WM_KILLFOCUS, 0, 0);
    CHECK_FALSE(w.window.PointerLocked());
    CHECK_FALSE(EvClipIsPoint());
    SendMessageW(w.Hwnd(), WM_SETFOCUS, 0, 0);  // still wanted: asked for again
    CHECK(w.window.PointerLocked());
    REQUIRE(w.window.SetPointerLock(true));  // a second request is a no-op
    CHECK(w.window.PointerLocked());
    REQUIRE(w.window.SetPointerLock(false));
    CHECK_FALSE(w.window.PointerLocked());
    CHECK_FALSE(EvClipIsPoint());
    CHECK(EvCursorShown(w.Hwnd()) == LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    REQUIRE(w.window.SetPointerLock(true));
    CHECK(w.window.PointerLocked());
    w.window.Close();
    CHECK_FALSE(w.window.PointerLocked());
    CHECK_FALSE(EvClipIsPoint());
    CHECK_FALSE(w.window.SetPointerLock(true));  // a closed window refuses
}
