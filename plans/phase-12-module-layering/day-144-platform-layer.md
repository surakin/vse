# Phase 12 / Day 144 — The Platform Layer: Windows and Input Leave the Foundation

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Day 143; the platform move's file list and namespace decision (Day 140, Spike C).

Move the windows and input code to `engine/platform/`, where `PLAN.md` always put it, so it stops being part of every foundation consumer
(`ARCHITECTURE.md` section 2, items 5, 9 and 10).

## Tasks

- [x] **Move with `git mv`** (history kept): `os/window.h`, `os/window.cpp`, `os/input.h`, `os/key_translate.cpp` to `engine/platform/`; `os/linux/wayland/`,
      `os/linux/x11/`, `os/linux/xkb_keyboard.*`, `os/linux/platform_window_host.*` to `engine/platform/linux/`; `os/windows/FULLSCREEN.md` and
      `os/macos/FULLSCREEN.md` to `engine/platform/windows/` and `macos/`; the vendored Wayland protocol code and its `VENDORED.md` with the Wayland backend.
      `SESSION.md`, `session.h` and `gpu_driver_probe.h` stay with the OS services.
- [x] **Neutral window host:** `PlatformWindowHost` gets a neutral header (`platform/window_host.h`) with the per-platform implementation behind it, so the demos
      and `engine_main` (or its replacement) include only neutral headers; `game/rhi_demo/main.cpp` and `game/renderer_demo/main.cpp` no longer include
      `os/linux/`.
- [x] **Namespace and includes** per the spec (decision 4): every include line, using-declaration and documentation reference updated; no forwarding headers left
      at the old paths unless the spec allows them for one phase (then listed in the allow list with their removal day).
- [x] **CMake:** `engine/platform/CMakeLists.txt` builds `platform` (and `platform_asan`/`platform_tsan` where the flavors need them) with the Linux display
      libraries linked here only; the foundation no longer links Wayland, X11 or xkbcommon; until Day 145 splits the foundation, `platform` links `foundation`.
      `tools/docgen` and `tools/shader_mint` no longer pull in display libraries (check with `ldd`).
- [x] **Everything that names the old paths:** `tests/phases.txt` and `tests/module_map.txt` patterns (the window tests keep phase 1), the gates' allow lists
      (`platform_neutral_check`, `file_access_check`, `config_access_check`, `signal_safety_check`, `std_container_check`), `docs/reference/modules.txt` (the
      `platform/window-input` module), `docs/guide/platform-windowing-and-input.md`, the Phase 13 and 22 plan files' paths, `ARCHITECTURE.md` section 1.
- [x] **Allow list:** entries for items 5, 9 and 10 deleted; `module_deps_check` reports no cycle anywhere and only the two RHI entries (Phase 13 Day 162).
- [x] **Tests:** the whole window and input suite unchanged in behavior on Wayland and X11 (`display` group); both demos run 1000 frames with `--validation` on
      both backends; `engine_main` (or its replacement) still opens a window.
- [x] **Documentation:** reference regenerated (the window pages change only in paths); `docgen lint` zero; `ctest -L docs`.
- [x] **Runs (Linux):** `scripts/test-phase.sh --affected` (CMake changed: everything) plus asan and tsan; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: a file left behind or duplicated by the move, a gate allow list still pointing at an old path
      (stale entries must fail, check they did), link order now that `platform` is separate.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** windows and input live in `engine/platform/` as their own library that alone links the display libraries, no program includes a per-platform header,
and the include graph has no cycle.

## Result (2026-11-07)

- Moved with `git mv` to `engine/platform/`: `window.h/.cpp`, `input.h`, `key_translate.cpp`; `linux/wayland/` (with the vendored protocol code and `VENDORED.md`), `linux/x11/`,
  `linux/xkb_keyboard.*`; `FULLSCREEN.md` to `windows/` and `macos/`. `SESSION.md`, `session.h` and `gpu_driver_probe.h` stay with the OS services. Namespace stays `os::`; include
  prefix `platform/`; no forwarding headers.
- Neutral host: `platform/window_host.h` declares `os::PlatformWindowHost` (a `UniquePtr<Impl>` to the backends, created in `Open`, so the class no longer holds the Wayland and X11
  windows by value); the implementation is `platform/linux/window_host_linux.cpp`. The demos and `hello_window` include only the neutral header.
- CMake: `engine/platform/CMakeLists.txt` builds `platform`, `platform_asan`, `platform_tsan` and finds Wayland, xkbcommon and X11; `foundation*` link none of them. `rhi*` links
  `platform*` (the RHI's headers and Vulkan surfaces use windows); `hello_window` and `foundation_tests` link `platform`; the docs sample link line adds it.
- `ldd`: `docgen` has no display library any more. **`shader_mint` still does**, because it links `renderer`, which links `rhi_vulkan` (`ARCHITECTURE.md` item 8, removed with the backend
  registry on Phase 13 Day 162). Noted in `KNOWN-ISSUES.md`.
- Everything naming the old paths: tests (nine files; `window_backend_test.cpp` now includes the Wayland and X11 headers itself), gate allow lists (`file_access_check`, `config_access_check`),
  `std_container_check` allow list, `tests/module_map.txt` (new `engine/platform` line, `game/hello_window`), `docs/reference/modules.txt` (the `platform/window-input` module), the window guide
  page (new section "Where the code lives, and what to link"), `BUILD.md`, `ARCHITECTURE.md` section 1, the Phase 13 plan files for Days 158 and 159. Historical day files of earlier phases keep the
  paths they were written with. `tests/phases.txt` needed no change (tests are named by file; the window tests keep phase 1).
- Allow list: the three `platform` entries (demos, `hello_window`) and the three display-library entries deleted; what remains is the two RHI entries for Phase 13 Day 162, and the gate
  reports no cycle.
- Runs: plain `--affected --crashes` = `ran 1527 of 3488 tests (phases: all; flavors: plain; display: included; crash tests: included)`, ASan `ran 965 of 3488`, TSan `ran 682 of 3488`;
  only the two known Wayland/NVIDIA failures. `rhi_demo` and `renderer_demo` ran 1000 frames with `--validation` on X11 and `rhi_demo` on Wayland, exit 0, no validation message;
  `renderer_demo --backend wayland` segfaults on its first frame (the known NVIDIA issue), so that combination was not verified. `hello_window` runs. Docs: reference regenerated, `ctest -L docs` passes.

## Post-implementation code review

Inline high-effort review. Checked for a file left behind or duplicated by the move (`git status` shows only renames; nothing remains under `os/linux/{wayland,x11}` or `os/window*`), for a gate
allow list pointing at an old path (stale entries fail the gates, and all gates pass), and for link order now that `platform` is separate (it links `foundation` PUBLIC; static libraries resolve in
both build modes of the targets built). Behavior change noted: `PlatformWindowHost` now allocates its backends when opened. Declined: none.
