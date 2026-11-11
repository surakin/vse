#pragma once

#include "platform/input.h"
#include "types.h"

//! Win32 key messages to [[os::Key]] (Phase 13 Day 159), with the meaning the Linux backends give keys: layout-aware.
//!
//! A key that types a character is named by the character the active layout gives it, as XKB names it by its keysym: on
//! a French AZERTY layout the key in the Q position is `Key::A`, and the `&` key is `Key::Num1` only with Shift. The
//! character is computed with the modifiers that choose a level (Shift, Caps Lock, AltGr), not Ctrl or Alt, so Ctrl+A
//! is still `Key::A`. Every other key is named by its virtual-key code and its scan code: left and right Shift by the
//! scan code, left and right Ctrl and Alt by the extended bit, and the navigation keys of the number pad (NumLock off)
//! apart from the separate ones, which are `Unknown` as XKB's `KP_Home` and friends are. Keys the enum has no name for
//! (Pause, Print Screen, NumLock, the number pad's operators and decimal point) are `Unknown`.

namespace engine_placeholder::os::win32
{

/// What a `WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN` or `WM_SYSKEYUP` says about the key, with the character the layout
/// gives it.
///
/// - Thread-safety: thread-compatible plain data.
struct Win32KeyMessage
{
    /// The virtual-key code (`wParam`).
    u32 virtual_key = 0;
    /// The scan code (`lParam` bits 16-23).
    u32 scan_code = 0;
    /// The extended-key flag (`lParam` bit 24): the right Ctrl and Alt, the separate navigation keys, the number pad's
    /// Enter.
    bool extended = false;
    /// The code point the active layout gives the key with the level modifiers held (Shift, Caps Lock, AltGr), or 0 for
    /// none or a dead key.
    u32 character = 0;
};

/// Returns the [[os::Key]] a key message names; `Key::Unknown` for keys the enum does not have.
///
/// - Param message: the key message and its character.
Key KeyFromWin32(const Win32KeyMessage& message);

/// The number of distinct [[os::win32::Win32KeyIndex]] values.
inline constexpr u32 kWin32KeyIndexCount = 512;

/// Returns an index that tells physical keys apart: the scan code with the extended flag as bit 8 (below
/// [[os::win32::kWin32KeyIndexCount]]).
///
/// - Param scan_code: the scan code (only the low 8 bits are used).
/// - Param extended: the extended-key flag.
inline u32 Win32KeyIndex(u32 scan_code, bool extended)
{
    return (scan_code & 0xFFu) | (extended ? 0x100u : 0u);
}

}  // namespace engine_placeholder::os::win32
