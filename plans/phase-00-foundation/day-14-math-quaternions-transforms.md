# Phase 0 / Day 14 — Math: Quaternions & Transforms

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 13 (math: vectors & matrices).

## Tasks

- [x] Implement `Quaternion` in `/engine/foundation/math/quaternion.h`:
  construction, multiply, conjugate/inverse, normalize, slerp.
- [x] Implement quaternion↔matrix conversion (both directions).
- [x] Implement `Transform` (position/rotation/scale) in
  `/engine/foundation/math/transform.h`: compose (parent * child), to-matrix
  conversion.
- [x] Write a test rotating a known vector by a known quaternion and
  comparing against the hand-calculated expected result.
- [x] Write a round-trip test: quaternion → matrix → quaternion
  recovers the original, within floating-point tolerance.
- [x] Commit.

**Done:** rotating a known vector by a known quaternion matches the
expected result.
