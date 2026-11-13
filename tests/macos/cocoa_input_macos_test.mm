// The macOS input event stream (Phase 21 Day 296; engine/platform/macos/cocoa_keys.h, cocoa_window.mm,
// engine/platform/BACKEND.md "macos"). The key table and the flag, text and wheel rules first, checked against the
// SDK's own constants without a window; then keys, text (with the current layout's dead key), the pointer, the wheel
// and the focus through a real window, from synthesized NSEvents given to it the way AppKit gives them, with the
// InputState compared to the stream. The window cases are in the `display` group and skip while the session is locked.
#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/vector.h"
#include "display_session.h"
#include "os/os.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/macos/cocoa_keys.h"
#include "platform/macos/cocoa_window.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <IOKit/hidsystem/IOLLEvent.h>

using namespace engine_placeholder;
using engine_placeholder::os::InputEvent;
using engine_placeholder::os::InputEventType;
using engine_placeholder::os::Key;
using engine_placeholder::os::cocoa::CocoaWindow;

namespace
{

constexpr u32 kCocoaNoKey = 0xFFFF;

// --- Helpers -------------------------------------------------------------------------------------------------------

NSWindow* WindowOf(const CocoaWindow& window)
{
    NSView* view = (__bridge NSView*)window.GetNativeInfo().display;
    return view.window;
}

NSView* ViewOf(const CocoaWindow& window)
{
    return (__bridge NSView*)window.GetNativeInfo().display;
}

Vector<InputEvent> Drain(os::Window& window)
{
    Vector<InputEvent> out;
    InputEvent event;
    while (window.PollEvent(&event))
    {
        out.Push(event);
    }
    return out;
}

// Opens a window and drains what opening it queued (the Resize, the focus).
void OpenQuiet(CocoaWindow& window, os::InputState* state, const char* title = "cocoa input")
{
    REQUIRE(window.Open(title, 320, 200, state));
    window.PumpEvents();
    Drain(window);
}

NSEvent* KeyEvent(NSEventType type, unsigned short code, NSString* characters, NSString* ignoring,
                  NSEventModifierFlags flags, const CocoaWindow& window, BOOL repeat = NO)
{
    return [NSEvent keyEventWithType:type
                            location:NSZeroPoint
                       modifierFlags:flags
                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                        windowNumber:WindowOf(window).windowNumber
                             context:nil
                          characters:characters
         charactersIgnoringModifiers:ignoring
                           isARepeat:repeat
                             keyCode:code];
}

// A key press and release given to the window as AppKit gives them (its sendEvent: to the first responder).
void Press(const CocoaWindow& window, unsigned short code, NSString* characters, NSString* ignoring,
           NSEventModifierFlags flags = 0)
{
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyDown, code, characters, ignoring, flags, window)];
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyUp, code, characters, ignoring, flags, window)];
}

NSEvent* FlagsEvent(unsigned short code, NSEventModifierFlags flags, const CocoaWindow& window)
{
    return KeyEvent(NSEventTypeFlagsChanged, code, @"", @"", flags, window);
}

// A mouse event at (x, y) points in the view, from its top left (AppKit's window coordinates are from the bottom left).
NSEvent* MouseEvent(NSEventType type, const CocoaWindow& window, CGFloat x, CGFloat y, NSInteger button = 0,
                    NSEventModifierFlags flags = 0)
{
    NSView* view = ViewOf(window);
    const NSPoint in_window = [view convertPoint:NSMakePoint(x, view.bounds.size.height - y) toView:nil];
    NSEvent* event = [NSEvent mouseEventWithType:type
                                        location:in_window
                                   modifierFlags:flags
                                       timestamp:[[NSProcessInfo processInfo] systemUptime]
                                    windowNumber:WindowOf(window).windowNumber
                                         context:nil
                                     eventNumber:0
                                      clickCount:1
                                        pressure:type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
    if (button >= 2)
    {
        // mouseEventWithType has no button number: the CGEvent carries it.
        CGEventRef cg = CGEventCreateCopy([event CGEvent]);
        CGEventSetIntegerValueField(cg, kCGMouseEventButtonNumber, button);
        event = [NSEvent eventWithCGEvent:cg];
        CFRelease(cg);
    }
    return event;
}

NSEvent* ScrollEvent(CGScrollEventUnit unit, i32 dy, i32 dx)
{
    CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr, unit, 2, dy, dx);
    NSEvent* event = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    return event;
}

bool TextIs(const InputEvent& event, const char* utf8)
{
    if (event.type != InputEventType::Text)
    {
        return false;
    }
    usize i = 0;
    for (; utf8[i] != '\0'; ++i)
    {
        if (i >= event.text_length || event.text[i] != utf8[i])
        {
            return false;
        }
    }
    return i == event.text_length;
}

usize CountOf(const Vector<InputEvent>& events, InputEventType type)
{
    usize n = 0;
    for (usize i = 0; i < events.Size(); ++i)
    {
        const InputEvent& e = events[i];
        n += e.type == type ? 1 : 0;
    }
    return n;
}

}  // namespace

// --- The key table and the rules, against the SDK (no window) -------------------------------------------------------

TEST_CASE("macOS: KeyFromCocoa names every Key, by the layout's character or by the key code", "[macos][input]")
{
    struct Case
    {
        u32 code;
        u32 character;
        Key key;
    };
    // The US layout's characters for the keys named by character; positional keys get a character the layout would
    // never give them, to show it is not used.
    const Case cases[] = {
        {kVK_ANSI_A, 'a', Key::A},
        {kVK_ANSI_B, 'b', Key::B},
        {kVK_ANSI_C, 'c', Key::C},
        {kVK_ANSI_D, 'd', Key::D},
        {kVK_ANSI_E, 'e', Key::E},
        {kVK_ANSI_F, 'f', Key::F},
        {kVK_ANSI_G, 'g', Key::G},
        {kVK_ANSI_H, 'h', Key::H},
        {kVK_ANSI_I, 'i', Key::I},
        {kVK_ANSI_J, 'j', Key::J},
        {kVK_ANSI_K, 'k', Key::K},
        {kVK_ANSI_L, 'l', Key::L},
        {kVK_ANSI_M, 'm', Key::M},
        {kVK_ANSI_N, 'n', Key::N},
        {kVK_ANSI_O, 'o', Key::O},
        {kVK_ANSI_P, 'p', Key::P},
        {kVK_ANSI_Q, 'q', Key::Q},
        {kVK_ANSI_R, 'r', Key::R},
        {kVK_ANSI_S, 's', Key::S},
        {kVK_ANSI_T, 't', Key::T},
        {kVK_ANSI_U, 'u', Key::U},
        {kVK_ANSI_V, 'v', Key::V},
        {kVK_ANSI_W, 'W', Key::W},  // Shift or Caps Lock: still the letter
        {kVK_ANSI_X, 'x', Key::X},
        {kVK_ANSI_Y, 'y', Key::Y},
        {kVK_ANSI_Z, 'z', Key::Z},
        {kVK_ANSI_0, '0', Key::Num0},
        {kVK_ANSI_1, '1', Key::Num1},
        {kVK_ANSI_2, '2', Key::Num2},
        {kVK_ANSI_3, '3', Key::Num3},
        {kVK_ANSI_4, '4', Key::Num4},
        {kVK_ANSI_5, '5', Key::Num5},
        {kVK_ANSI_6, '6', Key::Num6},
        {kVK_ANSI_7, '7', Key::Num7},
        {kVK_ANSI_8, '8', Key::Num8},
        {kVK_ANSI_9, '9', Key::Num9},
        {kVK_F1, 'x', Key::F1},
        {kVK_F2, 'x', Key::F2},
        {kVK_F3, 'x', Key::F3},
        {kVK_F4, 'x', Key::F4},
        {kVK_F5, 'x', Key::F5},
        {kVK_F6, 'x', Key::F6},
        {kVK_F7, 'x', Key::F7},
        {kVK_F8, 'x', Key::F8},
        {kVK_F9, 'x', Key::F9},
        {kVK_F10, 'x', Key::F10},
        {kVK_F11, 'x', Key::F11},
        {kVK_F12, 'x', Key::F12},
        {kVK_Escape, 0x1B, Key::Escape},
        {kVK_Return, '\r', Key::Enter},
        {kVK_Tab, '\t', Key::Tab},
        {kVK_Delete, 0x7F, Key::Backspace},
        {kVK_Space, ' ', Key::Space},
        {kVK_Help, 'x', Key::Insert},
        {kVK_ForwardDelete, 'x', Key::Delete},
        {kVK_Home, 'x', Key::Home},
        {kVK_End, 'x', Key::End},
        {kVK_PageUp, 'x', Key::PageUp},
        {kVK_PageDown, 'x', Key::PageDown},
        {kVK_LeftArrow, 'x', Key::Left},
        {kVK_RightArrow, 'x', Key::Right},
        {kVK_UpArrow, 'x', Key::Up},
        {kVK_DownArrow, 'x', Key::Down},
        {kVK_Shift, 0, Key::LeftShift},
        {kVK_RightShift, 0, Key::RightShift},
        {kVK_Control, 0, Key::LeftCtrl},
        {kVK_RightControl, 0, Key::RightCtrl},
        {kVK_Option, 0, Key::LeftAlt},
        {kVK_RightOption, 0, Key::RightAlt},
        {kVK_Command, 0, Key::LeftSuper},
        {kVK_RightCommand, 0, Key::RightSuper},
        {kVK_CapsLock, 0, Key::CapsLock},
        {kVK_ANSI_Minus, '-', Key::Minus},
        {kVK_ANSI_Equal, '=', Key::Equals},
        {kVK_ANSI_LeftBracket, '[', Key::LeftBracket},
        {kVK_ANSI_RightBracket, ']', Key::RightBracket},
        {kVK_ANSI_Backslash, '\\', Key::Backslash},
        {kVK_ANSI_Semicolon, ';', Key::Semicolon},
        {kVK_ANSI_Quote, '\'', Key::Apostrophe},
        {kVK_ANSI_Grave, '`', Key::Grave},
        {kVK_ANSI_Comma, ',', Key::Comma},
        {kVK_ANSI_Period, '.', Key::Period},
        {kVK_ANSI_Slash, '/', Key::Slash},
    };
    Array<bool, static_cast<usize>(Key::Count)> named = {};
    for (const Case& c : cases)
    {
        INFO("key code " << c.code);
        CHECK(os::cocoa::KeyFromCocoa(c.code, c.character) == c.key);
        named[static_cast<usize>(c.key)] = true;
    }
    for (usize k = 1; k < static_cast<usize>(Key::Count); ++k)
    {
        INFO("Key " << k << " has no case");
        CHECK(named[k]);
    }
    // The number pad: the digits and Enter as the main ones (as XKB's KP_0..KP_9 and KP_Enter).
    const u32 keypad[] = {kVK_ANSI_Keypad0, kVK_ANSI_Keypad1, kVK_ANSI_Keypad2, kVK_ANSI_Keypad3, kVK_ANSI_Keypad4,
                          kVK_ANSI_Keypad5, kVK_ANSI_Keypad6, kVK_ANSI_Keypad7, kVK_ANSI_Keypad8, kVK_ANSI_Keypad9};
    for (u32 d = 0; d < 10; ++d)
    {
        CHECK(os::cocoa::KeyFromCocoa(keypad[d], 'x') == static_cast<Key>(static_cast<u32>(Key::Num0) + d));
    }
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_KeypadEnter, 0x3) == Key::Enter);
}

TEST_CASE("macOS: KeyFromCocoa is layout-aware and gives Unknown where Linux does", "[macos][input]")
{
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_Q, 'a') == Key::A);        // AZERTY
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_Z, 'y') == Key::Y);        // QWERTZ
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_1, '!') == Key::Unknown);  // Shift+1 on a US layout, as XKB's exclam
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_Semicolon, 0xF1) == Key::Unknown);  // Spanish ñ
    CHECK(os::cocoa::KeyFromCocoa(kVK_ANSI_Quote, 0) == Key::Unknown);         // a dead key: no character
    CHECK(os::cocoa::KeyFromCocoa(kVK_ISO_Section, '<') == Key::Unknown);
    CHECK(os::cocoa::KeyFromCocoa(kVK_ISO_Section, '`') == Key::Grave);  // the ISO key, by its character
    const u32 unnamed[] = {kVK_Function,
                           kVK_F13,
                           kVK_F14,
                           kVK_F15,
                           kVK_F16,
                           kVK_F17,
                           kVK_F18,
                           kVK_F19,
                           kVK_F20,
                           kVK_ANSI_KeypadDecimal,
                           kVK_ANSI_KeypadMultiply,
                           kVK_ANSI_KeypadPlus,
                           kVK_ANSI_KeypadClear,
                           kVK_ANSI_KeypadDivide,
                           kVK_ANSI_KeypadMinus,
                           kVK_ANSI_KeypadEquals,
                           kVK_JIS_KeypadComma,
                           kVK_VolumeUp,
                           kVK_VolumeDown,
                           kVK_Mute,
                           kVK_JIS_Eisu,
                           kVK_JIS_Kana};
    for (const u32 code : unnamed)
    {
        INFO("key code " << code);
        CHECK(os::cocoa::KeyFromCocoa(code, '1') == Key::Unknown);  // even with a character that names a key
    }
    CHECK(os::cocoa::KeyFromCocoa(200, 'a') == Key::Unknown);  // out of range
}

TEST_CASE("macOS: the modifier flags, and which side of a modifier is down", "[macos][input]")
{
    CHECK(os::cocoa::ModifiersFromCocoa(0) == 0);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagShift) == os::kModShift);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagControl) == os::kModCtrl);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagOption) == os::kModAlt);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagCommand) == os::kModSuper);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagCapsLock) == os::kModCapsLock);
    CHECK(os::cocoa::ModifiersFromCocoa(NSEventModifierFlagFunction | NSEventModifierFlagNumericPad) == 0);
    for (const u32 code : {kVK_Shift, kVK_RightShift, kVK_Control, kVK_RightControl, kVK_Option, kVK_RightOption,
                           kVK_Command, kVK_RightCommand, kVK_CapsLock, kVK_Function})
    {
        CHECK(os::cocoa::IsCocoaModifierKey(code));
    }
    CHECK_FALSE(os::cocoa::IsCocoaModifierKey(kVK_ANSI_A));
    CHECK_FALSE(os::cocoa::IsCocoaModifierKey(kVK_Space));
    // The device bits say which side.
    const u64 both_shifts = NSEventModifierFlagShift | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK;
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_Shift, both_shifts));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_RightShift, both_shifts));
    const u64 right_only = NSEventModifierFlagShift | NX_DEVICERSHIFTKEYMASK;
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_Shift, right_only));  // the left one was released
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_RightShift, right_only));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_RightControl, NSEventModifierFlagControl | NX_DEVICERCTLKEYMASK));
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_Control, NSEventModifierFlagControl | NX_DEVICERCTLKEYMASK));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_Option, NSEventModifierFlagOption | NX_DEVICELALTKEYMASK));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_RightOption, NSEventModifierFlagOption | NX_DEVICERALTKEYMASK));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_Command, NSEventModifierFlagCommand | NX_DEVICELCMDKEYMASK));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_RightCommand, NSEventModifierFlagCommand | NX_DEVICERCMDKEYMASK));
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_Command, NSEventModifierFlagCommand | NX_DEVICERCMDKEYMASK));
    // Without device bits (an event made by NSEvent's constructors), the modifier's own flag.
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_Shift, NSEventModifierFlagShift));
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_Shift, 0));
    CHECK(os::cocoa::CocoaModifierKeyDown(kVK_CapsLock, NSEventModifierFlagCapsLock));
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_CapsLock, 0));
    CHECK_FALSE(os::cocoa::CocoaModifierKeyDown(kVK_ANSI_A, NSEventModifierFlagShift));
}

TEST_CASE("macOS: control characters and the function-key scalars are not text; the wheel turns into notches",
          "[macos][input]")
{
    CHECK(os::cocoa::IsTypedScalar('a'));
    CHECK(os::cocoa::IsTypedScalar(0xE9));     // é
    CHECK(os::cocoa::IsTypedScalar(0x1F389));  // 🎉
    CHECK(os::cocoa::IsTypedScalar(' '));
    for (const u32 c : {0x0Du, 0x09u, 0x08u, 0x1Bu, 0x7Fu, 0x03u, 0x85u})
    {
        CHECK_FALSE(os::cocoa::IsTypedScalar(c));
    }
    CHECK_FALSE(os::cocoa::IsTypedScalar(NSUpArrowFunctionKey));
    CHECK_FALSE(os::cocoa::IsTypedScalar(NSDeleteFunctionKey));
    CHECK_FALSE(os::cocoa::IsTypedScalar(NSF35FunctionKey));
    // Line deltas: one per notch, the sign turned (macOS: positive is up and left).
    const os::cocoa::CocoaWheel lines = os::cocoa::CocoaWheelNotches(1.0, -2.0, false);
    CHECK(lines.dx == -1.0);
    CHECK(lines.dy == 2.0);
    // Precise deltas: points, ten to the notch.
    const os::cocoa::CocoaWheel points = os::cocoa::CocoaWheelNotches(-5.0, 25.0, true);
    CHECK(points.dx == 0.5);
    CHECK(points.dy == -2.5);
}

// --- Through a window ------------------------------------------------------------------------------------------------

TEST_CASE("macOS events: a letter gives KeyDown then Text, repeats with its text, and goes up; InputState agrees",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_A, @"a", @"a", 0, window)];
    CHECK(state.keyboard.IsDown(Key::A));
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_A, @"a", @"a", 0, window, YES)];
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_A, @"a", @"a", 0, window)];
    CHECK_FALSE(state.keyboard.IsDown(Key::A));
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 5);
    CHECK(events[0].type == InputEventType::KeyDown);
    CHECK(events[0].key == Key::A);
    CHECK_FALSE(events[0].repeat);
    CHECK(events[0].time_ns != 0);
    CHECK(TextIs(events[1], "a"));
    CHECK(events[2].type == InputEventType::KeyDown);
    CHECK(events[2].repeat);
    CHECK(TextIs(events[3], "a"));
    CHECK(events[4].type == InputEventType::KeyUp);
    CHECK(events[4].key == Key::A);
}

TEST_CASE("macOS events: Enter, Tab, Backspace, Escape and Delete type nothing; Ctrl and Cmd presses neither",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    Press(window, kVK_Return, @"\r", @"\r");
    Press(window, kVK_Tab, @"\t", @"\t");
    Press(window, kVK_Delete, @"\x7f", @"\x7f");
    Press(window, kVK_Escape, @"\x1b", @"\x1b");
    const unichar forward_delete = NSDeleteFunctionKey;
    NSString* del = [NSString stringWithCharacters:&forward_delete length:1];
    Press(window, kVK_ForwardDelete, del, del);
    Press(window, kVK_ANSI_C, @"\x03", @"c", NSEventModifierFlagControl);
    // Cmd+B: no menu item has it, so the view gets it as a key (the menu would have taken Cmd-Q).
    [NSApp sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_B, @"b", @"b", NSEventModifierFlagCommand, window)];
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_B, @"b", @"b", NSEventModifierFlagCommand, window)];
    const Vector<InputEvent> events = Drain(window);
    CHECK(CountOf(events, InputEventType::Text) == 0);
    const Key expected[] = {Key::Enter, Key::Tab, Key::Backspace, Key::Escape, Key::Delete, Key::C, Key::B};
    REQUIRE(events.Size() == 2 * (sizeof(expected) / sizeof(expected[0])));
    for (usize i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        INFO("key " << i);
        CHECK(events[2 * i].type == InputEventType::KeyDown);
        CHECK(events[2 * i].key == expected[i]);
        CHECK(events[2 * i + 1].type == InputEventType::KeyUp);
    }
    CHECK(events[10].modifiers == os::kModCtrl);
    CHECK(events[12].modifiers == os::kModSuper);
}

TEST_CASE("macOS events: Option types the layout's third level and stays Alt; the key keeps its name",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window, nullptr);
    // Option+2 on a US layout is ™: the key is still Num2, the Alt bit is set, and the text arrives.
    [WindowOf(window)
        sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_2, @"™", @"2", NSEventModifierFlagOption, window)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() >= 1);
    CHECK(events[0].type == InputEventType::KeyDown);
    CHECK(events[0].key == Key::Num2);
    CHECK(events[0].modifiers == os::kModAlt);
}

TEST_CASE("macOS events: modifier keys go down and up with the modifiers held before them, left and right apart",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSWindow* ns = WindowOf(window);
    const NSEventModifierFlags shift = NSEventModifierFlagShift;
    [ns sendEvent:FlagsEvent(kVK_Shift, shift | NX_DEVICELSHIFTKEYMASK, window)];
    CHECK(state.keyboard.IsDown(Key::LeftShift));
    CHECK(state.keyboard.modifiers == os::kModShift);
    [ns sendEvent:FlagsEvent(kVK_RightShift, shift | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK, window)];
    [ns sendEvent:FlagsEvent(kVK_Shift, shift | NX_DEVICERSHIFTKEYMASK, window)];
    CHECK_FALSE(state.keyboard.IsDown(Key::LeftShift));
    CHECK(state.keyboard.IsDown(Key::RightShift));
    [ns sendEvent:FlagsEvent(kVK_RightShift, 0, window)];
    CHECK_FALSE(state.keyboard.IsDown(Key::RightShift));
    CHECK(state.keyboard.modifiers == 0);
    [ns sendEvent:FlagsEvent(kVK_Command, NSEventModifierFlagCommand | NX_DEVICELCMDKEYMASK, window)];
    [ns sendEvent:FlagsEvent(kVK_Command, 0, window)];
    [ns sendEvent:FlagsEvent(kVK_CapsLock, NSEventModifierFlagCapsLock, window)];  // the lock goes on: a tap
    CHECK_FALSE(state.keyboard.IsDown(Key::CapsLock));
    CHECK(state.keyboard.modifiers == os::kModCapsLock);
    [ns sendEvent:FlagsEvent(kVK_Function, NSEventModifierFlagFunction | NSEventModifierFlagCapsLock, window)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 8);
    CHECK(events[0].type == InputEventType::KeyDown);
    CHECK(events[0].key == Key::LeftShift);
    CHECK(events[0].modifiers == 0);  // before the key
    CHECK(events[1].type == InputEventType::KeyDown);
    CHECK(events[1].key == Key::RightShift);
    CHECK(events[1].modifiers == os::kModShift);
    CHECK(events[2].type == InputEventType::KeyUp);
    CHECK(events[2].key == Key::LeftShift);
    CHECK(events[3].type == InputEventType::KeyUp);
    CHECK(events[3].key == Key::RightShift);
    CHECK(events[4].key == Key::LeftSuper);
    CHECK(events[4].type == InputEventType::KeyDown);
    CHECK(events[5].key == Key::LeftSuper);
    CHECK(events[5].type == InputEventType::KeyUp);
    CHECK(events[5].modifiers == os::kModSuper);
    CHECK(events[6].type == InputEventType::KeyDown);
    CHECK(events[6].key == Key::CapsLock);
    CHECK(events[7].type == InputEventType::KeyUp);
    CHECK(events[7].key == Key::CapsLock);
    CHECK(events[7].modifiers == os::kModCapsLock);  // fn after it: no event of its own
}

TEST_CASE("macOS events: a key-up with Command held reaches the window through PumpEvents; the menu's own key does not "
          "leave a key-up behind",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSWindow* ns = WindowOf(window);
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_K, @"k", @"k", 0, window)];
    REQUIRE(state.keyboard.IsDown(Key::K));
    // Command goes down while K is held, then K comes up: AppKit's own sendEvent: would keep this key-up.
    [NSApp postEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_K, @"k", @"k", NSEventModifierFlagCommand, window) atStart:NO];
    window.PumpEvents();
    CHECK_FALSE(state.keyboard.IsDown(Key::K));
    // A key-up whose press went to the menu (Cmd-Q's Q) is not reported: the window never saw the key go down.
    [NSApp postEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_Q, @"q", @"q", NSEventModifierFlagCommand, window) atStart:NO];
    window.PumpEvents();
    const Vector<InputEvent> events = Drain(window);
    usize ups = 0;
    for (usize i = 0; i < events.Size(); ++i)
    {
        const InputEvent& e = events[i];
        if (e.type == InputEventType::KeyUp)
        {
            ++ups;
            CHECK(e.key == Key::K);
            CHECK(e.modifiers == os::kModSuper);
        }
    }
    CHECK(ups == 1);
}

TEST_CASE("macOS events: committed text is one Text per scalar, control characters dropped, marked text none",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window, nullptr);
    NSView<NSTextInputClient>* view = (NSView<NSTextInputClient>*)ViewOf(window);
    [view setMarkedText:@"´" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
    CHECK(view.hasMarkedText);
    CHECK(Drain(window).Size() == 0);  // a dead key waiting, or an input method composing: nothing yet
    [view insertText:@"é" replacementRange:NSMakeRange(NSNotFound, 0)];
    CHECK_FALSE(view.hasMarkedText);
    [view insertText:[[NSAttributedString alloc] initWithString:@"´x\r\U0001F389"]
        replacementRange:NSMakeRange(NSNotFound, 0)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 4);
    CHECK(TextIs(events[0], "\xC3\xA9"));
    CHECK(TextIs(events[1], "\xC2\xB4"));
    CHECK(TextIs(events[2], "x"));
    CHECK(TextIs(events[3], "\xF0\x9F\x8E\x89"));
}

TEST_CASE("macOS events: the current layout's dead key gives no text until the next key completes it",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    // The layout in use (this Mac's: Spanish ISO; others differ), read the way the text system reads it.
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    REQUIRE(source != nullptr);
    CFDataRef data = (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    if (data == nullptr)
    {
        CFRelease(source);
        SKIP("the current input source has no Unicode key layout (an input method)");
    }
    const UCKeyboardLayout* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
    // The first key that is dead without modifiers, and what it makes with the E key.
    u32 dead_code = kCocoaNoKey;
    Array<UniChar, 8> composed = {};
    UniCharCount composed_length = 0;
    for (u32 code = 0; code < 0x33 && dead_code == kCocoaNoKey; ++code)
    {
        UInt32 dead_state = 0;
        Array<UniChar, 8> chars = {};
        UniCharCount length = 0;
        UCKeyTranslate(layout, static_cast<UInt16>(code), kUCKeyActionDown, 0, LMGetKbdType(), 0, &dead_state,
                       chars.Size(), &length, chars.Data());
        if (dead_state != 0 && length == 0)
        {
            UCKeyTranslate(layout, kVK_ANSI_E, kUCKeyActionDown, 0, LMGetKbdType(), 0, &dead_state, composed.Size(),
                           &composed_length, composed.Data());
            if (composed_length > 0)
            {
                dead_code = code;
            }
        }
    }
    CFRelease(source);
    if (dead_code == kCocoaNoKey)
    {
        SKIP("the current layout has no dead key");
    }
    NSString* expected = [NSString stringWithCharacters:composed.Data() length:composed_length];
    CocoaWindow window;
    OpenQuiet(window, nullptr);
    NSWindow* ns = WindowOf(window);
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, static_cast<unsigned short>(dead_code), @"", @"", 0, window)];
    [ns sendEvent:KeyEvent(NSEventTypeKeyUp, static_cast<unsigned short>(dead_code), @"", @"", 0, window)];
    CHECK(CountOf(Drain(window), InputEventType::Text) == 0);
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_E, expected, @"e", 0, window)];
    [ns sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_E, @"e", @"e", 0, window)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() >= 3);
    CHECK(events[0].type == InputEventType::KeyDown);
    CHECK(events[0].key == Key::E);
    REQUIRE(events[1].type == InputEventType::Text);
    NSString* got = [[NSString alloc] initWithBytes:events[1].text
                                             length:events[1].text_length
                                           encoding:NSUTF8StringEncoding];
    INFO("dead key code " << dead_code << ", expected " << expected.UTF8String << ", got " << got.UTF8String);
    CHECK([got isEqualToString:expected]);
}

TEST_CASE("macOS events: the pointer enters, moves, presses every button and leaves, in pixels from the top left",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSWindow* ns = WindowOf(window);
    const f64 scale = window.ContentScale();
    [ViewOf(window) mouseMoved:MouseEvent(NSEventTypeMouseMoved, window, 10, 20)];
    [ns sendEvent:MouseEvent(NSEventTypeLeftMouseDown, window, 11, 21)];
    CHECK(state.IsMouseDown(os::kMouseLeft));
    [ns sendEvent:MouseEvent(NSEventTypeLeftMouseDragged, window, 12, 22)];
    [ns sendEvent:MouseEvent(NSEventTypeLeftMouseUp, window, 12, 22)];
    [ns sendEvent:MouseEvent(NSEventTypeRightMouseDown, window, 13, 23)];
    [ns sendEvent:MouseEvent(NSEventTypeRightMouseUp, window, 13, 23)];
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseDown, window, 14, 24, 2)];
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseUp, window, 14, 24, 2)];
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseDown, window, 14, 24, 3)];
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseUp, window, 14, 24, 3)];
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseDown, window, 14, 24, 4)];
    CHECK(state.IsMouseDown(os::kMouseForward));
    [ns sendEvent:MouseEvent(NSEventTypeOtherMouseUp, window, 14, 24, 4)];
    CHECK(state.mouse_buttons == 0);
    CHECK(state.pointer_inside);
    CHECK(state.mouse_x == 14 * scale);
    CHECK(state.mouse_y == 24 * scale);
    [ViewOf(window) mouseExited:MouseEvent(NSEventTypeMouseMoved, window, -1, 24)];  // as the tracking area says it
    CHECK_FALSE(state.pointer_inside);
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 14);
    CHECK(events[0].type == InputEventType::PointerEnter);  // the first move inside is the enter
    CHECK(events[0].x == 10 * scale);
    CHECK(events[0].y == 20 * scale);
    CHECK(events[1].type == InputEventType::PointerMove);
    CHECK(events[1].x == 10 * scale);
    CHECK(events[1].y == 20 * scale);
    CHECK(events[2].type == InputEventType::PointerButton);
    CHECK(events[2].button == os::kMouseLeft);
    CHECK(events[2].down);
    CHECK(events[2].y == 21 * scale);
    CHECK(events[3].type == InputEventType::PointerMove);  // dragged
    CHECK(events[4].type == InputEventType::PointerButton);
    CHECK_FALSE(events[4].down);
    CHECK(events[5].button == os::kMouseRight);
    CHECK(events[6].button == os::kMouseRight);
    CHECK(events[7].button == os::kMouseMiddle);
    CHECK(events[9].button == os::kMouseBack);
    CHECK(events[11].button == os::kMouseForward);
    CHECK(events[13].type == InputEventType::PointerLeave);
}

TEST_CASE("macOS events: wheel lines are notches, trackpad points ten to the notch, positive right and down",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    [ViewOf(window) mouseMoved:MouseEvent(NSEventTypeMouseMoved, window, 30, 30)];
    Drain(window);
    NSEvent* lines = ScrollEvent(kCGScrollEventUnitLine, 1, 0);      // one line up
    NSEvent* pixels = ScrollEvent(kCGScrollEventUnitPixel, 0, -25);  // 25 points to the right
    REQUIRE_FALSE(lines.hasPreciseScrollingDeltas);
    REQUIRE(pixels.hasPreciseScrollingDeltas);
    state.BeginFrame();
    [ViewOf(window) scrollWheel:lines];
    [ViewOf(window) scrollWheel:pixels];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 2);
    CHECK(events[0].type == InputEventType::Wheel);
    CHECK(events[0].x == 0.0);
    CHECK(events[0].y == -lines.scrollingDeltaY);  // up is negative; AppKit may accelerate the delta
    CHECK(events[0].y < 0.0);
    CHECK(events[1].x == pixels.scrollingDeltaX / -10.0);
    CHECK(events[1].x > 0.0);
    CHECK(state.scroll_dy == events[0].y * 10.0);
    CHECK(state.scroll_dx == events[1].x * 10.0);
}

TEST_CASE("macOS events: losing the focus releases every held key in the stream, then reports FocusLost",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSWindow* ns = WindowOf(window);
    [ns sendEvent:FlagsEvent(kVK_Command, NSEventModifierFlagCommand | NX_DEVICELCMDKEYMASK, window)];
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_W, @"w", @"w", NSEventModifierFlagCommand, window)];
    REQUIRE(state.keyboard.IsDown(Key::LeftSuper));
    REQUIRE(state.keyboard.IsDown(Key::W));
    Drain(window);
    // Cmd-Tab, as far as this window can tell: it stops being the key window with the keys down.
    window.OnFocusChanged(true);
    window.OnFocusChanged(false);
    const Vector<InputEvent> events = Drain(window);
    usize ups = 0;
    for (usize i = 0; i < events.Size(); ++i)
    {
        const InputEvent& e = events[i];
        ups += e.type == InputEventType::KeyUp ? 1 : 0;
    }
    REQUIRE(events.Size() >= 3);
    CHECK(ups == 2);
    CHECK(events[events.Size() - 1].type == InputEventType::FocusLost);
    CHECK(events[events.Size() - 2].type == InputEventType::KeyUp);
    CHECK_FALSE(state.keyboard.IsDown(Key::LeftSuper));
    CHECK_FALSE(state.keyboard.IsDown(Key::W));
    CHECK(state.keyboard.modifiers == 0);
}

TEST_CASE("macOS events: a second window taking the keyboard releases the first one's keys", "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow first;
    OpenQuiet(first, &state, "cocoa focus 1");
    [WindowOf(first) makeKeyWindow];
    first.PumpEvents();
    if (!WindowOf(first).keyWindow)
    {
        SKIP("this program is not the active one (another program has the keyboard)");
    }
    Drain(first);
    [WindowOf(first) sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_D, @"d", @"d", 0, first)];
    REQUIRE(state.keyboard.IsDown(Key::D));
    CocoaWindow second;
    REQUIRE(second.Open("cocoa focus 2", 200, 150, nullptr));
    second.PumpEvents();
    // The keyboard's events only: the real pointer may cross the windows meanwhile.
    Vector<InputEvent> events;
    const Vector<InputEvent> all = Drain(first);
    for (usize i = 0; i < all.Size(); ++i)
    {
        if (all[i].type == InputEventType::KeyUp || all[i].type == InputEventType::FocusLost ||
            all[i].type == InputEventType::FocusGained)
        {
            events.Push(all[i]);
        }
    }
    REQUIRE(events.Size() >= 2);
    CHECK(events[events.Size() - 2].type == InputEventType::KeyUp);
    CHECK(events[events.Size() - 2].key == Key::D);
    CHECK(events[events.Size() - 1].type == InputEventType::FocusLost);
    CHECK_FALSE(state.keyboard.IsDown(Key::D));
}

TEST_CASE("macOS events: an undrained queue keeps the oldest events in order and counts the dropped ones",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window, nullptr);
    const usize sent = os::InputEventQueue::kCapacity + 50;
    for (usize i = 0; i < sent; ++i)
    {
        [ViewOf(window) mouseMoved:MouseEvent(NSEventTypeMouseMoved, window, static_cast<CGFloat>(i % 300), 5)];
    }
    CHECK(window.DroppedEvents() == sent + 1 - os::InputEventQueue::kCapacity);  // the enter took one place
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == os::InputEventQueue::kCapacity);
    CHECK(events[0].type == InputEventType::PointerEnter);
    for (usize i = 1; i < events.Size(); ++i)
    {
        CHECK(events[i].x == static_cast<f64>((i - 1) % 300) * window.ContentScale());
    }
}

TEST_CASE("macOS events: a window reopened after Close holds no key and no modifier from before",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    [WindowOf(window) sendEvent:FlagsEvent(kVK_Shift, NSEventModifierFlagShift | NX_DEVICELSHIFTKEYMASK, window)];
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_A, @"A", @"A", NSEventModifierFlagShift, window)];
    window.Close();
    const Vector<InputEvent> closing = Drain(window);
    CHECK(CountOf(closing, InputEventType::KeyUp) == 2);
    CHECK_FALSE(state.keyboard.IsDown(Key::A));
    CHECK(state.keyboard.modifiers == 0);
    OpenQuiet(window, &state);
    // The same key coming up now is not reported: the new window never saw it go down.
    [WindowOf(window) sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_A, @"a", @"a", 0, window)];
    CHECK(CountOf(Drain(window), InputEventType::KeyUp) == 0);
}

// Hand check, Day 296: a held letter gave KeyDown repeats without Text: press-and-hold took the repeats for its accent
// menu. The application turns it off in the registration domain only.
TEST_CASE(
    "macOS: press-and-hold is off for the program, in the registration domain only, so a held key repeats its text",
    "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    REQUIRE(os::cocoa::EnsureApplication());
    NSDictionary* registered = [[NSUserDefaults standardUserDefaults] volatileDomainForName:NSRegistrationDomain];
    CHECK([registered[@"ApplePressAndHoldEnabled"] isEqual:@NO]);
}

// Review findings, Day 296.

TEST_CASE(
    "macOS events: two keys of one name each go down and up, and InputState keeps the Key down while either is held",
    "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSWindow* ns = WindowOf(window);
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_Keypad1, @"1", @"1", NSEventModifierFlagNumericPad, window)];
    [ns sendEvent:KeyEvent(NSEventTypeKeyDown, kVK_ANSI_1, @"1", @"1", 0, window)];
    [ns sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_1, @"1", @"1", 0, window)];
    CHECK(state.keyboard.IsDown(Key::Num1));  // the pad's 1 is still held
    [ns sendEvent:KeyEvent(NSEventTypeKeyUp, kVK_ANSI_Keypad1, @"1", @"1", NSEventModifierFlagNumericPad, window)];
    CHECK_FALSE(state.keyboard.IsDown(Key::Num1));
    const Vector<InputEvent> all = Drain(window);
    CHECK(CountOf(all, InputEventType::KeyDown) == 2);
    CHECK(CountOf(all, InputEventType::KeyUp) == 2);
}

TEST_CASE("macOS events: a move outside the window with no button held is not reported; a drag outside is",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    OpenQuiet(window, &state);
    NSView* view = ViewOf(window);
    [view mouseMoved:MouseEvent(NSEventTypeMouseMoved, window, 10, 10)];
    [view mouseExited:MouseEvent(NSEventTypeMouseMoved, window, -5, 10)];
    [view mouseMoved:MouseEvent(NSEventTypeMouseMoved, window, -5, 10)];  // a key window may still be told this
    [view mouseDragged:MouseEvent(NSEventTypeLeftMouseDragged, window, -8, 10)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 4);
    CHECK(events[0].type == InputEventType::PointerEnter);
    CHECK(events[1].type == InputEventType::PointerMove);
    CHECK(events[2].type == InputEventType::PointerLeave);
    CHECK(events[3].type == InputEventType::PointerMove);
    CHECK(events[3].x < 0.0);
    CHECK_FALSE(state.pointer_inside);
}

// Day 297: an NSString may hold an unpaired surrogate, for which UTF8String gives nothing: the scalar becomes U+FFFD.
TEST_CASE("macOS events: inserted text with an unpaired surrogate gives U+FFFD for it and keeps the rest",
          "[macos][window][events]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window, nullptr);
    NSView<NSTextInputClient>* view = (NSView<NSTextInputClient>*)ViewOf(window);
    const unichar units[] = {'a', 0xD800, 'b'};
    [view insertText:[NSString stringWithCharacters:units length:3] replacementRange:NSMakeRange(NSNotFound, 0)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 3);
    CHECK(TextIs(events[0], "a"));
    CHECK(TextIs(events[1], "\xEF\xBF\xBD"));
    CHECK(TextIs(events[2], "b"));
}
