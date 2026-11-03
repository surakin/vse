# Phase 1 / Day 21 — Wayland Keyboard Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 20 (Wayland event loop).

Uses `libxkbcommon` for keymap interpretation instead of hand-parsing
the XKB text format — see this phase's Notes section for why that's a
new named dependency to flag/confirm explicitly.

## Tasks

- [x] Add `libxkbcommon` (headers + link) to CMake. User explicitly
  approved the dependency on 2026-11-03; linked via `pkg_check_modules`
  `PUBLIC` on `foundation`/`foundation_asan`.
- [x] Call `wl_seat_get_keyboard` and install its listener (`keymap`,
  `enter`, `leave`, `key`, `modifiers`).
- [ ] In the `keymap` callback, build an `xkb_context`
  (`xkb_context_new`) and `xkb_keymap`
  (`xkb_keymap_new_from_string`/`_from_fd`) from the compositor-provided
  keymap, then an `xkb_state` from it. Wrapped in
  `os::XkbKeyboard` (`os/linux/xkb_keyboard.h/.cpp`), kept free of
  Wayland types so it is testable against named layouts.
- [x] In the `key` callback, translate the keycode through
  `xkb_state_key_get_sym` into an engine key enum value; update
  `xkb_state` from the `modifiers` callback via
  `xkb_state_update_mask`. Engine `Key` enum + `KeyboardState` in
  `os/input.h`; `KeyFromKeysym` in `os/key_translate.cpp` (shared with
  the X11 backend, Day 25). Releases clear the Key the code was
  *pressed* as, so layout/modifier changes between press and release
  can't leave a key stuck. Focus loss releases everything.
- [x] Test key presses under at least two different real keyboard
  layouts (e.g. US QWERTY and one non-US layout) and confirm correct
  engine key enum values for both. Verified by compiling real `us`,
  `fr` (AZERTY) and `de` (QWERTZ) keymaps via libxkbcommon and checking
  the same physical evdev keys yield the right `Key` per layout
  (`tests/foundation/keyboard_input_test.cpp`). **Not done:** live
  keypresses on physical keyboards through the compositor — no way to
  type at the screen in this session. Non-Latin layouts (e.g. `ru`)
  yield `Key::Unknown` by design (layout-aware keys; physical
  scancodes are a later input-layer concern).
- [x] Commit.

**Done:** key presses on any real keyboard layout (not just US QWERTY)
produce correct engine key enum values.
