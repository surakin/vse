# Foundation: Math

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0

## Source of truth

- `engine/foundation/math/vector.h`
- `matrix.h`
- `quaternion.h`
- `transform.h`
- `math_util.h`

## API reference

Full reference: [Foundation: Math](../reference/foundation-math.md).

- [[Vec2]] — 2-component vector
- [[Vec3]] — 3-component vector
- [[Vec4]] — 4-component vector
- [[Mat3]] — 3x3 column-major matrix
- [[Mat4]] — 4x4 column-major matrix
- [[Quaternion]] — rotation quaternion
- [[Transform]] — position, rotation, scale

## Already known — seed material (verify against the code when writing)

- Vector/matrix/quaternion types are hand-rolled; scalar functions (sqrt/sin/cos/...) come from `<cmath>`
- Quaternions use the Hamilton convention; slerp provided

## Outline

### Conventions (handedness, row/column, units, angle units)

_TODO_

### Vectors

_TODO_

### Matrices

_TODO_

### Quaternions

_TODO_

### Transforms

_TODO_

### Numerical notes and tolerances

_TODO_

