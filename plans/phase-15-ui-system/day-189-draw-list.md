# Phase 15 / Day 189 — The 2D Draw List (CPU)

**Phase:** [Phase 15 — UI System](../phase-15-ui-system.md)
**Depends on:** Day 184; Phase 14 (`text::Layout`, `GlyphAtlas`).

Everything a surface draws becomes one `ui::DrawList`: vertices, indices and commands. Nothing here touches the GPU, so it is tested by reading the data.

## Tasks

- [ ] **`ui/draw_list.h`:** `Vertex` (position in physical pixels, uv, packed RGBA color, and the parameters the rounded-rect shader needs - rect half size, corner radius, border width; the layout from Spike B: 40 bytes, a `flags` word whose low bits select the draw **mode** - rectangle SDF, glyph coverage, image color - per vertex, so rectangles and glyphs of a frame share one bound texture and merge into few commands, Day 184 spec section 6), `DrawCommand` (index offset and count, clip rectangle, `ImageId` or the glyph atlas page, a blend mode), `DrawList` owning `Vector<Vertex>`, `Vector<u32>` and `Vector<DrawCommand>`; `Clear()` keeps capacity so steady state allocates nothing.
- [ ] **Primitives:** `AddRect(rect, color)`, `AddRoundedRect(rect, radius, color)` (per-corner radii), `AddBorder(rect, radius, width, color)`, `AddLine(a, b, width, color)`, `AddTriangle` (arrows in combo boxes and menus), `AddImage(rect, ImageId, uv rect, tint)`, `AddText(layout, style, origin, color)` (glyph quads from `text::PositionedGlyph` and the `GlyphAtlas`, whole-pixel placement as `TextRenderer` does), `AddShadow(rect, radius, blur, color)` for popups (a larger SDF quad).
- [ ] **Clip stack:** `PushClip(rect)` / `PopClip()` intersect with the parent; a command carries its clip rectangle; commands merge when their state is equal and a draw is added to the same texture, so a typical window is a handful of commands; an empty intersection drops the draw before any vertex is made.
- [ ] **Layers:** `BeginLayer(order)` so popups, tooltips and drag visuals draw above everything regardless of the order widgets were visited; layers sort by order, stable within an order, and the sorted result is what the renderer sees.
- [ ] **Text through the atlas:** a glyph the atlas cannot hold counts as dropped (same counters as `TextRenderer`); text above `kMaxAtlasPixelSize` scales as on Day 181; atlas pages are named in the command so the renderer binds the right texture.
- [ ] **Determinism and cost:** same input, same bytes (a golden of the vertex/index/command data for a fixed scene, in `tests/ui/golden/`); steady-state allocation test (the render-graph allocation counter, 100 frames of the same scene allocate nothing); budget numbers from Spike B reproduced here with a bench section in `game/renderer_bench` added on Day 206.
- [ ] **Tests (`tests/ui/draw_list_test.cpp`):** each primitive's vertex count and bounds; clip intersection and the dropped empty clip; command merging; layer ordering; text quad positions equal `TextRenderer`'s for the same layout (the same function of layout and entry, tested against the text module directly); image and tint; huge counts (100k rects) stay within 32-bit indices and report overflow as a counter, never a wrap.
- [ ] **Tests are written with the code:** every public behavior above has a test; GPU tests carry the `gpu` tag, use `FOR_EACH_GPU_BACKEND` and skip a backend that has no shaders (Vulkan only until Day 205 mints DXIL); every new test file has its line in `tests/phases.txt` (phase 15).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux; then the asan and tsan flavors when the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the phase closes (Day 205); a day done on one machine files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/ui.md` gains the day's section (tagged samples compile and run); new modules are in `docs/reference/modules.txt`.
- [ ] **Module rules:** `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` updated in the same commit when a module, an edge or a prerequisite changed; `module_deps_check`, `platform_neutral_check`, `std_container_check`, `file_access_check` and `config_access_check` pass.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a scene of rectangles, borders, images and text builds into a deterministic, allocation-free-in-steady-state draw list that the tests read back, with clips and layers correct.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
