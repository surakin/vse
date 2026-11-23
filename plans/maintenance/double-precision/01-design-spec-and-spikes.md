# Maintenance: double precision, Day 1 — Design spec and spikes

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] Write `docs/superpowers/specs/<date>-double-precision-design.md`: the survey of the next task as a table (every module that holds a world position, the field, its consumers), the rule for what is `f64` and what stays `f32`, the type design, the migration order and the ABI plan.
- [ ] **Survey (measured, not guessed):** grep every `Vec3`, `Mat4` and `Quaternion` use per module (today: physics 46 files, renderer 23, spatial 12, script 12, asset_import 10, asset 8, engine_c 7, foundation 6, audio 6, storage 4, scene 3, net 2, ecs 2, plus 129 files in tests, game and tools) and classify each as a world position (becomes `f64`), a local or relative quantity (stays `f32`), a direction, normal, colour, UV or vertex attribute (stays `f32`), or a GPU-side value (stays `f32`).
- [ ] **Rule to test in the spike:** a world-space position and a world translation are `f64`; rotations, scales, directions, velocities, extents and everything in a body's or mesh's own space stay `f32`; a value that is the difference of two world positions is computed in `f64` and then narrowed to `f32` (the camera-relative idea).
- [ ] **Spike 1, the type:** `Vec3d` (and `Mat4d` or a `Transform64` of an `f64` translation with an `f32` rotation matrix and scale) against making `Vec3` a template on the scalar; compile-time cost, API noise in the 129 outside files, how little each needs to change; pick.
- [ ] **Spike 2, physics cost:** the world-space positions of bodies in `f64` with the narrow phase working in a body-relative `f32` frame (Unreal Chaos's approach) against `f64` throughout: step time of the Day 412 benchmark scenes, memory per body, and the determinism rule (`+ - * / Sqrt` only, no FMA, the golden hashes stay equal across thread counts); pick.
- [ ] **Spike 3, camera-relative rendering:** the renderer subtracts the camera's `f64` position from every object's `f64` position and writes `f32` matrices; measure the CPU cost for 100,000 objects and for the instance sets; decide where the subtraction happens (the scene sync, `RenderScene`, or the frame).
- [ ] **Spike 4, formats and ABI:** which saved forms hold positions (levels, storage snapshots, physics snapshot, minted meshes' node transforms, C API structs, script `Value`); for each, the version bump or the new sized struct, and the rule that blobs have only the current version (re-minted offline, no load-time conversion: the asset rules of 2026-11-13).
- [ ] Scripts: confirm that Lua numbers are already `f64` (`script::Value` holds `f64` scalars, `AsFloat` returns `f64`) and list the bindings that narrow to `f32` today (`script::Value` has `Vec2/3/4` of `f32`, the physics, entity and audio bindings).
- [ ] Decisions into the spec, `PLAN.md` and the later day files of this series (rewrite them if a spike changed their scope); `STATUS.md` and `KNOWN-ISSUES.md`.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** The spec is written and committed with the survey table and the four spike results; the later days are adjusted to it.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
