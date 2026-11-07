#include "platform/linux/xkb_keyboard.h"

#include <xkbcommon/xkbcommon.h>

#include "log/log.h"
#include "platform/linux/key_translate.h"

namespace engine_placeholder::os {

namespace {

// XKB keycodes are evdev keycodes offset by 8 (X11 heritage).
constexpr u32 kEvdevToXkbOffset = 8;

} // namespace

XkbKeyboard::~XkbKeyboard() {
    Reset();
    if (context_ != nullptr) {
        xkb_context_unref(context_);
        context_ = nullptr;
    }
}

void XkbKeyboard::Reset() {
    if (state_ != nullptr) {
        xkb_state_unref(state_);
        state_ = nullptr;
    }
    if (keymap_ != nullptr) {
        xkb_keymap_unref(keymap_);
        keymap_ = nullptr;
    }
}

bool XkbKeyboard::FinishKeymap(xkb_keymap* keymap) {
    if (keymap == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "xkb: failed to compile keymap");
        return false;
    }
    xkb_state* state = xkb_state_new(keymap);
    if (state == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_state_new failed");
        xkb_keymap_unref(keymap);
        return false;
    }
    Reset();
    keymap_ = keymap;
    state_ = state;
    return true;
}

bool XkbKeyboard::InitFromNames(const char* layout, const char* variant) {
    if (context_ == nullptr) {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ == nullptr) {
            Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_context_new failed");
            return false;
        }
    }
    xkb_rule_names names = {};
    names.layout = layout;
    names.variant = variant;
    return FinishKeymap(xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS));
}

bool XkbKeyboard::InitFromKeymapText(const char* text, usize length) {
    if (context_ == nullptr) {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ == nullptr) {
            Log(LogPlatform, LogVerbosity::Error, "xkb: xkb_context_new failed");
            return false;
        }
    }
    // The Wayland keymap size includes a trailing NUL; from_buffer wants
    // the text length alone.
    if (length > 0 && text[length - 1] == '\0') {
        --length;
    }
    return FinishKeymap(
        xkb_keymap_new_from_buffer(context_, text, length, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
}

Key XkbKeyboard::KeyForEvdevCode(u32 evdev_code) const {
    if (state_ == nullptr) {
        return Key::Unknown;
    }
    xkb_keysym_t sym = xkb_state_key_get_one_sym(state_, evdev_code + kEvdevToXkbOffset);
    return KeyFromKeysym(sym);
}

void XkbKeyboard::UpdateMask(u32 depressed, u32 latched, u32 locked, u32 group) {
    if (state_ != nullptr) {
        xkb_state_update_mask(state_, depressed, latched, locked, 0, 0, group);
    }
}

u32 XkbKeyboard::Modifiers() const {
    if (state_ == nullptr) {
        return 0;
    }
    auto active = [this](const char* name) {
        return xkb_state_mod_name_is_active(state_, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    u32 mods = 0;
    if (active(XKB_MOD_NAME_SHIFT)) mods |= kModShift;
    if (active(XKB_MOD_NAME_CTRL)) mods |= kModCtrl;
    if (active(XKB_MOD_NAME_ALT)) mods |= kModAlt;
    if (active(XKB_MOD_NAME_LOGO)) mods |= kModSuper;
    if (active(XKB_MOD_NAME_CAPS)) mods |= kModCapsLock;
    if (active(XKB_MOD_NAME_NUM)) mods |= kModNumLock;
    return mods;
}

} // namespace engine_placeholder::os
