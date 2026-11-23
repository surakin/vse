# Tutorial: Big Worlds: What Is `f64`, What Is `f32`, and Why

**Goal:** Know which numbers of the engine are double precision and which are single, why the line is where it is, and how a scene a hundred thousand kilometres from the origin keeps its millimetres and still draws without jitter.

**You need:** a built tree (`cmake --build build`); the samples on this page are compiled and run by `docgen samples`. The reference pages are [Math](../guide/foundation-math.md), [ECS](../guide/ecs.md) and [Renderer](../guide/renderer.md); a host program that uses the C API reads [The Engine as a Loadable Library](../guide/engine-library.md).

## The problem in one number

A `float` has 24 bits of mantissa. Near the origin that is plenty; far away the gap between two neighbouring values grows with the value itself. A world position of 100,000,000 metres (ten times the Earth's circumference) steps by 8 metres:

```cpp compile run standalone
#include "math/vector.h"
#include "log/output.h"

using namespace vse;

int main() {
    const f64 far = 1.0e8 + 0.25;                    // a quarter metre past 100,000 km
    const f32 as_float = static_cast<f32>(far);      // what a float keeps of it
    out::Print("as float: %.2f, error %.2f m\n", static_cast<f64>(as_float), far - static_cast<f64>(as_float));
    const Vec3d position(far, 2.0, far);             // a position in the engine
    const Vec3 narrowed = position.Narrow();         // narrowing is always spelled out
    out::Print("narrowed x %.2f\n", static_cast<f64>(narrowed.x));
    return 0;
}
```

```output
as float: 100000000.00, error 0.25 m
narrowed x 100000000.00
```

A `double` has 53 bits: at the same distance it steps by about 15 nanometres, and at the engine's world limit of 1e12 metres (about seven times the distance from the Sun to the Earth) it still steps by about a tenth of a millimetre.

## The rule

- **`f64`: where something is in the world.** Positions, translations and world-space boxes of entities, bodies, lights, cameras, sounds, spatial structures and level placements: `Vec3d`, `Aabbd`, `Placement::position`, `ecs::LocalTransform::position`, `ecs::WorldTransform::position`.
- **`f32`: everything else.** Rotations (`Quaternion`), scales, directions, velocities, forces, sizes, distances and times, anything measured in an object's own space or relative to something nearby, and every value the GPU sees.
- **Widening is free, narrowing is spelled.** A `Vec3` converts to a `Vec3d` on its own (nothing is lost); the other way is always the call `.Narrow()`, so a lost digit is never an accident. Subtract two world positions in `f64` and narrow the *difference*: a difference of nearby things is a small number, and small numbers are exact in `f32`.

## Placements: a position that keeps its digits

A `Placement` is a single-precision rotation-and-scale block plus a double-precision position. Composing placements multiplies the blocks in `f32` and adds the positions in `f64`; `RelativeTo(origin)` subtracts first and narrows after, which is the whole trick of far-away rendering:

```cpp compile run standalone
#include "math/placement.h"
#include "log/output.h"

using namespace vse;

int main() {
    const f64 far = 1.0e8;
    Placement ship = Placement::From(Vec3d(far, 0.0, far), Mat4::Identity());
    Placement turret = Placement::From(Vec3d(0.0, 2.0, 0.375), Mat4::Identity());
    const Placement turret_in_world = ship.Compose(turret);
    out::Print("turret y %.3f, z - far = %.3f\n", turret_in_world.position.y, turret_in_world.position.z - far);

    const Vec3d camera(far, 2.0, far);               // the camera is 0.375 m from the turret
    const Mat4 for_the_gpu = turret_in_world.RelativeTo(camera);
    out::Print("relative to the camera: %.3f %.3f %.3f\n", static_cast<f64>(for_the_gpu.col[3].x),
               static_cast<f64>(for_the_gpu.col[3].y), static_cast<f64>(for_the_gpu.col[3].z));
    return 0;
}
```

```output
turret y 2.000, z - far = 0.375
relative to the camera: 0.000 0.000 0.375
```

## Entities keep the exact position

`ecs::LocalTransform::position` is a `Vec3d`. The transform system composes parent and child like the sample above, and writes both the `WorldTransform::matrix` (single precision, for code that wants a matrix) and the exact `WorldTransform::position`. Code that needs the true place reads `position`; the renderer reads neither directly:

```cpp compile run standalone
#include "ecs/transform.h"
#include "log/output.h"

using namespace vse;

int main() {
    ecs::World world;
    world.SetResource(ecs::TransformReport{});

    ecs::LocalTransform place;
    place.position = Vec3d(1.0e8, 0.0, 1.0e8);
    const ecs::Entity ship = ecs::CreateTransformEntity(world, place);
    place.position = Vec3d(0.0, 2.0, 0.125);
    const ecs::Entity turret = ecs::CreateTransformEntity(world, place);
    ecs::SetParent(world, turret, ship);

    ecs::Schedule schedule;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Build();
    schedule.Run(world, 0.016f);
    const ecs::WorldTransform* turret_world = world.Get<ecs::WorldTransform>(turret);
    out::Print("exact z - far = %.3f\n", turret_world->position.z - 1.0e8);
    out::Print("matrix z - far = %.3f\n", static_cast<f64>(turret_world->matrix.col[3].z) - 1.0e8);
    return 0;
}
```

```output
exact z - far = 0.125
matrix z - far = 0.000
```

The matrix, narrowed, has lost the eighth of a metre; the exact position has not. That is why the engine's own systems (physics, spatial, audio, scripts, the renderer's sync) read `position`.

## Drawing: a render origin that follows the camera

The GPU computes in `f32`, so it must never be handed a number like 100,000,000. The renderer keeps a **render origin**, a `Vec3d` close to the camera, and gives the GPU every object's placement *relative* to it: `Placement::RelativeTo(origin)` (or `scene::RelativeMatrix` for an entity). Everything the GPU sees is then a few kilometres at most, whatever the world coordinates are. The camera's own matrix is relative to the origin too, so it sits near zero.

- `scene::SceneLink` holds the origin; `scene::UpdateRenderOrigin(link, camera_position)` moves it to the camera's position, rounded to a grid of `rebase_step` metres, once the camera has drifted that far; the next sync rewrites every object once (a "re-base", rare: the camera has to travel a whole step). A step of 0 keeps the origin at zero, the default.
- A host that uses the C API gets this for free: a surface re-bases at 4,096 m steps by itself.
- Anything that goes to the GPU and is *not* a position (colours, normals, light radii) is `f32` already.

`renderer_demo --origin-offset N` places the whole demo scene N metres from the origin on every axis, and a test compares the picture at 100,000 km with the picture at the origin: they agree.

## Physics and queries

Bodies, the broad phase and every query (ray, overlap, shape cast) are in `f64`. Inside a step, two bodies in contact are placed relative to the first one's origin (the contact's *anchor*), so the solver works with small numbers and a stack of boxes 10,000 km away behaves as at the origin to within micrometres. A ray's hit point is a `Vec3d`; its normal and distance are `f32`.

## What it costs

Measured in a Release build on the development machine, before and after the engine moved to double precision (the same scenes; medians of one run each, so differences of a few percent are noise):

| Benchmark | Single precision | Double precision |
|---|---|---|
| Physics, 5,000 bodies dropping: falling step | 17.6 ms | 18.1 ms |
| the same, landing (8,300 contacts) | 31.6 ms | 32.2 ms |
| Frame CPU time, 10,000 objects, one thread | 1.02 ms | 1.05 ms |
| Transform propagation, 100,000-entity hierarchy | 3.2 ms | 2.9 ms |
| Spatial: build 1,000,000 static boxes | 426 ms | 458 ms |
| Spatial: frustum cull of 1,000,000 boxes (tree, 1 percent visible) | 0.135 ms | 0.144 ms |
| Spatial: box overlap query in 1,000,000 boxes (4 m box) | 1.07 us | 1.01 us |

Memory: a world box is 48 bytes instead of 24, a position 24 bytes instead of 12; an entity's `WorldTransform` is 24 bytes larger. Nothing the GPU reads changed size.

## Which type do I use?

| I am holding... | Use |
|---|---|
| where an entity, body, light, camera or sound is | `Vec3d` |
| the box around such a thing in the world | `Aabbd` |
| which way it faces, its size, its speed | `Quaternion`, `Vec3` |
| the difference between two places | subtract in `Vec3d`, then `.Narrow()` |
| something I hand to the GPU | relative to the render origin, `Vec3` / `Mat4` |
| a position across the C API | `double[3]` |

## Where to go next

- [Math](../guide/foundation-math.md): the templates behind `Vec3d`, `Aabbd` and `Placement`.
- [ECS](../guide/ecs.md): transforms and hierarchy; [Spatial](../guide/spatial.md) and [Physics](../guide/physics.md): queries in `f64`.
- [The Engine as a Loadable Library](../guide/engine-library.md): positions as doubles across the C boundary.
