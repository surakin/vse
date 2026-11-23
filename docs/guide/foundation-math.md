# Foundation: Math

Status: written.

**Audience:** every engine programmer. Read [Standard types](foundation-types.md) first (`f32`, the scalar functions `Sqrt`, `Sin`, ...).

The math library is deliberately small: three vector types, two matrix types, a quaternion and a transform. They are plain structs of `f32`s, with no hidden
allocation, no SIMD tricks and no special alignment. If you need something that is not there (an inverse matrix, a lerp), look in the module that needs it
before writing your own: the renderer, for instance, has the projection and view helpers in `renderer/camera.h`.

| Type | What it is | Header (`engine/foundation/math/`) |
|---|---|---|
| `Vec2`, `Vec3`, `Vec4` | 2, 3 and 4 floats: positions, directions, colors | `vector.h` |
| `Mat3`, `Mat4` | 3x3 and 4x4 matrices | `matrix.h` |
| `Quaternion` | a rotation | `quaternion.h` |
| `Transform` | position + rotation + scale | `transform.h` |
| `kPi` | the constant pi | `math_util.h` |

## Conventions (read this once)

Most math bugs are a disagreement about conventions, so the engine fixes them and every function follows them:

- **Right-handed, Y up.** X points right, Y up, and the camera looks down **-Z**. The rotation of a positive angle about an axis is counter-clockwise when you look
  down the axis toward the origin.
- **Column vectors, column-major matrices.** A vector is a column and is transformed by multiplying on the **left**: `v' = M * v`. Each matrix stores its
  columns (`m.col[0]`, ...). This is the same convention as GLM and OpenGL.
- **Composition reads right to left.** `B * A` applies `A` first, then `B`. The same holds for quaternions: `a * b` rotates by `b` first.
- **Angles are radians**, in `f32`. `kPi` is pi; convert degrees yourself (`degrees * kPi / 180.0f`).
- **Units are whatever you decide** (the demos use metres), but be consistent.

The graphics conventions that sit on top of these (the Vulkan clip space, the triangle winding) are in
[Depth, indexed draws and the 3D conventions](rhi.md#depth-indexed-draws-and-the-3d-conventions).

## Vectors

`Vec2`, `Vec3` and `Vec4` have public `x`, `y`, `z`, `w` fields, constructors from components (and `Vec4(Vec3, w)`), `+`, `-`, `* scalar`, `==`, indexing, `Dot`,
`Length`, `LengthSquared` and `Normalized`. `Vec3` also has `Cross`.

```cpp compile run standalone
#include "math/vector.h"
#include "log/output.h"

using namespace vse;

int main() {
    Vec3 right(1.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);

    Vec3 forward = right.Cross(up);                    // right-handed: X cross Y = +Z
    out::Print("X cross Y = (%.0f, %.0f, %.0f)\n", static_cast<f64>(forward.x), static_cast<f64>(forward.y), static_cast<f64>(forward.z));

    Vec3 offset = Vec3(3.0f, 4.0f, 0.0f);
    out::Print("length %.1f, squared %.1f\n", static_cast<f64>(offset.Length()), static_cast<f64>(offset.LengthSquared()));

    Vec3 direction = offset.Normalized();
    out::Print("direction (%.1f, %.1f), dot with X %.1f\n", static_cast<f64>(direction.x), static_cast<f64>(direction.y), static_cast<f64>(direction.Dot(right)));

    Vec3 target = Vec3(1.0f, 2.0f, 3.0f) + offset * 0.5f;
    out::Print("target (%.1f, %.1f, %.1f)\n", static_cast<f64>(target.x), static_cast<f64>(target.y), static_cast<f64>(target.z));
    return 0;
}
```

```output
X cross Y = (0, 0, 1)
length 5.0, squared 25.0
direction (0.6, 0.8), dot with X 0.6
target (2.5, 4.0, 3.0)
```

Tips:

- **Compare lengths with `LengthSquared`.** It skips the square root. "Is B within 5 metres of A?" is `(b - a).LengthSquared() < 25.0f`.
- **`Normalized()` of a zero vector aborts** (`ENGINE_ASSERT`): check for zero length first where it can happen.
- **`==` compares exactly.** Floating-point results rarely match exactly; compare with a tolerance (below).

## Matrices

`Mat3` and `Mat4` hold their columns in `col[]` (`Vec3`s and `Vec4`s). Build one with `Identity()`, `FromBasis(x_axis, y_axis, z_axis[, translation])` or (for `Mat4`)
`FromColumns`. Multiply a matrix by a vector or by another matrix with `*`; `Transpose()` flips it. A `Mat4` transforms a `Vec4`: use `w = 1` for a **point** (it is
moved by the translation) and `w = 0` for a **direction** (it is not).

```cpp compile run standalone
#include "math/matrix.h"
#include "log/output.h"

using namespace vse;

int main() {
    // Move everything 10 units along X; axes unchanged.
    Mat4 move = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(10, 0, 0));
    // Scale by 2 on every axis.
    Mat4 grow = Mat4::FromBasis(Vec3(2, 0, 0), Vec3(0, 2, 0), Vec3(0, 0, 2), Vec3(0, 0, 0));

    Vec4 point(1, 1, 1, 1);        // w = 1: a position
    Vec4 direction(1, 1, 1, 0);    // w = 0: a direction

    Vec4 a = (move * grow) * point;      // grow first, then move
    Vec4 b = (grow * move) * point;      // move first, then grow
    Vec4 c = move * direction;           // translation does not affect a direction
    out::Print("move*grow: x=%.0f\n", static_cast<f64>(a.x));
    out::Print("grow*move: x=%.0f\n", static_cast<f64>(b.x));
    out::Print("direction after move: x=%.0f\n", static_cast<f64>(c.x));
    return 0;
}
```

```output
move*grow: x=12
grow*move: x=22
direction after move: x=1
```

The order matters: `move * grow` is "grow, then move" (the rightmost applies first).

## Quaternions

Rotations are stored as a `Quaternion` (`x, y, z, w`, Hamilton convention, `w` is the scalar part): four numbers that compose without the gimbal lock of Euler
angles and interpolate smoothly. You almost never touch the components; use the functions:

| To... | Use |
|---|---|
| make "no rotation" | `Quaternion::Identity()` |
| rotate by an angle about an axis | `Quaternion::FromAxisAngle(axis, radians)` |
| turn a vector | `q.RotateVector(v)` |
| combine two rotations | `a * b` (rotates by `b` first, then `a`) |
| undo a rotation | `q.Inverse()` (or `Conjugate()` for a unit quaternion) |
| get a matrix | `q.ToMat3()`, `q.ToMat4()`; and back with `Quaternion::FromMat3(m)` |
| blend between two rotations | `Slerp(a, b, t)` with `t` from 0 to 1 |
| keep it a rotation after many operations | `q.Normalized()` |

```cpp compile run standalone
#include "math/quaternion.h"
#include "log/output.h"

using namespace vse;

int main() {
    const Vec3 up(0, 1, 0);
    // A quarter turn about Y turns +X toward -Z (right-handed).
    Quaternion quarter = Quaternion::FromAxisAngle(up, kPi / 2.0f);
    Vec3 turned = quarter.RotateVector(Vec3(1, 0, 0));
    out::Print("+X turned: (%.1f, %.1f, %.1f)\n", static_cast<f64>(turned.x), static_cast<f64>(turned.y), static_cast<f64>(turned.z));

    // Two quarter turns make a half turn.
    Vec3 half = (quarter * quarter).RotateVector(Vec3(1, 0, 0));
    out::Print("+X after two: (%.1f, %.1f, %.1f)\n", static_cast<f64>(half.x), static_cast<f64>(half.y), static_cast<f64>(half.z));

    // Halfway between no rotation and a quarter turn is an eighth turn.
    Quaternion eighth = Slerp(Quaternion::Identity(), quarter, 0.5f);
    Vec3 mid = eighth.RotateVector(Vec3(1, 0, 0));
    out::Print("halfway: x=%.2f z=%.2f\n", static_cast<f64>(mid.x), static_cast<f64>(mid.z));
    return 0;
}
```

```output
+X turned: (0.0, 0.0, -1.0)
+X after two: (-1.0, 0.0, 0.0)
halfway: x=0.71 z=-0.71
```

Multiplying many quaternions accumulates rounding error, so a long-lived rotation (a camera) should call `Normalized()` every so often. `Normalized()` and `Inverse()` of a
zero-length quaternion abort.

## Transforms

A `Transform` is the usual way to place an object: a `position`, a `rotation` quaternion and a per-axis `scale` (default 1). `ToMatrix()` gives the 4x4
matrix that scales, then rotates, then translates. `Compose(parent, child)` puts a child (given in the parent's space) into the parent's space, which is how a hierarchy
is flattened: the child's position is scaled and rotated by the parent and then offset.

```cpp compile run standalone
#include "math/transform.h"
#include "log/output.h"

using namespace vse;

int main() {
    Transform parent;
    parent.position = Vec3(10, 0, 0);
    parent.rotation = Quaternion::FromAxisAngle(Vec3(0, 1, 0), kPi / 2.0f);   // quarter turn about Y

    Transform child;
    child.position = Vec3(1, 0, 0);          // one unit in front of the parent, in the parent's space

    Transform world = Compose(parent, child);
    out::Print("child in world space: (%.1f, %.1f, %.1f)\n", static_cast<f64>(world.position.x), static_cast<f64>(world.position.y),
              static_cast<f64>(world.position.z));

    Mat4 matrix = world.ToMatrix();
    Vec4 origin = matrix * Vec4(0, 0, 0, 1);  // the child's own origin lands at its world position
    out::Print("origin via matrix: x=%.1f z=%.1f\n", static_cast<f64>(origin.x), static_cast<f64>(origin.z));
    return 0;
}
```

```output
child in world space: (10.0, 0.0, -1.0)
origin via matrix: x=10.0 z=-1.0
```

`Compose` is the simple TRS composition: it is exact when the parent's scale is uniform, and not for a non-uniform parent scale combined with a rotated child (a
shear cannot be expressed in position/rotation/scale). The ECS builds its transform hierarchy on it ([ECS](ecs.md)).

## Double precision

The vector, box, sphere, plane, ray and frustum types are templates on the scalar (`Vec3T<S>`, `AabbT<S>`, `FrustumT<S>`, ...).
The old names are aliases of the single-precision form (`Vec3 = Vec3T<f32>`, `Aabb`, `Frustum`), and the double-precision
forms are `Vec2d`, `Vec3d`, `Vec4d`, `Aabbd`, `Sphered`, `Boundsd`, `Planed`, `Rayd` and `Frustumd`. The rule for the engine is:
a world position (a translation, a world box) is `f64`; rotations, scales, directions, normals, own-space values and everything
sent to the GPU are `f32`.

- Widening `f32` to `f64` is implicit and exact (a `Vec3` converts to a `Vec3d`, an `Aabb` to an `Aabbd`).
- Narrowing is always the named call `Narrow()`, so it cannot happen by accident. Compute a difference of two world positions in
  `f64` first, then narrow it: the result is the nearest `f32` of the exact difference.
- `Mat4` and `Quaternion` stay `f32`. `Placement` (`math/placement.h`) is a single-precision linear part (rotation and scale)
  plus a `Vec3d` position; `Placement::RelativeTo(origin)` gives the `Mat4` whose translation is `position - origin`
  computed in `f64`, which is how a far-away object is drawn precisely.
- `FrustumT<S>::Translated(origin)` moves a frustum built in camera space into world space, so an `f64` scene is culled
  without first converting every box.
- The out-of-line functions are instantiated for both scalars in `geometry.cpp`; they use `+ - * / Sqrt` only, so results are
  the same on every machine.

## Numerical notes

- **`f32` has about seven significant digits.** Far from the origin (tens of kilometres) positions lose precision; large worlds shift the origin.
- **Never compare floats with `==` after computing them.** Use a tolerance: `Abs(a - b) < 1e-5f` (`Abs` from `lang/scalar.h`), scaled to the size of the values.
- **Normalize directions, but not too often.** A unit vector's length drifts by a few parts in 10^7 per operation; renormalize after chains of operations, not every
  step.
- **Angles are radians everywhere**, including the parameters of the camera helpers.
- The scalar functions (`Sqrt`, `Sin`, `Cos`, `Acos`, `Floor`, `Abs`, `IsNaN`, ...) are in `lang/scalar.h`, not `<cmath>` ([Standard types](foundation-types.md)).

## Common mistakes

- **Multiplying in the wrong order.** `A * B` applies `B` first. If an object scales and moves in the wrong order, swap the factors.
- **Using `w = 1` for a direction (or `w = 0` for a position).** Then a translation is applied to a direction, or ignored for a point.
- **Composing rotations with a non-normalized quaternion.** The result is a scaled rotation. Normalize.
- **Expecting a left-handed or Z-up world.** Assets from other tools may need a conversion at import.
- **Degrees where radians are expected.** There is no automatic conversion.

## See also

[Standard types](foundation-types.md), [Renderer](renderer.md) (cameras and projections), [ECS](ecs.md); reference: [Foundation: Math](../reference/foundation-math.md).
