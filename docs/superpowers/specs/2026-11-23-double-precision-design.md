# Double precision for world positions: design

Date: 2026-11-23 (maintenance series `plans/maintenance/double-precision.md`, Day 1). Decision of the user (2026-11-23): the engine needs Unreal-style large
world coordinates, double precision wherever a world position lives, after Phase 28. Written from a survey and four spikes. The spike programs were throwaway
code (clang -O2, one core of a Ryzen 9 7950X3D, scratch directory, not kept); the method is in each section. **The kernels are synthetic: they show that the
arithmetic is not the cost, not what the real physics step costs; Day 6 and 7 measure that with `physics_bench` before and after.**

## The rule

- `f64`: a world-space **position** (a translation, a point in the world, an origin of a ray or a box, a listener or emitter position, a camera position) and a world-space box.
- `f32`: rotations (unit quaternions), scales, directions, normals, colours, UVs, vertex data, velocities, extents, a body's or a mesh's own-space values, every GPU value,
  and any quantity that is a **difference of two world positions** (computed in `f64`, narrowed once).
- The narrowing is explicit and named (`Narrow`, `RelativeTo`), never an implicit conversion; the widening of an `f32` value to `f64` is implicit (it is exact).

## Survey: where positions live

Files and uses of `Vec3`, `Mat4`, `Quaternion`, `Aabb`, `Sphere` (git grep, 2026-11-23):

| Module | Files | Uses | What changes |
|---|---|---|---|
| `foundation/math` | 6 | 265 | the new types (Day 2) |
| `ecs` | 2 | 33 | `LocalTransform` translation, `WorldTransform` (Day 3) |
| `scene` | 3 | 21 | sync of transforms to the render scene (Days 3, 4) |
| `renderer` | 24 | 291 | camera, `RenderScene`, culling, frame writes, terrain tile selection (Day 4) |
| `spatial` | 18 | 198 | grid, BVH, queries, pair feed (Day 5) |
| `physics` | 49 | 1,045 | body positions, broad phase, queries, snapshot, hash (Days 6, 7) |
| `audio`, `audio_physics` | 8 | 60 | listener, emitters, occlusion rays (Day 8) |
| `script`, `script_physics` | 13 | 229 | `Value` vectors, the bindings (Day 8) |
| `asset`, `asset_import` | 18 | 66 | level placements, node transforms in the importer (Day 9) |
| `storage` | 4 | 35 | a `Vec3` field kind and the save format (Day 3) |
| `engine_c` | 7 | 125 | the C API (Day 10) |
| tests, game, tools | 130 | 3,417 | compile unchanged where they use `f32` forms; the far-from-origin tests are new |

The C API has 24 `float[3]` fields in `engine_c.h` (transform position, collider offset, ray and query points, lights, audio) and 146 `float` fields in all.

## Spike 1: the type

Candidates: separate types (`Vec3d`, `Aabbd`, ... beside the existing `f32` ones), or a template on the scalar with `Vec3 = Vec3T<f32>` and `Vec3d = Vec3T<f64>`.
Compile time of 3,000 functions using five vector operations on each of two scalar types, at `-O0` (the development build): separate structs 2.56 s, template 2.58 s: no
difference. **Pick: the template (the user's decision, 2026-11-23, over the recommendation of separate types).** One definition of each operation serves both precisions, so a fix
or a new operation is made once and the two cannot drift apart. The existing code is unaffected: `Vec3` stays the name of the `f32` vector (an alias), and there is no forward
declaration of `Vec2`, `Vec3`, `Vec4`, `Aabb`, `Sphere`, `Frustum` or `Plane` anywhere (git grep), and one explicit specialisation of a type on them (`script::Codec<Vec3>`), which keeps
working through the alias.

The cost of the template, and how the design contains it:
- **Mixed arithmetic is explicit.** `Vec3T<f32> + Vec3T<f64>` has no implicit meaning in a template: the operators are defined for equal scalars, plus the mixed forms that are
  exact (`Vec3d` with `Vec3` gives `Vec3d`: the `f32` side widens exactly); the narrowing of a `Vec3d` to `Vec3` is a named call (`Narrow`), never an operator or a conversion.
- **Argument deduction.** A function template taking `Vec3T<S>` deduces from either; non-template functions keep taking `Vec3` (the `f32` alias) so a caller that passes a `Vec3d` to one
  is a compile error, which is the point.
- **Compile-time and error text.** Measured equal above; diagnostics name `Vec3T<float>` where they named `Vec3`, which clang prints through the alias.
- **Explicit instantiation.** The non-inline members (`Normalized`, `Length`, anything out of line) are explicitly instantiated for `f32` and `f64` in `math/` so no other module instantiates them
  (one definition, one place to port; the wrapper rule for math functions through `lang/scalar.h` holds for both).

The types (Day 2): `Vec2T`, `Vec3T`, `Vec4T`, `AabbT`, `SphereT`, `PlaneT`, `FrustumT` templated on the scalar, with the aliases `Vec3 = Vec3T<f32>`, `Vec3d = Vec3T<f64>`, `Aabb`, `Aabbd`, `Sphere`,
`Sphered`, `Frustum`, `Frustumd`, and so on. `Mat4` and `Quaternion` stay `f32` (rotation and scale are `f32` by the rule). **`Placement`** is a plain struct (not a template): a rotation-and-scale part as the existing `f32`
`Mat4` linear block (its translation column unused) plus a `Vec3d` position, with `RelativeTo(Vec3d origin)` giving the `f32` `Mat4` the GPU gets. `LocalTransform` keeps `Quaternion` and `Vec3 scale` and gets a
`Vec3d position`.

## Spike 2: physics

Candidates: (A) body positions and broad-phase boxes in `f64`, contacts built **body-relative** in `f32` (the second shape is moved into the first's frame by an `f64`
difference, narrowed once; manifolds, the solver and joints stay `f32`), against (B) `f64` throughout.

Kernels (200,000 bodies, scalar code as the engine writes it, no SIMD):

| Kernel | `f32` | `f64` |
|---|---|---|
| Box overlap tests, 12.8M | 11.1 ms | 7.9 ms |
| the same, 4M boxes (96 MB against 192 MB), 256M tests | 220 ms | 158 ms |
| Sphere pair contacts, 3.2M | 2.64 ms | 2.71 ms |
| Solver row updates, 1.6M | 5.08 ms | 4.86 to 5.12 ms |

Memory: `Vec3` 12 bytes against 24, `Aabb` 24 against 48. The arithmetic costs the same in this code (scalar and branchy; the `f32` box test is slower here only by
code generation), and a sequential sweep over twice the data does not slow down; what doubles is the footprint of what is stored in `f64`, which matters for a random access
to a million bodies, and which this series does not measure until the real step exists. Baseline of the real step (`physics_bench --scene drop --bodies 5000 --threads 1`, Release):
18 ms falling, 32 ms landing (broad 8.8, narrow 2.2, solve 19.6).

**Pick: (A).** Positions and the broad phase in `f64`, the narrow phase, manifolds, solver and joints body-relative in `f32`. It is the smaller change (physics has 1,045 uses,
most of them in the narrow phase and solver that stay as they are), keeps the solver's numerics and its tests, and costs the same arithmetic. Contact points and events reach the
caller as `f64` world points (the anchor plus the relative point). Determinism: the new operations use `+ - * / Sqrt` only, the golden hashes are re-baselined once on Day 6
(positions hash as `f64`) and must again be equal for 1 to 32 threads. If Day 7's measurement shows (A)'s per-pair subtraction costs more than 5% of the narrow phase, the spec is
amended there, not worked around.

## Spike 3: camera-relative rendering

100,000 objects with an `f64` translation and an `f32` rotation and scale:

| Work | Time |
|---|---|
| Write the world and the normal matrix of each object, subtracting an `f64` camera position, output `f32` | 0.43 ms |
| Cull 100,000 boxes against six planes, all `f32` | 1.00 ms |
| the same with `f64` world boxes and `f64` planes | 0.99 ms |
| `f64` world boxes, subtract the camera, then the `f32` test | 1.06 ms |

(For comparison `FrameResources::WriteObjects` today takes 1.27 ms for 100,000 objects, which includes the normal matrix, and `GBufferPass::Prepare` 30.7 ms all told.)

**Pick: the scene is stored in `f64` world space and culled in `f64` world space**; the camera's frustum is built in `f64` from its `f64` position, so the cull needs no
subtraction and costs the same as today. **The subtraction happens once, where the GPU data is written** (`FrameResources::WriteObjects`, the instance compaction, the terrain
tile uniforms): that is what Day 4 builds. The BVH holds `f64` boxes; the transparent, UI and text layers are screen space and do not change. The render origin is the camera's
position unless a host sets another.

## Spike 4: formats and the ABI

- **Physics snapshot** (`engine/physics/snapshot.cpp`, `kVersion = 2`): body positions become `f64`; version 3; current version only.
- **Storage** (`engine/storage/field.h`: the `FieldKind::Vec3` and `Aabb` kinds, a store's `format_version`): a `Vec3d` and `Aabbd` kind are added, the entity save format
  version moves, saves are not converted at load.
- **Level and minted blobs** (`asset/level.h` and the mint tools): a level's placements hold an `f64` translation; the level blob type version moves; levels are re-minted
  offline (the 2026-11-13 asset rules: no load-time conversion, no old versions); mesh, skeleton, clip, texture and height-map blobs are object-space and do not change.
- **Import fingerprint** (`engine_api.import_format_version`) changes with the level type version; `host_import.c`'s expected value is updated in the same commit.
- **C API** (24 `float[3]` fields): every struct that carries a world position gets an `f64` sibling by the sized-struct rules (a new struct such as `engine_transform64`, or new
  fields appended after the old ones with `struct_size` deciding), never a change of the existing layout; `engine_api` grows at the end; the old entry points keep their `f32`
  forms, narrow, and are documented as lossy far from the origin. `engine.h` wraps every new member.
- **Scripts:** Lua numbers are already `f64` (`script::Value::FromFloat(f64)`, `AsFloat` returns `f64`). The vector kinds of a `Value` (`Vec2`, `Vec3`, `Vec4`, `Quat`) are `f32`, so a
  position that travels as a `Value` vector narrows; Day 8 adds `f64` vector kinds (or passes positions as three numbers) and the bindings that return positions use them.

## What does not change

Everything near the origin behaves as before (the rule: a scene at the origin renders the same and simulates the same, apart from the one deliberate re-baseline of the physics golden
hashes and the script state hashes that contain positions); the GPU formats, the shaders, the asset blobs of meshes and textures, the RHI.

## Days

No day is added or removed. The day files carry the picks: Day 2 builds the scalar templates (`Vec3T` and the rest, aliased as `Vec3`, `Vec3d`, `Aabbd`, `Frustumd`, ...) and `Placement`; Day 3 `LocalTransform` and the storage kinds;
Day 4 the `f64` scene with the subtraction at the write; Days 6 and 7 option (A) with the measurement that can amend it.
