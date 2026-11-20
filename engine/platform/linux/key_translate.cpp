#include "platform/linux/key_translate.h"

// Header-only keysym constants (no libxkbcommon symbols are used here);
// the numbering is the X11 keysym numbering, so the X11 backend shares
// this translation.
#include <xkbcommon/xkbcommon-keysyms.h>

namespace vse::os
{

Key KeyFromKeysym(u32 keysym)
{
    if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
    {
        return static_cast<Key>(static_cast<u32>(Key::A) + (keysym - XKB_KEY_a));
    }
    if (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z)
    {
        return static_cast<Key>(static_cast<u32>(Key::A) + (keysym - XKB_KEY_A));
    }
    if (keysym >= XKB_KEY_0 && keysym <= XKB_KEY_9)
    {
        return static_cast<Key>(static_cast<u32>(Key::Num0) + (keysym - XKB_KEY_0));
    }
    if (keysym >= XKB_KEY_KP_0 && keysym <= XKB_KEY_KP_9)
    {
        return static_cast<Key>(static_cast<u32>(Key::Num0) + (keysym - XKB_KEY_KP_0));
    }
    if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12)
    {
        return static_cast<Key>(static_cast<u32>(Key::F1) + (keysym - XKB_KEY_F1));
    }

    switch (keysym)
    {
    case XKB_KEY_Escape: return Key::Escape;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: return Key::Enter;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case XKB_KEY_BackSpace: return Key::Backspace;
    case XKB_KEY_space: return Key::Space;
    case XKB_KEY_Insert: return Key::Insert;
    case XKB_KEY_Delete: return Key::Delete;
    case XKB_KEY_Home: return Key::Home;
    case XKB_KEY_End: return Key::End;
    case XKB_KEY_Page_Up: return Key::PageUp;
    case XKB_KEY_Page_Down: return Key::PageDown;
    case XKB_KEY_Left: return Key::Left;
    case XKB_KEY_Right: return Key::Right;
    case XKB_KEY_Up: return Key::Up;
    case XKB_KEY_Down: return Key::Down;
    case XKB_KEY_Shift_L: return Key::LeftShift;
    case XKB_KEY_Shift_R: return Key::RightShift;
    case XKB_KEY_Control_L: return Key::LeftCtrl;
    case XKB_KEY_Control_R: return Key::RightCtrl;
    case XKB_KEY_Alt_L: return Key::LeftAlt;
    case XKB_KEY_Alt_R:
    case XKB_KEY_ISO_Level3_Shift: return Key::RightAlt;
    case XKB_KEY_Super_L: return Key::LeftSuper;
    case XKB_KEY_Super_R: return Key::RightSuper;
    case XKB_KEY_Caps_Lock: return Key::CapsLock;
    case XKB_KEY_minus: return Key::Minus;
    case XKB_KEY_equal: return Key::Equals;
    case XKB_KEY_bracketleft: return Key::LeftBracket;
    case XKB_KEY_bracketright: return Key::RightBracket;
    case XKB_KEY_backslash: return Key::Backslash;
    case XKB_KEY_semicolon: return Key::Semicolon;
    case XKB_KEY_apostrophe: return Key::Apostrophe;
    case XKB_KEY_grave: return Key::Grave;
    case XKB_KEY_comma: return Key::Comma;
    case XKB_KEY_period: return Key::Period;
    case XKB_KEY_slash: return Key::Slash;
    default: return Key::Unknown;
    }
}

}  // namespace vse::os
