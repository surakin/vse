// macOS key events to Key (Phase 21 Day 296; platform/macos/cocoa_keys.h says what the names mean). The codes are
// HIToolbox's kVK_ values and the flags AppKit's NSEventModifierFlag and IOKit's NX_DEVICE masks, written as numbers so
// this file needs no Apple header; tests/macos/cocoa_input_macos_test.mm checks every one against the SDK's constants.
#include "platform/macos/cocoa_keys.h"

namespace engine_placeholder::os::cocoa
{

namespace
{

// NSEventModifierFlag values.
constexpr u64 kFlagCapsLock = 1ull << 16;
constexpr u64 kFlagShift = 1ull << 17;
constexpr u64 kFlagControl = 1ull << 18;
constexpr u64 kFlagOption = 1ull << 19;
constexpr u64 kFlagCommand = 1ull << 20;

// The device-dependent bits (NX_DEVICE*KEYMASK) in the low word of modifierFlags: which side is held.
constexpr u64 kDeviceLeftControl = 0x0001;
constexpr u64 kDeviceLeftShift = 0x0002;
constexpr u64 kDeviceRightShift = 0x0004;
constexpr u64 kDeviceLeftCommand = 0x0008;
constexpr u64 kDeviceRightCommand = 0x0010;
constexpr u64 kDeviceLeftOption = 0x0020;
constexpr u64 kDeviceRightOption = 0x0040;
constexpr u64 kDeviceRightControl = 0x2000;

// kVK_ codes of the keys named by position.
enum : u32
{
    kVkReturn = 0x24,
    kVkTab = 0x30,
    kVkSpace = 0x31,
    kVkDelete = 0x33,  // the Delete above Return: Backspace
    kVkEscape = 0x35,
    kVkRightCommand = 0x36,
    kVkCommand = 0x37,
    kVkShift = 0x38,
    kVkCapsLock = 0x39,
    kVkOption = 0x3A,
    kVkControl = 0x3B,
    kVkRightShift = 0x3C,
    kVkRightOption = 0x3D,
    kVkRightControl = 0x3E,
    kVkFunction = 0x3F,
    kVkF17 = 0x40,
    kVkKeypadDecimal = 0x41,
    kVkKeypadMultiply = 0x43,
    kVkKeypadPlus = 0x45,
    kVkKeypadClear = 0x47,
    kVkVolumeUp = 0x48,
    kVkVolumeDown = 0x49,
    kVkMute = 0x4A,
    kVkKeypadDivide = 0x4B,
    kVkKeypadEnter = 0x4C,
    kVkKeypadMinus = 0x4E,
    kVkF18 = 0x4F,
    kVkF19 = 0x50,
    kVkKeypadEquals = 0x51,
    kVkKeypad0 = 0x52,
    kVkKeypad1 = 0x53,
    kVkKeypad2 = 0x54,
    kVkKeypad3 = 0x55,
    kVkKeypad4 = 0x56,
    kVkKeypad5 = 0x57,
    kVkKeypad6 = 0x58,
    kVkKeypad7 = 0x59,
    kVkF20 = 0x5A,
    kVkKeypad8 = 0x5B,
    kVkKeypad9 = 0x5C,
    kVkJisKeypadComma = 0x5F,
    kVkF5 = 0x60,
    kVkF6 = 0x61,
    kVkF7 = 0x62,
    kVkF3 = 0x63,
    kVkF8 = 0x64,
    kVkF9 = 0x65,
    kVkJisEisu = 0x66,
    kVkF11 = 0x67,
    kVkJisKana = 0x68,
    kVkF13 = 0x69,
    kVkF16 = 0x6A,
    kVkF14 = 0x6B,
    kVkF10 = 0x6D,
    kVkF12 = 0x6F,
    kVkF15 = 0x71,
    kVkHelp = 0x72,
    kVkHome = 0x73,
    kVkPageUp = 0x74,
    kVkForwardDelete = 0x75,
    kVkF4 = 0x76,
    kVkEnd = 0x77,
    kVkF2 = 0x78,
    kVkPageDown = 0x79,
    kVkF1 = 0x7A,
    kVkLeftArrow = 0x7B,
    kVkRightArrow = 0x7C,
    kVkDownArrow = 0x7D,
    kVkUpArrow = 0x7E,
};

Key KeyOffset(Key first, u32 offset)
{
    return static_cast<Key>(static_cast<u32>(first) + offset);
}

// The key a layout's character names, as KeyFromKeysym names the same keysym on Linux.
Key KeyFromCharacter(u32 c)
{
    if (c >= 'a' && c <= 'z')
    {
        return KeyOffset(Key::A, c - 'a');
    }
    if (c >= 'A' && c <= 'Z')
    {
        return KeyOffset(Key::A, c - 'A');
    }
    if (c >= '0' && c <= '9')
    {
        return KeyOffset(Key::Num0, c - '0');
    }
    switch (c)
    {
    case ' ': return Key::Space;
    case '-': return Key::Minus;
    case '=': return Key::Equals;
    case '[': return Key::LeftBracket;
    case ']': return Key::RightBracket;
    case '\\': return Key::Backslash;
    case ';': return Key::Semicolon;
    case '\'': return Key::Apostrophe;
    case '`': return Key::Grave;
    case ',': return Key::Comma;
    case '.': return Key::Period;
    case '/': return Key::Slash;
    default: return Key::Unknown;
    }
}

}  // namespace

Key KeyFromCocoa(u32 key_code, u32 character)
{
    switch (key_code)
    {
    case kVkReturn:
    case kVkKeypadEnter: return Key::Enter;
    case kVkTab: return Key::Tab;
    case kVkSpace: return Key::Space;
    case kVkDelete: return Key::Backspace;
    case kVkEscape: return Key::Escape;
    case kVkShift: return Key::LeftShift;
    case kVkRightShift: return Key::RightShift;
    case kVkControl: return Key::LeftCtrl;
    case kVkRightControl: return Key::RightCtrl;
    case kVkOption: return Key::LeftAlt;
    case kVkRightOption: return Key::RightAlt;
    case kVkCommand: return Key::LeftSuper;
    case kVkRightCommand: return Key::RightSuper;
    case kVkCapsLock: return Key::CapsLock;
    case kVkF1: return Key::F1;
    case kVkF2: return Key::F2;
    case kVkF3: return Key::F3;
    case kVkF4: return Key::F4;
    case kVkF5: return Key::F5;
    case kVkF6: return Key::F6;
    case kVkF7: return Key::F7;
    case kVkF8: return Key::F8;
    case kVkF9: return Key::F9;
    case kVkF10: return Key::F10;
    case kVkF11: return Key::F11;
    case kVkF12: return Key::F12;
    case kVkHelp: return Key::Insert;
    case kVkForwardDelete: return Key::Delete;
    case kVkHome: return Key::Home;
    case kVkEnd: return Key::End;
    case kVkPageUp: return Key::PageUp;
    case kVkPageDown: return Key::PageDown;
    case kVkLeftArrow: return Key::Left;
    case kVkRightArrow: return Key::Right;
    case kVkUpArrow: return Key::Up;
    case kVkDownArrow: return Key::Down;
    case kVkKeypad0: return Key::Num0;
    case kVkKeypad1: return Key::Num1;
    case kVkKeypad2: return Key::Num2;
    case kVkKeypad3: return Key::Num3;
    case kVkKeypad4: return Key::Num4;
    case kVkKeypad5: return Key::Num5;
    case kVkKeypad6: return Key::Num6;
    case kVkKeypad7: return Key::Num7;
    case kVkKeypad8: return Key::Num8;
    case kVkKeypad9: return Key::Num9;
    // Keys the enum has no name for (on Linux: KP_Decimal, KP_Add, KP_Equal, XF86 volume keys, F13 and up).
    case kVkFunction:
    case kVkF13:
    case kVkF14:
    case kVkF15:
    case kVkF16:
    case kVkF17:
    case kVkF18:
    case kVkF19:
    case kVkF20:
    case kVkKeypadDecimal:
    case kVkKeypadMultiply:
    case kVkKeypadPlus:
    case kVkKeypadClear:
    case kVkKeypadDivide:
    case kVkKeypadMinus:
    case kVkKeypadEquals:
    case kVkJisKeypadComma:
    case kVkVolumeUp:
    case kVkVolumeDown:
    case kVkMute:
    case kVkJisEisu:
    case kVkJisKana: return Key::Unknown;
    default: break;
    }
    if (key_code >= kCocoaKeyCodeCount)
    {
        return Key::Unknown;
    }
    // A key that types something (letters, the digit row, punctuation): named by the layout's character, 0 for a dead
    // key.
    return KeyFromCharacter(character);
}

u32 ModifiersFromCocoa(u64 flags)
{
    u32 mask = 0;
    if ((flags & kFlagShift) != 0)
    {
        mask |= kModShift;
    }
    if ((flags & kFlagControl) != 0)
    {
        mask |= kModCtrl;
    }
    if ((flags & kFlagOption) != 0)
    {
        mask |= kModAlt;
    }
    if ((flags & kFlagCommand) != 0)
    {
        mask |= kModSuper;
    }
    if ((flags & kFlagCapsLock) != 0)
    {
        mask |= kModCapsLock;
    }
    return mask;
}

bool IsCocoaModifierKey(u32 key_code)
{
    switch (key_code)
    {
    case kVkShift:
    case kVkRightShift:
    case kVkControl:
    case kVkRightControl:
    case kVkOption:
    case kVkRightOption:
    case kVkCommand:
    case kVkRightCommand:
    case kVkCapsLock:
    case kVkFunction: return true;
    default: return false;
    }
}

namespace
{

// One side of a modifier: down if its device bit says so; if the event carries no device bit for either side, the
// modifier's own flag decides.
bool SideDown(u64 flags, u64 this_side, u64 other_side, u64 modifier)
{
    if ((flags & (this_side | other_side)) != 0)
    {
        return (flags & this_side) != 0;
    }
    return (flags & modifier) != 0;
}

}  // namespace

bool CocoaModifierKeyDown(u32 key_code, u64 flags)
{
    switch (key_code)
    {
    case kVkShift: return SideDown(flags, kDeviceLeftShift, kDeviceRightShift, kFlagShift);
    case kVkRightShift: return SideDown(flags, kDeviceRightShift, kDeviceLeftShift, kFlagShift);
    case kVkControl: return SideDown(flags, kDeviceLeftControl, kDeviceRightControl, kFlagControl);
    case kVkRightControl: return SideDown(flags, kDeviceRightControl, kDeviceLeftControl, kFlagControl);
    case kVkOption: return SideDown(flags, kDeviceLeftOption, kDeviceRightOption, kFlagOption);
    case kVkRightOption: return SideDown(flags, kDeviceRightOption, kDeviceLeftOption, kFlagOption);
    case kVkCommand: return SideDown(flags, kDeviceLeftCommand, kDeviceRightCommand, kFlagCommand);
    case kVkRightCommand: return SideDown(flags, kDeviceRightCommand, kDeviceLeftCommand, kFlagCommand);
    case kVkCapsLock: return (flags & kFlagCapsLock) != 0;
    case kVkFunction: return (flags & (1ull << 23)) != 0;  // NSEventModifierFlagFunction
    default: return false;
    }
}

bool IsTypedScalar(u32 c)
{
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0))
    {
        return false;  // control characters: Enter, Tab, Backspace, Escape and Delete among them
    }
    return c < 0xF700 || c > 0xF8FF;  // NSUpArrowFunctionKey and the rest
}

CocoaWheel CocoaWheelNotches(f64 delta_x, f64 delta_y, bool precise)
{
    const f64 divisor = precise ? kCocoaPointsPerNotch : 1.0;
    CocoaWheel wheel;
    wheel.dx = -delta_x / divisor;
    wheel.dy = -delta_y / divisor;
    return wheel;
}

}  // namespace engine_placeholder::os::cocoa
