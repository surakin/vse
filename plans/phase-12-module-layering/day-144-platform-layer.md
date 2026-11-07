# Phase 12 / Day 144 — The Platform Layer: Windows and Input Leave the Foundation

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 143; the platform move's file list and namespace decision (Day 140, Spike C).

Move the windows and input code to `engine/platform/`, where `PLAN.md` always put it, so it stops being part of every foundation consumer
(`ARCHITECTURE.md` section 2, items 5, 9 and 10).

## Tasks

- [ ] **Move with `git mv`** (history kept): `os/window.h`, `os/window.cpp`, `os/input.h`, `os/key_translate.cpp` to `engine/platform/`; `os/linux/wayland/`,
      `os/linux/x11/`, `os/linux/xkb_keyboard.*`, `os/linux/platform_window_host.*` to `engine/platform/linux/`; `os/windows/FULLSCREEN.md` and
      `os/macos/FULLSCREEN.md` to `engine/platform/windows/` and `macos/`; the vendored Wayland protocol code and its `VENDORED.md` with the Wayland backend.
      `SESSION.md`, `session.h` and `gpu_driver_probe.h` stay with the OS services.
- [ ] **Neutral window host:** `PlatformWindowHost` gets a neutral header (`platform/window_host.h`) with the per-platform implementation behind it, so the demos
      and `engine_main` (or its replacement) include only neutral headers; `game/rhi_demo/main.cpp` and `game/renderer_demo/main.cpp` no longer include
      `os/linux/`.
- [ ] **Namespace and includes** per the spec (decision 4): every include line, using-declaration and documentation reference updated; no forwarding headers left
      at the old paths unless the spec allows them for one phase (then listed in the allow list with their removal day).
- [ ] **CMake:** `engine/platform/CMakeLists.txt` builds `platform` (and `platform_asan`/`platform_tsan` where the flavors need them) with the Linux display
      libraries linked here only; the foundation no longer links Wayland, X11 or xkbcommon; until Day 145 splits the foundation, `platform` links `foundation`.
      `tools/docgen` and `tools/shader_mint` no longer pull in display libraries (check with `ldd`).
- [ ] **Everything that names the old paths:** `tests/phases.txt` and `tests/module_map.txt` patterns (the window tests keep phase 1), the gates' allow lists
      (`platform_neutral_check`, `file_access_check`, `config_access_check`, `signal_safety_check`, `std_container_check`), `docs/reference/modules.txt` (the
      `platform/window-input` module), `docs/guide/platform-windowing-and-input.md`, the Phase 13 and 14 plan files' paths, `ARCHITECTURE.md` section 1.
- [ ] **Allow list:** entries for items 5, 9 and 10 deleted; `module_deps_check` reports no cycle anywhere and only the two RHI entries (Phase 13 Day 162).
- [ ] **Tests:** the whole window and input suite unchanged in behavior on Wayland and X11 (`display` group); both demos run 1000 frames with `--validation` on
      both backends; `engine_main` (or its replacement) still opens a window.
- [ ] **Documentation:** reference regenerated (the window pages change only in paths); `docgen lint` zero; `ctest -L docs`.
- [ ] **Runs (Linux):** `scripts/test-phase.sh --affected` (CMake changed: everything) plus asan and tsan; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: a file left behind or duplicated by the move, a gate allow list still pointing at an old path
      (stale entries must fail, check they did), link order now that `platform` is separate.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** windows and input live in `engine/platform/` as their own library that alone links the display libraries, no program includes a per-platform header,
and the include graph has no cycle.
