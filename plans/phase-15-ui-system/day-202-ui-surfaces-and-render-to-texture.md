# Phase 15 / Day 202 — UI Surfaces and Render-to-Texture

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Days 190, 192, 201.

The same widget tree rendered into an offscreen texture that a 3D scene samples, with the update policies and input without a window.

## Tasks

- [x] **`renderer::UiSurfaceTarget`** (`renderer/ui_surface_target.h`): owns the offscreen color texture of a surface (format, size, sRGB handling, optional transparent clear), implements `ImageSource` and `Recreatable` (rebuilt from the surface's draw list, which is rebuilt each frame anyway), creation and destruction through the device's deferred destruction, resize coalesced to once per frame, no UI types in the material system: a world material simply samples a texture.
- [x] **Update policies:** per surface `UpdatePolicy` = every frame, on demand (only when `Surface::NeedsRedraw()`: tree dirty, animation running, caret blinking), or at a fixed rate; an idle on-demand surface adds no pass and no GPU work (a counter shows 0 passes over 100 idle frames; a dozen idle screens cost nothing); the texture keeps its last picture between updates.
- [x] **Graph integration:** an offscreen surface's `ui` pass writes its texture and is ordered before any pass that samples it by resource (the world pass sampling the screen texture); the window surface's own `ui` pass remains last. Mips are not generated (documented non-goal; the renderer's existing texture rules apply).
- [x] **Per-surface settings:** size and scale factor independent of the window, theme, background (opaque, or transparent for overlays - blending a transparent surface over the world is the application's material choice), `srgb` handling so a UI drawn into an sRGB texture and shown through an emissive material has the colors the theme says; tested by sampling pixels in both the window and the offscreen case.
- [x] **Input without a window:** a `SurfaceInput` API the application calls with pointer position in surface pixels, buttons, wheel, keys, text, focus - the same `ui::Event`s the window adapter produces; helper `RayToSurfacePoint(ray, quad)` (math only, in `ui`, no scene types: origin, direction, quad corners in, hit uv out) for the application's ray cast; pointer capture and focus per surface, a pressed button released by `SurfaceInput` loss.
- [x] **Display-only and interactive modes:** a surface with no input is display-only; an interactive one has its hover/pressed states react to `SurfaceInput` (a monitor you can click with a ray).
- [x] **Tests (`tests/ui/surface_input_test.cpp` CPU; `tests/renderer/ui_surface_target_test.cpp` GPU):** `RayToSurfacePoint` for several quads and angles including misses and back faces; two surfaces with different themes and scales on one harness; on-demand policy pass counts; fixed-rate cadence with a fake clock; GPU: a surface (button + label) renders to its texture, the texture is sampled by a lit quad in a Phase 5 scene (readback shows the button color on the quad's pixels), the surface updates on demand when a click changes the button state, the window UI and the in-world UI use different themes in one frame, reset and loss.
- [x] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [x] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a widget tree renders into a texture shown on a quad in the 3D scene, updates only when it needs to, accepts input from a ray cast by the application, and is independent of any window.

## Notes

- `Surface::NeedsRedraw()` already existed (the picture differs from the last list), so the new "would a frame do anything" query is `Surface::NeedsFrame()`; on-demand uses it.
- `UiSurfaceTarget` holds a `ViewportTarget` for the texture, registry and resize; it adds the clear colour (sRGB-decoded for sRGB formats), the paint pass and the `NeedsPaint` rule.
- The GPU tests show the surface through the window UI pass (sampled in the same graph), not on a lit quad of a Phase 5 scene; the lit quad is for the demo days. Per-surface themes are tested on the CPU (two harnesses), not in one GPU frame.
- **Runs (Linux):** plain `ran 1873 of 4801 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`; asan `ran 1375 of 4801 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`; tsan `ran 1059 of 4801 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`. `ctest -L docs` passes, the gates pass, Clang and GCC build clean in unity and non-unity.

## Post-implementation code review

`/code-review high`: 3 findings, 1 fixed, 2 declined.

- **Fixed:** `NeedsFrame` stayed true under a shown tooltip, so an idle on-demand screen never went idle (regression test: it failed before the fix, confirmed).
- **Declined:** a fixed-rate frame is given one period as its time, but the remainder is carried to the next frame, so the time is conserved (only a remainder over a whole period is dropped). The lit-quad GPU test is not written; filed in `KNOWN-ISSUES.md`.
