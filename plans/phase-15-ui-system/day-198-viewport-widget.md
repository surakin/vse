# Phase 15 / Day 198 — The Viewport Widget

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 188, 190, 192; Phase 5 (offscreen `FrameTarget`, `Renderer::RenderFrame`); Day 186 (pointer lock).

A widget that hosts a rendered 3D world, with the input a camera needs.

## Tasks

- [ ] **`ui::Viewport` widget (CPU, no RHI types):** a laid-out rectangle in physical pixels, a `ViewportBinding` interface the application implements - `OnResize(width, height)` called **at most once per frame** with the final size after layout (coalesced; never during a drag of the window's edge per configure), `Render(frame info)` to produce the picture, `Image()` returning the `ImageId` the UI draws as a quad in the widget's rectangle (clipped like any widget), `OnInput(ViewportInput&)`.
- [ ] **`renderer` glue (`renderer/viewport_target.h`):** `ViewportTarget` owns the offscreen color + depth textures for a viewport, resizes (create-before-destroy through the device's deferred destruction) when told, registers with `DeviceResources` (reset-safe), exposes itself as an `ImageSource` (Day 188) so `UiRenderer` draws it; the Phase 5 renderer draws the world into it with `Renderer::RenderFrame(..., &offscreen)` as a pass scheduled before the UI pass (graph order by resources).
- [ ] **Camera aspect:** the binding gets the size, the application sets the camera's aspect; a helper `AspectOf(width, height)`; zero-size viewports (collapsed panes) render nothing and keep their target.
- [ ] **Input to the viewport:** pointer position in the viewport's own pixel space (not logical units: documented, with the scale factor given), enter/leave, focus on click, capture while dragging, wheel; **mouse look**: `Viewport::BeginPointerLock()` asks the surface for a lock (Day 186) when a drag starts on a chosen button and ends it on release or focus loss; relative motion events reach `OnInput` as deltas; without lock support the viewport falls back to captured absolute motion with a warning logged once.
- [ ] **Multiple viewports:** two viewports in one surface each with their own target and camera, focus and capture independent; one hidden (tab not selected) does not render and frees nothing; rendering order is tree order.
- [ ] **Overlay content in the viewport:** widgets that are children of the viewport (a HUD label, a gizmo handle widget) draw after the image with the viewport's clip rect through the same draw list; hit testing gives them the pointer before the viewport's own input.
- [ ] **Resize coalescing test against the Phase 1 lesson:** a script resizes the window ten times between two frames; exactly one target resize happens (counted by the binding); no target is created for an intermediate size; the old target is destroyed after the GPU is done with it (deferred destruction counter).
- [ ] **Tests (`tests/ui/viewport_test.cpp` CPU with a fake binding; `tests/renderer/viewport_target_test.cpp` GPU):** the binding calls and their counts; input coordinates; capture and lock state machine with a fake window; fallback without lock; hidden viewports; overlay child draws above the image; GPU: the Phase 5 lit cube renders into a viewport target inside a UI draw (readback shows cube pixels inside the rectangle and the panel color outside), resize to a new size keeps the picture correct, reset and loss rebuild the target and the picture returns; two viewports draw different cameras.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 202 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 202); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a resizable panel shows the lit cube from its own camera, input and mouse look reach it correctly, resizes are coalesced to one target resize per frame, and it survives a device reset.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
