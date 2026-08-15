# Phase 0 / Day 13 — Math: Vectors & Matrices

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 9 (logging & assertions) — uses the assert macro for
internal invariants; not a hard data dependency.

## Tasks

- [x] Implement `Vec2`/`Vec3`/`Vec4` in `/engine/foundation/math/vector.h`:
  component access, add/sub/scale, dot product, cross product (Vec3),
  normalize.
- [x] Implement `Mat3`/`Mat4` in `/engine/foundation/math/matrix.h`:
  multiply, transpose, construction helpers (identity, from basis
  vectors).
- [x] Write tests cross-checking dot, cross, normalize, and multiply
  against hand-calculated reference values, at least 2-3 non-trivial
  cases each.
- [x] Commit.

**Done:** results cross-checked against hand-calculated reference
values.
