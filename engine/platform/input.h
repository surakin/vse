#pragma once

#include "types.h"

//! Keyboard and mouse input types shared by every platform backend.
//!
//! Keys are layout-aware: they name the symbol the user's layout produces, not the physical position. Each backend gives the state the
//! same meaning: on Linux from XKB keysyms (`platform/linux/key_translate.h`), on Windows from the layout's character for a key that types
//! one and the scan code for the others (`platform/windows/win32_keys.h`). The differences on Windows: AltGr is the right Alt alone and no
//! modifier, as XKB's level-3 shift is; a focus loss also releases the mouse buttons (Windows hands the mouse to the other window, where
//! the X11 and Wayland servers still deliver the release); and the wheel is scaled to the Linux backends' 10 units per notch. There is no
//! text input yet on any backend.

namespace engine_placeholder::os {

/// Layout-aware keyboard key identifiers.
enum class Key : u8 {
    Unknown = 0, ///< Key not in this enum.
    A, ///< Letter A.
    B, ///< Letter B.
    C, ///< Letter C.
    D, ///< Letter D.
    E, ///< Letter E.
    F, ///< Letter F.
    G, ///< Letter G.
    H, ///< Letter H.
    I, ///< Letter I.
    J, ///< Letter J.
    K, ///< Letter K.
    L, ///< Letter L.
    M, ///< Letter M.
    N, ///< Letter N.
    O, ///< Letter O.
    P, ///< Letter P.
    Q, ///< Letter Q.
    R, ///< Letter R.
    S, ///< Letter S.
    T, ///< Letter T.
    U, ///< Letter U.
    V, ///< Letter V.
    W, ///< Letter W.
    X, ///< Letter X.
    Y, ///< Letter Y.
    Z, ///< Letter Z.
    Num0, ///< Digit 0 on the main row.
    Num1, ///< Digit 1 on the main row.
    Num2, ///< Digit 2 on the main row.
    Num3, ///< Digit 3 on the main row.
    Num4, ///< Digit 4 on the main row.
    Num5, ///< Digit 5 on the main row.
    Num6, ///< Digit 6 on the main row.
    Num7, ///< Digit 7 on the main row.
    Num8, ///< Digit 8 on the main row.
    Num9, ///< Digit 9 on the main row.
    F1, ///< Function key F1.
    F2, ///< Function key F2.
    F3, ///< Function key F3.
    F4, ///< Function key F4.
    F5, ///< Function key F5.
    F6, ///< Function key F6.
    F7, ///< Function key F7.
    F8, ///< Function key F8.
    F9, ///< Function key F9.
    F10, ///< Function key F10.
    F11, ///< Function key F11.
    F12, ///< Function key F12.
    Escape, ///< Escape.
    Enter, ///< Enter or Return.
    Tab, ///< Tab.
    Backspace, ///< Backspace.
    Space, ///< Space bar.
    Insert, ///< Insert.
    Delete, ///< Delete.
    Home, ///< Home.
    End, ///< End.
    PageUp, ///< Page Up.
    PageDown, ///< Page Down.
    Left, ///< Left arrow.
    Right, ///< Right arrow.
    Up, ///< Up arrow.
    Down, ///< Down arrow.
    LeftShift, ///< Left Shift.
    RightShift, ///< Right Shift.
    LeftCtrl, ///< Left Control.
    RightCtrl, ///< Right Control.
    LeftAlt, ///< Left Alt.
    RightAlt, ///< Right Alt.
    LeftSuper, ///< Left Super (Windows or Command key).
    RightSuper, ///< Right Super (Windows or Command key).
    CapsLock, ///< Caps Lock.
    Minus, ///< Minus or hyphen.
    Equals, ///< Equals sign.
    LeftBracket, ///< Left square bracket.
    RightBracket, ///< Right square bracket.
    Backslash, ///< Backslash.
    Semicolon, ///< Semicolon.
    Apostrophe, ///< Apostrophe or quote.
    Grave, ///< Grave accent or backtick.
    Comma, ///< Comma.
    Period, ///< Period or full stop.
    Slash, ///< Forward slash.
    Count, ///< Number of keys; not a real key.
};

/// Bits of the modifier mask, set for logically active modifiers.
///
/// This is the effective state, so a latched or locked modifier counts, not just a held one.
enum ModifierBit : u32 {
    kModShift = 1u << 0,    ///< Shift is active.
    kModCtrl = 1u << 1,     ///< Control is active.
    kModAlt = 1u << 2,      ///< Alt is active.
    kModSuper = 1u << 3,    ///< Super is active.
    kModCapsLock = 1u << 4, ///< Caps Lock is on.
    kModNumLock = 1u << 5,  ///< Num Lock is on.
};

/// Per-key down state plus the modifier mask; part of [[os::InputState]].
///
/// - Thread-safety: thread-compatible. Written by the backend during event pumping and read by the owner;
/// the caller must synchronize if those happen on different threads.
struct KeyboardState {
    /// Number of tracked keys, including `Key::Unknown` at index 0.
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static_assert(kKeyCount <= 128, "KeyboardState::down_bits_ holds 128 keys");

    /// Returns whether `key` is currently held down; `Key::Unknown` and out-of-range keys are never down.
    /// - Param key: the key to query.
    bool IsDown(Key key) const {
        usize i = static_cast<usize>(key);
        return i < kKeyCount && (down_bits_[i / 64] >> (i % 64) & 1u) != 0;
    }

    /// Records `key` as down or up; `Key::Unknown` and out-of-range keys are ignored.
    /// - Param key: the key to update.
    /// - Param down: true if the key is now held down.
    void SetDown(Key key, bool down) {
        usize i = static_cast<usize>(key);
        if (i == 0 || i >= kKeyCount) {
            return;
        }
        u64 bit = u64{1} << (i % 64);
        if (down) {
            down_bits_[i / 64] |= bit;
        } else {
            down_bits_[i / 64] &= ~bit;
        }
    }

    /// Marks every key as up; the modifier mask is left unchanged.
    void ReleaseAll() {
        down_bits_[0] = 0;
        down_bits_[1] = 0;
    }

    /// Mask of active [[os::ModifierBit]] values.
    u32 modifiers = 0;

private:
    u64 down_bits_[2] = {0, 0};
};

/// Bits of the mouse button mask.
enum MouseButtonBit : u32 {
    kMouseLeft = 1u << 0,    ///< Left button.
    kMouseRight = 1u << 1,   ///< Right button.
    kMouseMiddle = 1u << 2,  ///< Middle button (wheel click).
    kMouseBack = 1u << 3,    ///< Back ("side") button.
    kMouseForward = 1u << 4, ///< Forward ("extra") button.
};

/// Everything the platform layer knows about input right now.
///
/// Written by the active window backend while it pumps events, and polled by upper layers.
///
/// - Thread-safety: thread-compatible. Written during [[os::Window::PumpEvents]] and read by the owner;
/// the caller must synchronize if those happen on different threads.
struct InputState {
    /// Keyboard key and modifier state.
    KeyboardState keyboard;

    /// Pointer x position in window-local surface coordinates; the last known value when the pointer is outside.
    f64 mouse_x = 0.0;
    /// Pointer y position in window-local surface coordinates; the last known value when the pointer is outside.
    f64 mouse_y = 0.0;
    /// True while the pointer is inside the window.
    bool pointer_inside = false;
    /// Mask of held [[os::MouseButtonBit]] values.
    u32 mouse_buttons = 0;

    /// Horizontal scroll accumulated since the last `BeginFrame`; positive is scrolling right.
    f64 scroll_dx = 0.0;
    /// Vertical scroll accumulated since the last `BeginFrame`; positive is scrolling down.
    f64 scroll_dy = 0.0;

    /// Returns whether any button in `button_bit` is held.
    /// - Param button_bit: a [[os::MouseButtonBit]] value or a mask of them.
    bool IsMouseDown(u32 button_bit) const { return (mouse_buttons & button_bit) != 0; }

    /// Resets per-frame deltas (scroll).
    ///
    /// Call once per frame before pumping platform events so scroll does not accumulate forever.
    void BeginFrame() {
        scroll_dx = 0.0;
        scroll_dy = 0.0;
    }
};

} // namespace engine_placeholder::os
