# Phase 27 / Day 347 — 3D Text Meshes Through the Renderer

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Days 345-346; Phase 5 `MeshData`/`GpuMesh`, materials and the Phase 15 viewport; Phase 17 (ECS and the scene bridge).

A string becomes one `MeshData` that draws like any other mesh.

## Tasks

- [ ] **`text::SolidGlyphCache`** (key: font id, glyph id, extrude/bevel options hash, tolerance) with the same byte budget and counters as the 2D cache; `renderer::BuildTextMesh(const TextLayout&, Font&, const TextMeshOptions&) -> MeshData`: merges the cached glyph solids at their pen positions (layout units mapped to world units by `world_per_em`), line breaks and alignment from `TextLayout` unchanged, a string with no outlines (spaces) gives an empty mesh and says so; vertices `StandardVertex` (or `ColorVertex` for Day 349).
- [ ] **Indices and strips:** `uint16` when the string fits, `uint32` otherwise; where the Day 342 benchmark showed strips smaller, the face triangles go through `ListToStrip` with restart (Phase 5 Day 78) -- decided by the measured count and recorded here, not assumed.
- [ ] **Drawing:** the mesh draws through the existing material system (`MeshData` + `GpuMesh` + material; PBR default); since Phase 17 the scene is ECS entities: a `Text3D` component (text, font, options, material) and a system in the scene bridge module (`engine/scene`, Phase 17 Day 233) build and cache the `GpuMesh` per (string, layout, options) with an LRU budget, rebuild it when the component changes (`Changed<Text3D>`), and attach the `MeshRenderer`; `AddText3D(world, layout, options, material, transform)` is the convenience that creates the entity; the `GpuMesh` is registered for reset with `Source::load` pointing at the builder so a reset does not keep CPU copies twice.
- [ ] **Culling and bounds:** `MeshData` bounds from the vertices (Phase 5 rule); the frustum culling and sorting paths work on text meshes (a test with text outside the frustum).
- [ ] **Tests:** CPU (`tests/renderer/text_mesh_build_test.cpp`: merged vertex count equals the sum of the glyph solids, positions equal the layout's pen positions, bounds contain every vertex, empty string, a 10,000-glyph string builds under a documented time); GPU (`tests/renderer/text3d_test.cpp`, per backend): 'Hi' lit by a directional light rendered into the G-buffer path and readback checks of coverage, a front-facing normal pixel and a depth value; the mesh is culled outside the frustum; reset through `reset_rig.h` keeps counts; a golden PGM of 'Rex' extruded, beveled, lit, at a fixed camera.
- [ ] **Setting/limits:** `renderer.text3d.cache_bytes` declared as a setting; the maximum glyphs per mesh is documented with the failure (a counted refusal, not a crash).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 354; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "3D text" (rendering, cost, limits); `docs/guide/renderer.md` cross-reference.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a laid-out string draws as an extruded (optionally beveled) lit mesh through the normal renderer, cached, culled and reset-safe, with a golden image per backend.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
