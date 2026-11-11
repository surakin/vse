// Win32 key messages to Key (Phase 13 Day 159; platform/windows/win32_keys.h says what the names mean).
#include "platform/windows/win32_keys.h"

#include "os/windows/win32.h"

namespace engine_placeholder::os::win32
{

namespace
{

constexpr u32 kRightShiftScanCode = 0x36;  // the left Shift is 0x2A

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

// The navigation keys: the separate ones carry the extended flag; the same virtual keys without it come from the number
// pad with NumLock off, which XKB reports as KP_Home and the like (not in the enum).
Key Navigation(Key key, bool extended)
{
    return extended ? key : Key::Unknown;
}

}  // namespace

Key KeyFromWin32(const Win32KeyMessage& m)
{
    const u32 vk = m.virtual_key;
    if (vk >= VK_F1 && vk <= VK_F12)
    {
        return KeyOffset(Key::F1, vk - VK_F1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        return KeyOffset(Key::Num0, vk - VK_NUMPAD0);  // NumLock on, as XKB's KP_0..KP_9
    }
    switch (vk)
    {
    case VK_ESCAPE: return Key::Escape;
    case VK_RETURN: return Key::Enter;  // the main Enter and the number pad's (extended)
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_SPACE: return Key::Space;
    case VK_INSERT: return Navigation(Key::Insert, m.extended);
    case VK_DELETE: return Navigation(Key::Delete, m.extended);
    case VK_HOME: return Navigation(Key::Home, m.extended);
    case VK_END: return Navigation(Key::End, m.extended);
    case VK_PRIOR: return Navigation(Key::PageUp, m.extended);
    case VK_NEXT: return Navigation(Key::PageDown, m.extended);
    case VK_LEFT: return Navigation(Key::Left, m.extended);
    case VK_RIGHT: return Navigation(Key::Right, m.extended);
    case VK_UP: return Navigation(Key::Up, m.extended);
    case VK_DOWN: return Navigation(Key::Down, m.extended);
    case VK_SHIFT: return (m.scan_code & 0xFFu) == kRightShiftScanCode ? Key::RightShift : Key::LeftShift;
    case VK_LSHIFT: return Key::LeftShift;
    case VK_RSHIFT: return Key::RightShift;
    case VK_CONTROL: return m.extended ? Key::RightCtrl : Key::LeftCtrl;
    case VK_LCONTROL: return Key::LeftCtrl;
    case VK_RCONTROL: return Key::RightCtrl;
    case VK_MENU: return m.extended ? Key::RightAlt : Key::LeftAlt;
    case VK_LMENU: return Key::LeftAlt;
    case VK_RMENU: return Key::RightAlt;
    case VK_LWIN: return Key::LeftSuper;
    case VK_RWIN: return Key::RightSuper;
    case VK_CAPITAL: return Key::CapsLock;
    // Keys the enum has no name for (on Linux: KP_Decimal, KP_Add, Pause, Print, Num_Lock, Scroll_Lock, Menu,
    // KP_Begin).
    case VK_DECIMAL:
    case VK_ADD:
    case VK_SUBTRACT:
    case VK_MULTIPLY:
    case VK_DIVIDE:
    case VK_SEPARATOR:
    case VK_PAUSE:
    case VK_SNAPSHOT:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_APPS:
    case VK_CLEAR: return Key::Unknown;
    default: break;
    }
    // A key that types something (letters, the digit row, punctuation): named by the layout's character, 0 for a dead
    // key.
    return KeyFromCharacter(m.character);
}

}  // namespace engine_placeholder::os::win32
