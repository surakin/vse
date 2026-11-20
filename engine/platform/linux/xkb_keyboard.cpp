#include "platform/linux/xkb_keyboard.h"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include "log/log.h"
#include "platform/linux/key_translate.h"

namespace vse::os
{

namespace
{

// XKB keycodes are evdev keycodes offset by 8 (X11 heritage).
constexpr u32 kEvdevToXkbOffset = 8;

}  // namespace

XkbKeyboard::~XkbKeyboard()
{
    Reset();
    if (compose_state_ != nullptr)
    {
        xkb_compose_state_unref(compose_state_);
        compose_state_ = nullptr;
    }
    if (compose_table_ != nullptr)
    {
        xkb_compose_table_unref(compose_table_);
        compose_table_ = nullptr;
    }
    if (context_ != nullptr)
    {
        xkb_context_unref(context_);
        context_ = nullptr;
    }
}

void XkbKeyboard::Reset()
{
    if (state_ != nullptr)
    {
        xkb_state_unref(state_);
        state_ = nullptr;
    }
    if (keymap_ != nullptr)
    {
        xkb_keymap_unref(keymap_);
        keymap_ = nullptr;
    }
}

bool XkbKeyboard::FinishKeymap(xkb_keymap* keymap)
{
    if (keymap == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "xkb: failed to compile keymap");
        return false;
    }
    xkb_state* state = xkb_state_new(keymap);
    if (state == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_state_new failed");
        xkb_keymap_unref(keymap);
        return false;
    }
    Reset();
    keymap_ = keymap;
    state_ = state;
    return true;
}

bool XkbKeyboard::InitFromNames(const char* layout, const char* variant)
{
    if (context_ == nullptr)
    {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ == nullptr)
        {
            Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_context_new failed");
            return false;
        }
    }
    xkb_rule_names names = {};
    names.layout = layout;
    names.variant = variant;
    return FinishKeymap(xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS));
}

bool XkbKeyboard::InitFromKeymapText(const char* text, usize length)
{
    if (context_ == nullptr)
    {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ == nullptr)
        {
            Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_context_new failed");
            return false;
        }
    }
    // The Wayland keymap size includes a trailing NUL; from_buffer wants
    // the text length alone.
    if (length > 0 && text[length - 1] == '\0')
    {
        --length;
    }
    return FinishKeymap(
        xkb_keymap_new_from_buffer(context_, text, length, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
}

Key XkbKeyboard::KeyForEvdevCode(u32 evdev_code) const
{
    if (state_ == nullptr)
    {
        return Key::Unknown;
    }
    xkb_keysym_t sym = xkb_state_key_get_one_sym(state_, evdev_code + kEvdevToXkbOffset);
    return KeyFromKeysym(sym);
}

void XkbKeyboard::UpdateMask(u32 depressed, u32 latched, u32 locked, u32 group)
{
    if (state_ != nullptr)
    {
        xkb_state_update_mask(state_, depressed, latched, locked, 0, 0, group);
    }
}

void XkbKeyboard::UpdateKey(u32 evdev_code, bool pressed)
{
    if (state_ != nullptr)
    {
        xkb_state_update_key(state_, evdev_code + kEvdevToXkbOffset, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    }
}

bool XkbKeyboard::InitCompose(const char* locale)
{
    if (context_ == nullptr)
    {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ == nullptr)
        {
            return false;
        }
    }
    if (compose_state_ != nullptr)
    {
        xkb_compose_state_unref(compose_state_);
        compose_state_ = nullptr;
    }
    if (compose_table_ != nullptr)
    {
        xkb_compose_table_unref(compose_table_);
        compose_table_ = nullptr;
    }
    compose_table_ = xkb_compose_table_new_from_locale(context_, locale != nullptr && locale[0] != '\0' ? locale : "C",
                                                       XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (compose_table_ == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Info,
            "xkb: no compose table for the locale; dead keys and compose sequences are not combined");
        return false;
    }
    compose_state_ = xkb_compose_state_new(compose_table_, XKB_COMPOSE_STATE_NO_FLAGS);
    return compose_state_ != nullptr;
}

bool XkbKeyboard::KeyRepeats(u32 evdev_code) const
{
    return keymap_ != nullptr && xkb_keymap_key_repeats(keymap_, evdev_code + kEvdevToXkbOffset) != 0;
}

void XkbKeyboard::ResetCompose()
{
    if (compose_state_ != nullptr)
    {
        xkb_compose_state_reset(compose_state_);
    }
}

usize XkbKeyboard::TextForEvdevCode(u32 evdev_code, char* out, usize capacity)
{
    if (state_ == nullptr || out == nullptr || capacity == 0)
    {
        return 0;
    }
    const xkb_keycode_t code = evdev_code + kEvdevToXkbOffset;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(state_, code);
    if (compose_state_ != nullptr && sym != XKB_KEY_NoSymbol)
    {
        // The compose machine sees every press, including the ones that become shortcuts, so that a sequence is not
        // left half open.
        xkb_compose_state_feed(compose_state_, sym);
        switch (xkb_compose_state_get_status(compose_state_))
        {
        case XKB_COMPOSE_COMPOSING: return 0;  // a dead key or the middle of a sequence: no text yet
        case XKB_COMPOSE_CANCELLED: xkb_compose_state_reset(compose_state_); return 0;
        case XKB_COMPOSE_COMPOSED:
        {
            char buffer[8];
            const int n = xkb_compose_state_get_utf8(compose_state_, buffer, sizeof(buffer));
            xkb_compose_state_reset(compose_state_);
            if (n <= 0 || static_cast<usize>(n) > capacity)
            {
                return 0;
            }
            for (int i = 0; i < n; ++i)
            {
                out[i] = buffer[i];
            }
            return static_cast<usize>(n);
        }
        case XKB_COMPOSE_NOTHING: break;
        }
    }
    const u32 mods = Modifiers();
    if ((mods & (kModCtrl | kModAlt)) != 0)
    {
        return 0;  // a shortcut, not text
    }
    char buffer[8];
    const int n = xkb_state_key_get_utf8(state_, code, buffer, sizeof(buffer));
    if (n <= 0 || static_cast<usize>(n) > capacity)
    {
        return 0;
    }
    const u8 first = static_cast<u8>(buffer[0]);
    if (first < 0x20 || first == 0x7f)
    {
        return 0;  // Enter, Tab, Backspace, Escape, Delete: keys, not text
    }
    for (int i = 0; i < n; ++i)
    {
        out[i] = buffer[i];
    }
    return static_cast<usize>(n);
}

u32 XkbKeyboard::Modifiers() const
{
    if (state_ == nullptr)
    {
        return 0;
    }
    auto active = [this](const char* name)
    { return xkb_state_mod_name_is_active(state_, name, XKB_STATE_MODS_EFFECTIVE) > 0; };
    u32 mods = 0;
    if (active(XKB_MOD_NAME_SHIFT))
    {
        mods |= kModShift;
    }
    if (active(XKB_MOD_NAME_CTRL))
    {
        mods |= kModCtrl;
    }
    if (active(XKB_MOD_NAME_ALT))
    {
        mods |= kModAlt;
    }
    if (active(XKB_MOD_NAME_LOGO))
    {
        mods |= kModSuper;
    }
    if (active(XKB_MOD_NAME_CAPS))
    {
        mods |= kModCapsLock;
    }
    if (active(XKB_MOD_NAME_NUM))
    {
        mods |= kModNumLock;
    }
    return mods;
}

}  // namespace vse::os
