# Phase 1 / Day 17 — Wayland Keyboard Input

**Phase:** [Phase 1 — Linux Platform Layer](../phase-01-linux-platform.md)
**Depends on:** Day 16 (Wayland event loop).

Uses `libxkbcommon` for keymap interpretation instead of hand-parsing
the XKB text format — see this phase's Notes section for why that's a
new named dependency to flag/confirm explicitly.

## Tasks

- [ ] Add `libxkbcommon` (headers + link) to CMake.
- [ ] Call `wl_seat_get_keyboard` and install its listener (`keymap`,
  `enter`, `leave`, `key`, `modifiers`).
- [ ] In the `keymap` callback, build an `xkb_context`
  (`xkb_context_new`) and `xkb_keymap`
  (`xkb_keymap_new_from_string`/`_from_fd`) from the compositor-provided
  keymap, then an `xkb_state` from it.
- [ ] In the `key` callback, translate the keycode through
  `xkb_state_key_get_sym` into an engine key enum value; update
  `xkb_state` from the `modifiers` callback via
  `xkb_state_update_mask`.
- [ ] Test key presses under at least two different real keyboard
  layouts (e.g. US QWERTY and one non-US layout) and confirm correct
  engine key enum values for both.
- [ ] Commit.

**Done:** key presses on any real keyboard layout (not just US QWERTY)
produce correct engine key enum values.
