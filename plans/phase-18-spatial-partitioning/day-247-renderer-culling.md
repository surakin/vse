# Phase 18 / Day 247 — Renderer Integration: Frustum and Light Culling

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 233, 241, 245; Phase 5 `RenderScene::Cull`, `GBufferPass::Prepare`, `FrameResources::WriteLights`.

Replace the renderer's flat per-object scan with the render set's tree, behind the existing interfaces.

## Tasks

- [ ] **Visibility system in `engine/scene`** (stage `kRender`, after `SpatialSync`): `CullFrustum(set, frustum, mask, out visible entities)` over the render set's trees (parallel over the top-level subtrees, merged in a deterministic order), writing a `Visible` list the `RenderSync` of Day 233 uses to keep `RenderScene` entries in step; `RenderScene::Cull` gains an overload that takes the precomputed visible slots so `GBufferPass::Prepare` does not re-scan (the old flat path stays as the reference and the fallback for scenes without an ECS).
- [ ] **Lights:** point and spot lights are in the render set with their range as bounds; `LightSummary` is built from the lights whose bounds overlap the frustum (and, as a stretch for the day, per-tile light lists from the tree for the lighting pass if the pass's shader interface allows it without a layout change; otherwise recorded as a Phase 31 item).
- [ ] **Settings:** `renderer.culling = bvh|flat` (default `bvh`) so the two paths can be compared at run time without a rebuild.
- [ ] **Tests** (`tests/scene/visibility_test.cpp`, CPU, plus the golden tests of Phase 5 on the null device where applicable): the BVH-visible set equals the flat `Cull` result (as a set) on 1,000 random scenes and cameras; a moved object is culled and unculled the same frame as the flat path; a hidden object is never visible; the rendered pixels with `bvh` equal the pixels with `flat` on the Phase 5 golden scenes (same tolerance); the draw count equals the flat path; culling a 100k-object scene with a camera that sees 1 percent costs at least the factor the spec states less than the flat scan (recorded in the day file, compared with the pass condition of Day 237).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, the parallel culling); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/spatial.md`: "Culling for the renderer"; `docs/guide/renderer.md` gets a pointer.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the renderer culls through the spatial tree with identical output to the flat scan and a measured gain on large scenes, with the flat path kept as the reference.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
