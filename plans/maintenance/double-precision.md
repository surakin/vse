# Maintenance: double precision for world positions

This is not a numbered phase: it is a maintenance series of eleven days, chosen by the user on 2026-11-23 to run **after Phase 28** (it does not change
`CURRENT_PHASE` or any phase or day number; it starts on its own first day when the user says so).

**Origin:** the engine is single precision everywhere (`Vec3`, `Mat4`, `Quaternion`, the ECS transforms, the camera, physics, spatial, audio, the C API's
`engine_transform`), so a position loses precision far from the origin: 1 mm at 10 km, 7.8 mm at 100 km, 6 cm at 1,000 km. The user (2026-11-23) wants what Unreal 5
calls Large World Coordinates: **double precision for everything that needs it**. Unreal keeps doubles on the CPU and renders camera-relative in single precision on the
GPU; this series does the same. Lua numbers are already `f64` (`script::Value` holds `f64` scalars), so scripts lose precision only where the bindings narrow to `f32`.
Phase 28's instancing (Days 494-501) is built ready for it (`f64` chunk origins and tree roots, a render origin); terrain and the sky are not.

**Depends on:** Phase 28 complete (terrain, instances, the sky), Phases 17 (ECS), 18 (spatial), 22 (assets), 23-24 (the C API), 25 (physics), 26 (audio), 27 (scripting).

**Decisions (the user, 2026-11-23):** double precision for everything that needs it; the whole engine, after Phase 28. Not yet decided (Day 1's spikes decide): the vector
type (a separate `Vec3d` or a templated `Vec3`), whether physics works `f64` throughout or body-relative `f32` around an `f64` anchor, and where the camera-relative
subtraction happens.

**Goal:** a world of at least the size Unreal supports (about 88 million km across, the `f64` limit at centimetre use), with millimetre precision at any distance a game
reaches, no change in what a scene at the origin looks like or how it simulates (except the golden hashes, re-baselined once, deliberately), and a C API a host can use with
doubles.

**Definition of done:**
- Every day's boxes are checked.
- A scene, a simulation, a saved world and a C host 100,000 km from the origin behave like the same ones at the origin (positions equal after subtracting the offset, pictures equal within tolerance).
- Physics and script state hashes are equal at 1 to 32 threads on Linux; the Windows and macOS checks are listed for the user.
- The C API's positions are `double` in place (the only client is rebuilt with the header); `engine_h_coverage` and the snapshot pass; `docgen lint` is at zero; `ctest -L docs` passes.

## What stays `f32`

Rotations (unit quaternions), scales, directions, normals, colours, UVs, vertex data, velocities and extents, everything in a body's or a mesh's own space, and everything on the GPU. A difference
of two world positions is computed in `f64` and then narrowed.

## Days

1. [Design spec and spikes](double-precision/01-design-spec-and-spikes.md)
2. [The foundation math types](double-precision/02-foundation-math-types.md)
3. [Entity transforms in double precision](double-precision/03-ecs-transforms.md)
4. [A camera-relative renderer](double-precision/04-camera-relative-renderer.md)
5. [The spatial module in double precision](double-precision/05-spatial-module.md)
6. [Physics: bodies, broad phase, queries](double-precision/06-physics-bodies-and-broad-phase.md)
7. [Physics: narrow phase, solver, joints](double-precision/07-physics-narrow-phase-and-solver.md)
8. [Audio and script bindings](double-precision/08-audio-and-script-bindings.md)
9. [Assets, levels and the importers](double-precision/09-assets-levels-and-import.md)
10. [The C API and the C++ wrapper](double-precision/10-c-api-and-engine-h.md)
11. [Demos, benchmarks, docs and closing](double-precision/11-demos-benchmarks-and-closing.md)

## Rules for every day

- `scripts/test-phase.sh --start-day` first; `--affected` plain at the end, then `--flavor asan` and `--flavor tsan` when the day touches concurrency, lifetime or memory code; quote the script's `ran X of Y` line in the day file and `STATUS.md` (a reduced run is never "all tests pass").
- Builds clean (warnings are errors) with clang and GCC, unity on and off. Wrapped types only, no `std`; `build_defs.h` for platform macros; `scripts/format.sh`.
- `docgen lint` at zero, `docs/reference/` regenerated and `ctest -L docs` for any public header or guide change; published docs name no phases or days.
- End-of-day `/code-review high`, recorded under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` in the same commit; commit and push.
