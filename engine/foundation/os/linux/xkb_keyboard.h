#pragma once

#include "os/input.h"
#include "types.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

// Phase 1 Day 21: thin owner of the libxkbcommon context/keymap/state
// triple used to interpret keycodes, kept separate from any Wayland
// types so it can be tested against named layouts (us/de/fr) without a
// compositor. libxkbcommon is the user-approved named dependency for
// keymap interpretation (see plans/phase-01-linux-platform.md Notes).
namespace engine_placeholder::os {

class XkbKeyboard {
public:
    XkbKeyboard() = default;
    ~XkbKeyboard();

    XkbKeyboard(const XkbKeyboard&) = delete;
    XkbKeyboard& operator=(const XkbKeyboard&) = delete;

    // Builds a keymap from RMLVO names (e.g. layout "de", variant
    // nullptr), using the system xkeyboard-config data. Replaces any
    // existing keymap. False (logged) on failure.
    bool InitFromNames(const char* layout, const char* variant = nullptr);

    // Builds a keymap from compositor-provided XKB text (wl_keyboard's
    // keymap fd, mmapped). `text`/`length` need not be NUL-terminated.
    bool InitFromKeymapText(const char* text, usize length);

    bool Valid() const { return state_ != nullptr; }

    // Translates a Linux evdev keycode (what wl_keyboard sends; the XKB
    // keycode is evdev + 8) to an engine Key under the current state.
    Key KeyForEvdevCode(u32 evdev_code) const;

    // Applies wl_keyboard.modifiers; returns false if not Valid().
    void UpdateMask(u32 depressed, u32 latched, u32 locked, u32 group);

    // Effective modifier bitmask (ModifierBit) under the current state.
    u32 Modifiers() const;

private:
    void Reset();
    bool FinishKeymap(xkb_keymap* keymap);

    xkb_context* context_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    xkb_state* state_ = nullptr;
};

} // namespace engine_placeholder::os
