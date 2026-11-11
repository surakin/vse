#pragma once

#include "platform/input.h"
#include "types.h"

//! XKB keyboard: libxkbcommon keymap and state used to translate evdev keycodes into engine keys.

struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;

namespace engine_placeholder::os
{

/// Owns a libxkbcommon context, keymap and state used to interpret keycodes.
///
/// Independent of any Wayland types, so it can be driven from named layouts
/// without a compositor.
///
/// - Thread-safety: not-thread-safe. Const methods may run concurrently with each other only.
/// - Ownership: owns the xkb context, keymap and state and releases them on destruction or re-init.
class XkbKeyboard
{
public:
    XkbKeyboard() = default;

    /// Releases the xkb context, keymap and state.
    ~XkbKeyboard();

    XkbKeyboard(const XkbKeyboard&) = delete;
    XkbKeyboard& operator=(const XkbKeyboard&) = delete;

    /// Builds a keymap from RMLVO names using the system xkeyboard-config data, replacing any existing keymap.
    /// - Param layout: XKB layout name, for example `"de"`.
    /// - Param variant: XKB variant name, or null for the layout default.
    /// - Returns: `true` on success.
    /// - Fails: returns `false` (logged) if the keymap cannot be built.
    bool InitFromNames(const char* layout, const char* variant = nullptr);

    /// Builds a keymap from XKB keymap text, such as the mapped `wl_keyboard` keymap fd.
    /// - Param text: keymap text; it need not be NUL-terminated and is not retained.
    /// - Param length: size of `text` in bytes.
    /// - Returns: `true` on success.
    /// - Fails: returns `false` (logged) if the text cannot be compiled.
    bool InitFromKeymapText(const char* text, usize length);

    /// Returns whether a keymap has been compiled and the keyboard is usable.
    bool Valid() const { return state_ != nullptr; }

    /// Translates a Linux evdev keycode to an engine `Key` under the current modifier state.
    ///
    /// The XKB keycode is the evdev code plus 8, which this function applies.
    /// - Param evdev_code: evdev keycode as sent by `wl_keyboard`.
    /// - Returns: the translated key, or `Key::Unknown` if not [[os::XkbKeyboard::Valid]] or unmapped.
    Key KeyForEvdevCode(u32 evdev_code) const;

    /// Applies a `wl_keyboard.modifiers` event to the keyboard state; does nothing if not valid.
    /// - Param depressed: depressed modifier mask.
    /// - Param latched: latched modifier mask.
    /// - Param locked: locked modifier mask.
    /// - Param group: effective layout group.
    void UpdateMask(u32 depressed, u32 latched, u32 locked, u32 group);

    /// Updates the state for a key press or release, as a compositor that sends no `modifiers` event would (X11 and
    /// tests); do not mix with [[os::XkbKeyboard::UpdateMask]] for the same keyboard.
    /// - Param evdev_code: evdev keycode.
    /// - Param pressed: true for a press, false for a release.
    void UpdateKey(u32 evdev_code, bool pressed);

    /// Loads the compose table for `locale` (for example `"en_US.UTF-8"`) so dead keys and compose sequences produce
    /// one composed character from [[os::XkbKeyboard::TextForEvdevCode]].
    /// - Param locale: the locale name; null or empty means `"C"`.
    /// - Returns: true if a table was loaded.
    /// - Fails: returns false (logged at info level) and keeps working without composition; a dead key then produces no
    /// text and the next key its own character.
    bool InitCompose(const char* locale);

    /// Returns the text a key press produces, as UTF-8, taking dead keys and compose sequences into account.
    ///
    /// Call once per **press** (never for a release or a repeat that the caller synthesizes from a press it already
    /// sent text for, unless the key should repeat its text), after the modifier state includes the key's own
    /// modifiers. A press with Ctrl or Alt active produces no text (it is a shortcut), nor does a control character
    /// (Enter, Tab, Backspace, Escape, Delete), nor a dead key or the middle of a compose sequence. The result is one
    /// Unicode scalar.
    /// - Param evdev_code: evdev keycode.
    /// - Param out: receives up to `capacity` bytes, not NUL-terminated.
    /// - Param capacity: size of `out`; 4 is enough.
    /// - Returns: the number of bytes written, 0 for none.
    usize TextForEvdevCode(u32 evdev_code, char* out, usize capacity);

    /// Returns whether the keymap says the key auto-repeats (modifiers and lock keys do not).
    /// - Param evdev_code: evdev keycode.
    /// - Returns: true if the key repeats; false when not valid.
    bool KeyRepeats(u32 evdev_code) const;

    /// Forgets a compose sequence in progress (call when focus is lost).
    void ResetCompose();

    /// Returns the effective modifier bitmask (`ModifierBit` values) under the current state.
    ///
    /// - Returns: the mask, or 0 if not valid.
    u32 Modifiers() const;

private:
    void Reset();
    bool FinishKeymap(xkb_keymap* keymap);

    xkb_context* context_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    xkb_state* state_ = nullptr;
    xkb_compose_table* compose_table_ = nullptr;
    xkb_compose_state* compose_state_ = nullptr;
};

}  // namespace engine_placeholder::os
