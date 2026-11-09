# Phase 15 / Day 201 — The `ui_demo` Application, Themes, Reset and Goldens

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 188-200.

The phase's definition of done as one program, with scripted tests and golden images.

## Tasks

- [ ] **`game/ui_demo`:** a resizable window whose entire contents are widgets: a menu bar (File/Edit/View/Help with submenus, accelerators and a context menu), a toolbar of buttons/toggles, a left panel with labels, checkbox, radio group, combobox, slider, spin box, text field and a scrollable list box of 1,000 items, a central **viewport** with the Phase 5 lit textured cube and orbit camera (drag to rotate with pointer lock, wheel to zoom), a bottom status label, a splitter between the panels, a modal About dialog, tooltips; Tab navigation works throughout.
- [ ] **Themes:** View > Theme switches between light and dark at run time (the whole tree restyles, no rebuild); a per-subtree override demonstrated (the status bar always dark).
- [ ] **In-world screen:** a second small `UiSurface` (a counter button and a label) rendered to a texture shown on a quad in the viewport's 3D scene next to the cube, clickable through a ray cast from the viewport's pointer (the application's job, using `RayToSurfacePoint`), with its own theme.
- [ ] **Options and scripting:** `--backend`, `--frames N`, `--screenshot out.ppm`, `--script WORDS` (`click:name`, `type:text`, `key:Name`, `resize:WxH`, `theme:dark|light`, `menu:File/About`, `drag:name:dx,dy`, `frame:N`, `quit`) so the whole demo is driven without a person; `--scale 1|1.25|2`; `--gpu`, `--rhi`, `--validation` like the other demos (shared option code); launch-only options listed in `config_access_check`'s allow list entry for the demo; the rendering settings come from the settings system and `--help` is a golden (`help.golden`).
- [ ] **Golden images:** the window at 1x and 2x (light theme), the dark theme, a menu open, the modal dialog, the in-world screen, each from a scripted run to `tests/rhi/golden/ui_demo_*.ppm` (Vulkan on Linux), blessed by eye the first time and listed with their GPU as the Phase 5 golden is.
- [ ] **Reset and loss with the UI on screen:** `ApplySettings` changes of every cost class (present mode, scale, shader quality, a device reset, an injected loss) leave the picture identical to the golden afterwards (compare after each), as the Day 182 reset test does for the text overlay; the viewport target and the in-world surface texture come back.
- [ ] **Display-backend tests:** scripted runs under Wayland and X11 (`ui_demo_script_wayland|x11`): click, type, menu, resize, theme switch, drag with pointer lock where supported; exit code 0, no validation messages with `--validation`; frame rate sanity (average frame time logged, not asserted).
- [ ] **By-eye checks recorded in this file:** the person (or the session) looks at both themes at 1x and 2x, a text field with accents, a menu near a window edge, the viewport resizing while dragging the window edge, and the cursor shapes on hover; screenshots named in the notes.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** `ui_demo` meets the phase's definition of done on Vulkan under Wayland and X11, scripted and golden-tested, with theme switching, the in-world screen and reset/loss recovery.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
