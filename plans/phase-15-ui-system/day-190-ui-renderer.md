# Phase 15 / Day 190 — The UI Renderer: Shaders, Pipeline and Graph Pass

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 189 (the `DrawListCanvas` fills the list this renderer draws); Phase 14 Days 180-181 (`GlyphAtlasTexture`, `TextRenderer`); Phase 8 (reset).

The renderer side: `renderer::UiRenderer` turns a `ui::DrawList` into one render-graph pass, reset- and loss-safe like `TextRenderer`.

## Tasks

- [ ] **Shaders:** `shaders/ui.vert.slang` and `ui.frag.slang`, minted with `slangc` to SPIR-V and reflection (`*.spv.inc`, `*.refl.inc`, listed in `shaders/CMakeLists.txt`); the fragment shader evaluates the rounded-rect/border signed-distance function with analytic anti-aliasing (the choice from Day 184), samples an R8 glyph page (coverage) or an RGBA image, multiplies by the vertex color, and outputs premultiplied color; sRGB targets decode the vertex color in the vertex shader as the text shaders do. DXIL waits for Day 205 (as for the text shaders).
- [ ] **`renderer::UiRenderer`** (`renderer/ui_renderer.h`): `Create(device, glyph_atlas_texture, desc, resources)`, `Destroy`, `SetImage(ImageId, rhi::TextureHandle owner...)` registration of application textures **by owner** (an interface `ImageSource` with `Handle()` read at record time, never a cached handle: the reset rule of `docs/guide/renderer.md`), `AddToGraph(graph, draw_list, target, format, width, height)` that declares a pass named `ui` after the passes that wrote the target (a read-write color attachment, no clear), no pass when the list is empty, `Stats()` (commands, draws, vertices, dropped).
- [ ] **Recording:** one vertex and one index buffer region per frame in flight (CpuToGpu, like `TextRenderer`), capacity from `UiRendererDesc` (vertices, indices) with a counted drop on overflow; per command: scissor from the clip rectangle (clamped to the target), bind group for the page/image (transient bind groups, as `TextRenderer`), `DrawIndexed`; blending premultiplied; no depth.
- [ ] **Shares with `TextRenderer` where it can:** the glyph atlas texture and the quad placement helper; `TextRenderer` stays for plain overlays (the Phase 14 demo) and is not removed; document which one a program picks.
- [ ] **Reset and loss:** implements `Recreatable` at the materials tier, keeps the CPU draw list reusable (the list is rebuilt every frame anyway), registers with `DeviceResources`; tests through `tests/renderer/reset_rig.h` compare `Device::Resources()` counts after a reset and recover from an injected loss; an application image whose owner is gone draws nothing and logs once.
- [ ] **Format and size changes:** a different target format rebuilds the pipeline (retry on failure: the Day 183 review finding), a resize needs nothing but the new size in `AddToGraph`.
- [ ] **Tests (`tests/renderer/ui_renderer_test.cpp`, GPU):** a filled rect, a rounded rect with a border and a gradient-free image drawn onto a cleared target and read back (exact interior pixel, anti-aliased edge between the two colors, outside untouched); a clip rectangle hides everything outside it; layers draw in order; text through the atlas matches `TextRenderer`'s pixels for the same layout; sRGB and UNORM targets; empty list = no pass; overflow drops and counts; reset and loss.
- [ ] **Golden:** a small fixed scene (panel, button, text, border, shadow) to `tests/rhi/golden/ui_scene.ppm`, compared with the Phase 5 tolerance machinery, checked by eye on first bless.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `ui::DrawList` appears on any color target through one graph pass on Vulkan, matching the golden, surviving a device reset and loss, with the Direct3D 12 half open until Day 205.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
