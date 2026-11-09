# Phase 15 / Day 200 — Settings Binding and the Debug Overlay

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 193-197; Phase 11 (`settings::Registry`, `Settings`); Phase 14 (`TextRenderer`).

Two consumers of the toolkit that prove it on real content: a settings screen generated from the registry, and a debug overlay.

## Tasks

- [ ] **Settings binding (`ui/settings_view.h`):** `BuildSettingsPanel(Settings&, Registry&, filter)` creates the widgets from registry metadata: group headings (`group`), a labelled row per setting (`label`, `description` as tooltip), the widget by type (bool -> `CheckBox`, int/float with a range -> `Slider` + `SpinBox`, enum -> `ComboBox` over the enum names, string -> `TextField`), with ranges and `RangePolicy` from the metadata; `kHidden` is skipped, `kAdvanced` collapsed under an Advanced section, `kSecret` shown as a password field, `kRestartRequired` flagged with an icon/text and collected for a restart prompt.
- [ ] **Writes and rollback:** a change goes through `Settings::Apply`; a rejected change rolls the widget back and shows the reason as an inline message; a value pinned by the environment or command line (`Settings::Provenance`) is shown disabled with the source as a tooltip; the widgets refresh from `Settings` change listeners and `OnChange`, never by polling (a test counts reads over 100 idle frames: zero).
- [ ] **Live example:** the `renderer_demo` rendering settings (present mode, render scale, shader quality) change from the panel at run time through `Renderer::ApplySettings`, with the cost class (dynamic, pipeline rebuild, swapchain, device reset) displayed next to the control from the Phase 8 classification.
- [ ] **Debug overlay (`ui/debug_overlay.h`):** a small tree of widgets (frame time graph from a ring of samples, FPS, job stats from the scheduler's counters, draw/pass counts from the renderer's `Stats`, GPU memory) toggled by a key, drawn in the top layer; a **log console** widget (scrolling `ListBox` over a ring of recent log lines from a log sink, level filter, a command line `TextField` that calls a registered command table) - the immediate-mode-style milestone of the phase file, built on the same retained widgets (the phase file's rule: fold, do not duplicate).
- [ ] **Log sink:** `ui::LogCapture` registers a sink with the log core, copies lines into a bounded ring without allocating on the logging thread beyond the ring (documented), and the console reads it on the main thread; no UI call from other threads (UI is main-thread only).
- [ ] **Tests (`tests/ui/settings_view_test.cpp`, `debug_overlay_test.cpp`):** a test registry with every type and flag - widgets by type, hidden skipped, advanced collapsed, secret masked, restart flagged; applying a change calls `Apply` once; a rejected value rolls back and shows the message; provenance disables; an external change refreshes the widget without polling; log console receives lines from another thread (thread-safe ring, tsan clean), filter, command execution; the frame-time graph ring.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a settings screen is generated from registry metadata and changes rendering settings live with rollback and provenance, and a debug overlay with a log console runs on the same widgets.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
