# Maintenance: double precision, Day 4 — A camera-relative renderer

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] `renderer::Camera` keeps an `f64` position; the view matrix is built camera-relative (the eye at the origin) and `FrameData` carries `f32` only.
- [x] `RenderScene` and `SceneFrame` take a per-frame **render origin** (`f64 Vec3`, the camera position unless a host sets one): every object's world matrix is written to the GPU as `f32` relative to it; the cull, the sort and the BVH work on camera-relative boxes (a decision of the spec: where the subtraction happens).
- [x] Lights, the sky's sun, shadows (if any), the terrain tiles and `TerrainTiles::Select` (the camera position in map space), the debug drawer and the text and UI layers (screen space: no change) follow the origin.
- [x] The instance sets and the hierarchy (phase 28's `f64` chunk origins and roots) take the render origin and drop their zero default.
- [x] Tests: a scene 100,000 km from the origin draws the same picture (pixel compare within tolerance) as the same scene near the origin; objects 1 mm apart at that distance stay distinct; the golden-scene hashes of the renderer tests are unchanged for scenes near the origin (the rule: no change near the origin).
- [x] Docs: `docs/guide/renderer.md` gains "Large worlds".
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A scene 100,000 km away renders like the same scene at the origin.

## Notes

- Written 2026-11-23.
- Day 1 picks: the scene is stored in `f64` world space and culled with an `f64` frustum (no subtraction in the cull: measured the same cost as `f32`); the subtraction happens where the GPU data is written (`WriteObjects`, the instance compaction, the terrain uniforms): 0.43 ms for 100,000 objects.

## Post-implementation code review

`/code-review high`: 3 findings, all recorded, none fixed.

1. `MakeCameraSystem` now reads `SceneLink` (a new requirement; every in-tree world has it).
2. Direct users of `RenderScene` (hand-built scenes, a debug drawer) are not rebased; with a nonzero origin they use render-origin coordinates. Documented; no demo enables the origin yet.
3. Design deviation from this day file, recorded in the spec's "Amendment (Day 4)": a floating render origin instead of an `f64` scene and `f64` `Camera`; the instance sets (paused Phase 28) take the origin when they are built; the terrain tile selection and the sky need nothing (they work in the relative frame).

Runs: see `STATUS.md`. Guide: `docs/guide/renderer.md` ("Large worlds"); `ctest -L docs` passes.
