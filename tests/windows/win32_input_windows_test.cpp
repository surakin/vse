// Win32 input (Phase 13 Day 159): the key translation as a table, without a window, and a real window fed the messages
// Windows sends for keys, the mouse, the wheel and a focus loss. The messages are posted to the window rather than
// typed with SendInput: SendInput goes to whatever window has the focus on the desktop, which a test cannot be sure is
// its own. The tests use keys whose meaning does not depend on the keyboard layout (and skip the letter case on a
// layout without Latin letters).
#include <catch2/catch_test_macros.hpp>

#include "display_session.h"
#include "os/os.h"
#include "os/windows/win32.h"
#include "platform/input.h"
#include "platform/windows/win32_keys.h"
#include "platform/windows/win32_window.h"

using namespace engine_placeholder;
using os::Key;
using os::win32::KeyFromWin32;
using os::win32::Win32KeyMessage;
using os::win32::Win32Window;

namespace
{

Key Translate(u32 vk, u32 scan, bool extended, u32 character = 0)
{
    return KeyFromWin32(Win32KeyMessage{vk, scan, extended, character});
}

// The lParam of a key message: repeat count 1, the scan code, the extended flag, the Alt context (bit 29), the previous
// state (bit 30) and the transition (bit 31, set for a release).
LPARAM KeyParam(u32 scan, bool extended, bool released, bool repeat, bool alt)
{
    u32 p = 1u | ((scan & 0xFFu) << 16);
    if (extended)
    {
        p |= 1u << 24;
    }
    if (alt)
    {
        p |= 1u << 29;
    }
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

void PostKey(HWND hwnd, bool down, u32 vk, u32 scan, bool extended = false, bool repeat = false)
{
    PostMessageW(hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, KeyParam(scan, extended, !down, repeat, false));
}

void PostSysKey(HWND hwnd, bool down, u32 vk, u32 scan, bool extended = false)
{
    PostMessageW(hwnd, down ? WM_SYSKEYDOWN : WM_SYSKEYUP, vk, KeyParam(scan, extended, !down, false, true));
}

LPARAM Point(i32 x, i32 y)
{
    return MAKELPARAM(static_cast<u16>(static_cast<i16>(x)), static_cast<u16>(static_cast<i16>(y)));
}

struct InputWindow
{
    os::InputState input;
    Win32Window window;
    bool Open() { return window.Open("input test", 320, 240, &input); }
    HWND Hwnd() const { return window.NativeHandle(); }
    void Pump() { window.PumpEvents(); }
};

bool CapsLockOn()
{
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}
bool NumLockOn()
{
    return (GetKeyState(VK_NUMLOCK) & 1) != 0;
}

// The modifier mask with the locks taken out (they are the system's toggles, whatever this test does).
u32 HeldModifiers(const os::InputState& input)
{
    return input.keyboard.modifiers & ~(os::kModCapsLock | os::kModNumLock);
}

}  // namespace

// ---- the translation, as a table
// ----------------------------------------------------------------------------------------------------

TEST_CASE("Windows: KeyFromWin32 names every Key, by the layout's character or by the virtual key and scan code",
          "[win32][input]")
{
    bool covered[os::KeyboardState::kKeyCount] = {};
    auto check = [&](Key got, Key want)
    {
        CHECK(got == want);
        covered[static_cast<usize>(want)] = true;
    };
    for (u32 i = 0; i < 26; ++i)
    {
        check(Translate('A' + i, 0x10, false, 'a' + i), static_cast<Key>(static_cast<u32>(Key::A) + i));
        CHECK(Translate('A' + i, 0x10, false, 'A' + i) ==
              static_cast<Key>(static_cast<u32>(Key::A) + i));  // Shift or Caps Lock
    }
    for (u32 i = 0; i < 10; ++i)
    {
        check(Translate('0' + i, 0x02, false, '0' + i), static_cast<Key>(static_cast<u32>(Key::Num0) + i));
        CHECK(Translate(VK_NUMPAD0 + i, 0x47, false, '0' + i) ==
              static_cast<Key>(static_cast<u32>(Key::Num0) + i));  // NumLock on
    }
    for (u32 i = 0; i < 12; ++i)
    {
        check(Translate(VK_F1 + i, 0x3B + i, false), static_cast<Key>(static_cast<u32>(Key::F1) + i));
    }
    check(Translate(VK_ESCAPE, 0x01, false), Key::Escape);
    check(Translate(VK_RETURN, 0x1C, false, '\r'), Key::Enter);
    CHECK(Translate(VK_RETURN, 0x1C, true, '\r') == Key::Enter);  // the number pad's Enter
    check(Translate(VK_TAB, 0x0F, false, '\t'), Key::Tab);
    check(Translate(VK_BACK, 0x0E, false, 0x08), Key::Backspace);
    check(Translate(VK_SPACE, 0x39, false, ' '), Key::Space);
    check(Translate(VK_INSERT, 0x52, true), Key::Insert);
    check(Translate(VK_DELETE, 0x53, true), Key::Delete);
    check(Translate(VK_HOME, 0x47, true), Key::Home);
    check(Translate(VK_END, 0x4F, true), Key::End);
    check(Translate(VK_PRIOR, 0x49, true), Key::PageUp);
    check(Translate(VK_NEXT, 0x51, true), Key::PageDown);
    check(Translate(VK_LEFT, 0x4B, true), Key::Left);
    check(Translate(VK_RIGHT, 0x4D, true), Key::Right);
    check(Translate(VK_UP, 0x48, true), Key::Up);
    check(Translate(VK_DOWN, 0x50, true), Key::Down);
    check(Translate(VK_SHIFT, 0x2A, false), Key::LeftShift);
    check(Translate(VK_SHIFT, 0x36, false), Key::RightShift);
    check(Translate(VK_CONTROL, 0x1D, false), Key::LeftCtrl);
    check(Translate(VK_CONTROL, 0x1D, true), Key::RightCtrl);
    check(Translate(VK_MENU, 0x38, false), Key::LeftAlt);
    check(Translate(VK_MENU, 0x38, true), Key::RightAlt);
    check(Translate(VK_LWIN, 0x5B, true), Key::LeftSuper);
    check(Translate(VK_RWIN, 0x5C, true), Key::RightSuper);
    check(Translate(VK_CAPITAL, 0x3A, false), Key::CapsLock);
    check(Translate(VK_OEM_MINUS, 0x0C, false, '-'), Key::Minus);
    check(Translate(VK_OEM_PLUS, 0x0D, false, '='), Key::Equals);
    check(Translate(VK_OEM_4, 0x1A, false, '['), Key::LeftBracket);
    check(Translate(VK_OEM_6, 0x1B, false, ']'), Key::RightBracket);
    check(Translate(VK_OEM_5, 0x2B, false, '\\'), Key::Backslash);
    check(Translate(VK_OEM_1, 0x27, false, ';'), Key::Semicolon);
    check(Translate(VK_OEM_7, 0x28, false, '\''), Key::Apostrophe);
    check(Translate(VK_OEM_3, 0x29, false, '`'), Key::Grave);
    check(Translate(VK_OEM_COMMA, 0x33, false, ','), Key::Comma);
    check(Translate(VK_OEM_PERIOD, 0x34, false, '.'), Key::Period);
    check(Translate(VK_OEM_2, 0x35, false, '/'), Key::Slash);
    for (usize k = 1; k < os::KeyboardState::kKeyCount; ++k)
    {
        INFO("Key " << k);
        CHECK(covered[k]);
    }
}

TEST_CASE("Windows: KeyFromWin32 is layout-aware and gives Unknown where Linux does", "[win32][input]")
{
    // French AZERTY: the key in the Q position types 'a', the digit row types '&' unshifted and '1' with Shift (XKB:
    // Unknown, then Num1).
    CHECK(Translate('A', 0x10, false, 'a') == Key::A);
    CHECK(Translate('1', 0x02, false, '&') == Key::Unknown);
    CHECK(Translate('1', 0x02, false, '1') == Key::Num1);
    // German QWERTZ: the key in the Y position types 'z'.
    CHECK(Translate('Z', 0x15, false, 'z') == Key::Z);
    // A shifted symbol, a dead key (no character), a letter outside the enum.
    CHECK(Translate(VK_OEM_1, 0x27, false, ':') == Key::Unknown);
    CHECK(Translate(VK_OEM_6, 0x1A, false, 0) == Key::Unknown);
    CHECK(Translate(VK_OEM_7, 0x28, false, 0x00E9) == Key::Unknown);
    // The number pad with NumLock off (the navigation keys without the extended flag), its operators and its decimal
    // point.
    CHECK(Translate(VK_HOME, 0x47, false) == Key::Unknown);
    CHECK(Translate(VK_LEFT, 0x4B, false) == Key::Unknown);
    CHECK(Translate(VK_DELETE, 0x53, false) == Key::Unknown);
    CHECK(Translate(VK_CLEAR, 0x4C, false) == Key::Unknown);
    CHECK(Translate(VK_DECIMAL, 0x53, false, '.') == Key::Unknown);
    CHECK(Translate(VK_ADD, 0x4E, false, '+') == Key::Unknown);
    CHECK(Translate(VK_SUBTRACT, 0x4A, false, '-') == Key::Unknown);
    CHECK(Translate(VK_DIVIDE, 0x35, true, '/') == Key::Unknown);
    // Keys the enum has no name for.
    CHECK(Translate(VK_PAUSE, 0x45, false) == Key::Unknown);
    CHECK(Translate(VK_SNAPSHOT, 0x37, true) == Key::Unknown);
    CHECK(Translate(VK_NUMLOCK, 0x45, true) == Key::Unknown);
    CHECK(Translate(VK_SCROLL, 0x46, false) == Key::Unknown);
    CHECK(Translate(VK_APPS, 0x5D, true) == Key::Unknown);
    CHECK(Translate(VK_F13, 0x64, false) == Key::Unknown);
}

TEST_CASE("Windows: Win32KeyIndex tells the extended keys from the others", "[win32][input]")
{
    CHECK(os::win32::Win32KeyIndex(0x1D, false) != os::win32::Win32KeyIndex(0x1D, true));  // left and right Ctrl
    CHECK(os::win32::Win32KeyIndex(0x1D, true) < os::win32::kWin32KeyIndexCount);
    CHECK(os::win32::Win32KeyIndex(0x1FF, true) < os::win32::kWin32KeyIndexCount);  // only the low 8 bits count
}

// ---- a window fed Windows' messages
// -----------------------------------------------------------------------------------------------

TEST_CASE("Windows: key presses and releases fill the keyboard state, left and right told apart",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostKey(w.Hwnd(), true, VK_ESCAPE, 0x01);
    PostKey(w.Hwnd(), true, VK_SHIFT, 0x36);          // right Shift
    PostKey(w.Hwnd(), true, VK_CONTROL, 0x1D, true);  // right Ctrl
    PostKey(w.Hwnd(), true, VK_HOME, 0x47, true);     // the separate Home
    PostKey(w.Hwnd(), true, VK_F5, 0x3F);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::Escape));
    CHECK(w.input.keyboard.IsDown(Key::RightShift));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftShift));
    CHECK(w.input.keyboard.IsDown(Key::RightCtrl));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftCtrl));
    CHECK(w.input.keyboard.IsDown(Key::Home));
    CHECK(w.input.keyboard.IsDown(Key::F5));
    CHECK(HeldModifiers(w.input) == (os::kModShift | os::kModCtrl));
    CHECK(((w.input.keyboard.modifiers & os::kModCapsLock) != 0) == CapsLockOn());
    CHECK(((w.input.keyboard.modifiers & os::kModNumLock) != 0) == NumLockOn());

    PostKey(w.Hwnd(), false, VK_ESCAPE, 0x01);
    PostKey(w.Hwnd(), false, VK_SHIFT, 0x36);
    PostKey(w.Hwnd(), false, VK_CONTROL, 0x1D, true);
    PostKey(w.Hwnd(), false, VK_HOME, 0x47, true);
    PostKey(w.Hwnd(), false, VK_F5, 0x3F);
    w.Pump();
    for (usize k = 1; k < os::KeyboardState::kKeyCount; ++k)
    {
        CHECK_FALSE(w.input.keyboard.IsDown(static_cast<Key>(k)));
    }
    CHECK(HeldModifiers(w.input) == 0);
}

TEST_CASE("Windows: a release clears the Key its key was pressed as, and auto-repeat changes nothing",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostKey(w.Hwnd(), true, VK_NUMPAD7, 0x47);               // number pad 7 with NumLock on
    PostKey(w.Hwnd(), true, VK_NUMPAD7, 0x47, false, true);  // a repeat
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::Num7));
    PostKey(w.Hwnd(), false, VK_HOME,
            0x47);  // released after NumLock was turned off: the same physical key, another virtual key
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::Num7));
}

TEST_CASE("Windows: one Shift release ends both Shift keys (Windows sends no release for the first)",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostKey(w.Hwnd(), true, VK_SHIFT, 0x2A);
    PostKey(w.Hwnd(), true, VK_SHIFT, 0x36);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::LeftShift));
    CHECK(w.input.keyboard.IsDown(Key::RightShift));
    PostKey(w.Hwnd(), false, VK_SHIFT, 0x36);  // the only release Windows sends
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftShift));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::RightShift));
    CHECK((w.input.keyboard.modifiers & os::kModShift) == 0);
}

TEST_CASE("Windows: a letter is named by the layout, and Ctrl does not change it", "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    if (MapVirtualKeyW('A', MAPVK_VK_TO_CHAR) != 'A')
    {
        SKIP("the keyboard layout has no Latin A");
    }
    InputWindow w;
    REQUIRE(w.Open());
    const u32 scan = MapVirtualKeyW('A', MAPVK_VK_TO_VSC);
    PostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
    PostKey(w.Hwnd(), true, 'A', scan);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::A));
    CHECK(w.input.keyboard.IsDown(Key::LeftCtrl));
    CHECK(HeldModifiers(w.input) == os::kModCtrl);
    PostKey(w.Hwnd(), false, 'A', scan);
    PostKey(w.Hwnd(), false, VK_CONTROL, 0x1D);
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::A));
}

TEST_CASE("Windows: AltGr is the right Alt alone, as on Linux, not Ctrl and Alt", "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    // Windows sends AltGr as a left Ctrl and a right Alt with the same message time. Two posts usually land in the same
    // tick; try again when they do not.
    bool same_time = false;
    for (int attempt = 0; attempt < 20 && !same_time; ++attempt)
    {
        w.Pump();
        PostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
        PostSysKey(w.Hwnd(), true, VK_MENU, 0x38, true);
        MSG first{};
        MSG second{};
        REQUIRE(PeekMessageW(&first, w.Hwnd(), WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE));
        REQUIRE(PeekMessageW(&second, w.Hwnd(), WM_SYSKEYDOWN, WM_SYSKEYDOWN, PM_NOREMOVE));
        same_time = first.time == second.time;
        if (!same_time)
        {
            w.Pump();
            PostKey(w.Hwnd(), false, VK_CONTROL, 0x1D);
            PostSysKey(w.Hwnd(), false, VK_MENU, 0x38, true);
        }
    }
    REQUIRE(same_time);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::RightAlt));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftCtrl));
    CHECK(HeldModifiers(w.input) == 0);  // XKB's level-3 shift is neither Ctrl nor Alt
    PostKey(w.Hwnd(), false, VK_CONTROL, 0x1D);
    PostSysKey(w.Hwnd(), false, VK_MENU, 0x38, true);
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::RightAlt));
    CHECK(HeldModifiers(w.input) == 0);

    // A left Ctrl on its own (a different time from the next message, or none after it) is a left Ctrl.
    PostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::LeftCtrl));
    CHECK(HeldModifiers(w.input) == os::kModCtrl);
}

TEST_CASE("Windows: the left Alt is Alt, Alt alone opens no menu, and Alt+F4 still asks to close",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostSysKey(w.Hwnd(), true, VK_MENU, 0x38);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::LeftAlt));
    CHECK(HeldModifiers(w.input) == os::kModAlt);
    PostSysKey(w.Hwnd(), false, VK_MENU, 0x38);
    w.Pump();
    // What the default procedure sends after Alt alone; the window does not go into menu mode (no menu loop starts).
    int modal_calls = 0;
    w.window.SetModalLoopCallback([](void* user) { ++*static_cast<int*>(user); }, &modal_calls);
    CHECK(SendMessageW(w.Hwnd(), WM_SYSCOMMAND, SC_KEYMENU, 0) == 0);
    w.Pump();
    CHECK(modal_calls == 0);
    CHECK_FALSE(w.window.ShouldClose());

    PostSysKey(w.Hwnd(), true, VK_MENU, 0x38);
    PostSysKey(w.Hwnd(), true, VK_F4, 0x3E);
    for (int i = 0; i < 100 && !w.window.ShouldClose(); ++i)
    {
        w.Pump();
        os::SleepNanos(10'000'000);
    }
    CHECK(w.window.ShouldClose());  // the default procedure turned Alt+F4 into WM_CLOSE
    CHECK(w.window.Valid());
}

TEST_CASE("Windows: the mouse fills position, buttons with capture, and the wheel in the Linux units",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostMessageW(w.Hwnd(), WM_MOUSEMOVE, 0, Point(10, 20));
    w.Pump();
    CHECK(w.input.mouse_x == 10.0);
    CHECK(w.input.mouse_y == 20.0);
    // The move says inside; asking for WM_MOUSELEAVE then gets one at once if the real cursor is elsewhere (Windows
    // checks it), so the pointer ends up where the real cursor is.
    POINT cursor{};
    GetCursorPos(&cursor);
    CHECK(w.input.pointer_inside == (WindowFromPoint(cursor) == w.Hwnd()));

    PostMessageW(w.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, Point(11, 21));
    PostMessageW(w.Hwnd(), WM_RBUTTONDOWN, MK_LBUTTON | MK_RBUTTON, Point(11, 21));
    PostMessageW(w.Hwnd(), WM_MBUTTONDOWN, MK_LBUTTON | MK_RBUTTON | MK_MBUTTON, Point(11, 21));
    PostMessageW(w.Hwnd(), WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON1), Point(11, 21));
    PostMessageW(w.Hwnd(), WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), Point(11, 21));
    w.Pump();
    CHECK(w.input.mouse_buttons ==
          (os::kMouseLeft | os::kMouseRight | os::kMouseMiddle | os::kMouseBack | os::kMouseForward));
    CHECK(GetCapture() == w.Hwnd());  // the releases come here even outside the window

    // Outside the client area while the buttons hold the capture: the position goes on, the pointer is not inside.
    PostMessageW(w.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, Point(-5, 30));
    w.Pump();
    CHECK(w.input.mouse_x == -5.0);
    CHECK_FALSE(w.input.pointer_inside);

    PostMessageW(w.Hwnd(), WM_LBUTTONUP, 0, Point(1, 1));
    PostMessageW(w.Hwnd(), WM_RBUTTONUP, 0, Point(1, 1));
    PostMessageW(w.Hwnd(), WM_MBUTTONUP, 0, Point(1, 1));
    PostMessageW(w.Hwnd(), WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), Point(1, 1));
    w.Pump();
    CHECK(w.input.mouse_buttons == os::kMouseForward);
    CHECK(GetCapture() == w.Hwnd());
    PostMessageW(w.Hwnd(), WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), Point(1, 1));
    w.Pump();
    CHECK(w.input.mouse_buttons == 0);
    CHECK(GetCapture() != w.Hwnd());

    // One notch away from the user is up (negative), a half notch toward the user is half a step down; tilting right is
    // positive.
    w.input.BeginFrame();
    PostMessageW(w.Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<u16>(WHEEL_DELTA)), Point(10, 10));
    PostMessageW(w.Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<u16>(static_cast<i16>(-WHEEL_DELTA / 2))),
                 Point(10, 10));
    PostMessageW(w.Hwnd(), WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<u16>(WHEEL_DELTA)), Point(10, 10));
    w.Pump();
    CHECK(w.input.scroll_dy == -5.0);  // -10 + 5
    CHECK(w.input.scroll_dx == 10.0);
    w.input.BeginFrame();
    CHECK(w.input.scroll_dy == 0.0);

    // Leaving keeps held buttons held, as on Linux.
    PostMessageW(w.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, Point(5, 5));
    PostMessageW(w.Hwnd(), WM_MOUSELEAVE, 0, 0);
    w.Pump();
    CHECK_FALSE(w.input.pointer_inside);
    CHECK(w.input.IsMouseDown(os::kMouseLeft));
    // Another window taking the mouse while a button is held: the release will not come, so the button is up.
    SendMessageW(w.Hwnd(), WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(GetDesktopWindow()));
    CHECK(w.input.mouse_buttons == 0);
}

TEST_CASE("Windows: losing the focus releases every key, button and modifier", "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostKey(w.Hwnd(), true, VK_SHIFT, 0x2A);
    PostKey(w.Hwnd(), true, VK_F2, 0x3C);
    PostMessageW(w.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, Point(5, 5));
    w.Pump();
    REQUIRE(w.input.keyboard.IsDown(Key::LeftShift));
    REQUIRE(w.input.keyboard.IsDown(Key::F2));
    REQUIRE(w.input.IsMouseDown(os::kMouseLeft));
    SendMessageW(w.Hwnd(), WM_KILLFOCUS, 0,
                 0);  // what Windows sends when another window takes the keyboard (Alt+Tab, a click elsewhere)
    CHECK_FALSE(w.input.keyboard.IsDown(Key::LeftShift));
    CHECK_FALSE(w.input.keyboard.IsDown(Key::F2));
    CHECK(w.input.keyboard.modifiers == 0);
    CHECK(w.input.mouse_buttons == 0);
    CHECK(GetCapture() != w.Hwnd());
    // The keys' releases arrive at another window; a key pressed again here is down again (nothing is stuck either
    // way).
    PostKey(w.Hwnd(), false, VK_F2, 0x3C);
    PostKey(w.Hwnd(), true, VK_F2, 0x3C);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::F2));
}

TEST_CASE("Windows: the focus moving to another window of the program releases what this one held",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow a;
    InputWindow b;
    REQUIRE(a.Open());
    REQUIRE(b.Open());
    SetFocus(a.Hwnd());
    if (GetFocus() != a.Hwnd())
    {
        SKIP("this program is not in the foreground, so it cannot give its windows the keyboard focus");
    }
    PostKey(a.Hwnd(), true, VK_F3, 0x3D);
    a.Pump();
    REQUIRE(a.input.keyboard.IsDown(Key::F3));
    SetFocus(b.Hwnd());
    a.Pump();
    CHECK_FALSE(a.input.keyboard.IsDown(Key::F3));
}

// Without an InputState the window keeps its own (the event stream needs the key and button tracking, Phase 15 Day
// 205), so a press still takes the capture, and losing the focus still gives it back.
TEST_CASE("Windows: a window opened without an InputState takes input and releases it on a focus loss",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    Win32Window window;
    REQUIRE(window.Open("no input", 320, 240, nullptr));
    PostKey(window.NativeHandle(), true, VK_ESCAPE, 0x01);
    PostMessageW(window.NativeHandle(), WM_MOUSEMOVE, 0, Point(3, 4));
    PostMessageW(window.NativeHandle(), WM_LBUTTONDOWN, MK_LBUTTON, Point(3, 4));
    PostMessageW(window.NativeHandle(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<u16>(WHEEL_DELTA)), Point(3, 4));
    window.PumpEvents();
    CHECK(window.Valid());
    CHECK(GetCapture() == window.NativeHandle());
    SendMessageW(window.NativeHandle(), WM_KILLFOCUS, 0, 0);
    CHECK(GetCapture() != window.NativeHandle());
}

TEST_CASE("Windows: keys sent without a scan code each keep their own state", "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    // Injected input (a remote session, an automation tool) may carry the virtual key only.
    PostKey(w.Hwnd(), true, VK_F6, 0);
    PostKey(w.Hwnd(), true, VK_F7, 0);
    PostKey(w.Hwnd(), false, VK_F6, 0);
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::F6));
    CHECK(w.input.keyboard.IsDown(Key::F7));
    PostKey(w.Hwnd(), false, VK_F7, 0);
    w.Pump();
    CHECK_FALSE(w.input.keyboard.IsDown(Key::F7));
}

TEST_CASE("Windows: a left Ctrl pressed some time before a right Alt is a left Ctrl, not AltGr",
          "[window][win32][input]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    InputWindow w;
    REQUIRE(w.Open());
    PostKey(w.Hwnd(), true, VK_CONTROL, 0x1D);
    os::SleepNanos(60'000'000);  // several ticks, so the two messages have different times
    PostSysKey(w.Hwnd(), true, VK_MENU, 0x38, true);
    w.Pump();
    CHECK(w.input.keyboard.IsDown(Key::LeftCtrl));
    CHECK(w.input.keyboard.IsDown(Key::RightAlt));
    CHECK(HeldModifiers(w.input) == (os::kModCtrl | os::kModAlt));
}
