# Phase 18 / Day 243 — Extruding Glyph Geometry

**Phase:** [Phase 18 — 3D Text, Large 2D Text & Colored Glyphs](../phase-18-3d-text-and-color-glyphs.md)
**Depends on:** Day 240.

Turn the triangulated glyph into a solid: front face, back face and side walls, with normals, tangents and UVs. CPU only.

## Tasks

- [ ] **`ExtrudeGlyph(const CleanShape&, const TriangleMesh2D&, const ExtrudeOptions&) -> SolidMesh`:** `ExtrudeOptions { f32 depth_em; NormalMode normals (Flat | Smooth); f32 smooth_angle_deg; bool front; bool back; }`; the front face at z = +depth/2 with normal +z, the back at z = -depth/2 with normal -z and reversed winding, one side-wall quad (two triangles) per boundary edge of every loop (outer and hole loops; hole walls face into the hole), counter-clockwise seen from outside.
- [ ] **Normals:** flat = per-face normals with vertices duplicated at every edge; smooth = averaged across wall edges whose angle is below `smooth_angle_deg`, corners above it stay hard (the wall of a square stays flat, the wall of a circle-like `o` is smooth); face normals exact +/-z.
- [ ] **Tangents and UVs:** wall UVs (u along the loop length normalized, v across the depth), face UVs from the glyph's bounds so a texture maps across the string later; tangent with handedness w as `StandardVertex` requires.
- [ ] **Output** is neutral CPU data (`SolidMesh { positions, normals, tangents, uvs, indices }`), converted to the renderer's `MeshData` on Day 245.
- [ ] **Tests** (`tests/text/extrude_test.cpp`): watertight (every edge shared by exactly two triangles, with opposite directions, checked on index buffers after welding positions), no degenerate triangles, Euler characteristic of a letter without holes (2) and with one hole ('O', 0 genus handles: V - E + F = 0 for the solid with one through-hole), volume equals area times depth (divergence theorem on the triangles, relative error 1e-5), outward normals (signed volume positive), flat vs smooth normal counts, a depth of zero is refused or gives a flat double-sided face (documented), `front=false` leaves an open mesh and says so; every glyph of the shipped font passes the watertight, volume and outward-normal checks.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (pure CPU); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 252; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` new section "3D text" (geometry).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** every glyph of the shipped font extrudes to a closed, outward-facing solid whose volume is area times depth, with correct normals, tangents and UVs.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
