#pragma once

#include "types.h"

// Phase 1 Day 21: engine-level keyboard types shared by every platform
// backend (Wayland Day 21, X11 Day 25, later Windows/macOS/...). Keys
// are *layout-aware* (they name the symbol the user's layout produces,
// e.g. the physical Q key on AZERTY reports Key::A), which is what text-
// and shortcut-style input wants; physical-position scancodes for
// gameplay bindings (WASD regardless of layout) are a later input-layer
// concern, not this platform layer's.
namespace engine_placeholder::os {

enum class Key : u8 {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Escape, Enter, Tab, Backspace, Space,
    Insert, Delete, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, LeftSuper, RightSuper, CapsLock,
    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Grave, Comma, Period, Slash,
    Count,
};

// Bitmask of logically-active modifiers (effective state, so a latched
// or locked modifier counts, not just a held one).
enum ModifierBit : u32 {
    kModShift = 1u << 0,
    kModCtrl = 1u << 1,
    kModAlt = 1u << 2,
    kModSuper = 1u << 3,
    kModCapsLock = 1u << 4,
    kModNumLock = 1u << 5,
};

// Maps an X11/XKB keysym value (identical numbering in both worlds) to
// an engine Key; Key::Unknown for anything not in the enum (including
// non-Latin letters). Upper- and lower-case letters map to the same Key.
Key KeyFromKeysym(u32 keysym);

// Per-key down state plus the modifier mask. Part of InputState (Day 22).
struct KeyboardState {
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static_assert(kKeyCount <= 128, "KeyboardState::down_bits_ holds 128 keys");

    bool IsDown(Key key) const {
        usize i = static_cast<usize>(key);
        return i < kKeyCount && (down_bits_[i / 64] >> (i % 64) & 1u) != 0;
    }

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

    void ReleaseAll() {
        down_bits_[0] = 0;
        down_bits_[1] = 0;
    }

    u32 modifiers = 0;

private:
    u64 down_bits_[2] = {0, 0};
};

enum MouseButtonBit : u32 {
    kMouseLeft = 1u << 0,
    kMouseRight = 1u << 1,
    kMouseMiddle = 1u << 2,
    kMouseBack = 1u << 3,    // "side" button
    kMouseForward = 1u << 4, // "extra" button
};

// Phase 1 Day 22: everything the platform layer knows about input right
// now, written by whichever backend is active (Wayland Days 21-22, X11
// Day 25) and polled by upper layers. Room for touch/gamepad state is
// left to later phases (PLAN.md assumption #11) — add members, don't
// reshape these.
struct InputState {
    KeyboardState keyboard;

    // Pointer position in window-local surface coordinates, valid while
    // `pointer_inside` (last known position otherwise).
    f64 mouse_x = 0.0;
    f64 mouse_y = 0.0;
    bool pointer_inside = false;
    u32 mouse_buttons = 0; // MouseButtonBit mask

    // Scroll accumulated since the last BeginFrame(); positive y is
    // scrolling down, positive x is scrolling right (Wayland's sign
    // convention, X11 backend normalizes to match).
    f64 scroll_dx = 0.0;
    f64 scroll_dy = 0.0;

    bool IsMouseDown(u32 button_bit) const { return (mouse_buttons & button_bit) != 0; }

    // Call once per frame before pumping platform events so per-frame
    // deltas (scroll) don't accumulate forever.
    void BeginFrame() {
        scroll_dx = 0.0;
        scroll_dy = 0.0;
    }
};

} // namespace engine_placeholder::os
