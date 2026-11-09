# Phase 15 / Day 203 — Guide, Benchmark, Closing Runs, Phase Review

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 184-202.

Document, measure, run everything on both machines, review the whole phase, close it.

## Tasks

- [ ] **Guide `docs/guide/ui.md` complete:** the model (surface, tree, event flow, frame cycle, coordinates and scale), a complete "hello UI" sample (tagged, compiles and runs on an offscreen surface with the harness), each widget family with a sample, theming (data, variants, overrides, swap), layout, input routing and focus, popups and modality, the viewport widget and mouse look, UI surfaces and the ray-cast input recipe, the settings screen, the debug overlay, reset and loss, the limits (no docking, no IME, no OS clipboard, no accessibility tree, no file dialogs); `docs/guide/README.md`, `renderer.md`, `building-and-testing.md` and `platform` guides updated where they mention input or the new tests.
- [ ] **Reference:** `docgen lint` zero; reference regenerated and committed (identical on both machines); `docs/reference/modules.txt` covers `ui`, `ui_window` and the new renderer and platform headers.
- [ ] **Benchmark** `game/renderer_bench` section `ui`: draw-list build for 1,000 / 5,000 widgets, layout time for the same trees, UI renderer record cost, list box with 100,000 items per frame, steady-state allocation count; recorded in the guide and this file (no thresholds in tests).
- [ ] **Project documents:** `PLAN.md` (the roadmap row, a progress-log entry, the assumption list if the platform libraries or the pointer-lock choices need one), `CLAUDE.md` if a rule changed (the `ui` module line, the platform event stream, theme rule "no literal colors in widgets" if it became a gate), `BUILD.md` (any new library), `ARCHITECTURE.md` (the measured graph), `STATUS.md`.
- [ ] **Known-issues sweep:** every Phase 15 deferral filed (no docking, no IME, process-local clipboard only, no drag-and-drop, no accessibility, no file dialogs, no scroll inertia, tree view deferred to Phase 27 if the editor needs it, DX12 and Win32 items as they stand after Day 202); entries this phase fixed removed.
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, `ui_demo` and the other demos 1000 frames with validation on Wayland and X11; Windows `scripts/test-phase.sh --all --flavor all` (plain and asan), `ctest -L docs`, the demos 1000 frames on DX12 and Vulkan; record both final `ran X of Y tests (phases ...)` lines here; nothing left out.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 184 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] `plans/phase-15-ui-system.md` (status complete), `PLAN.md`, `STATUS.md` updated; `CURRENT_PHASE` stays 15 until Phase 16 has its day files and starts; commit and push.

**Done:** the guide and reference are current and identical on both machines, the closing full-run lines for Linux and Windows are recorded, the uncapped review is recorded, and Phase 15 is marked complete.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
