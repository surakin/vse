# Phase 0 / Day 14 — Math: Quaternions & Transforms

**Phase:** [Phase 0 — Foundation Layer](../phase-00-foundation.md)
**Depends on:** Day 13 (math: vectors & matrices).

## Tasks

- [ ] Implement `Quaternion` in `/engine/foundation/quaternion.h`:
  construction, multiply, conjugate/inverse, normalize, slerp.
- [ ] Implement quaternion↔matrix conversion (both directions).
- [ ] Implement `Transform` (position/rotation/scale) in
  `/engine/foundation/transform.h`: compose (parent * child), to-matrix
  conversion.
- [ ] Write a test rotating a known vector by a known quaternion and
  comparing against the hand-calculated expected result.
- [ ] Write a round-trip test: quaternion → matrix → quaternion
  recovers the original, within floating-point tolerance.
- [ ] Commit.

**Done:** rotating a known vector by a known quaternion matches the
expected result.
