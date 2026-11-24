# Maintenance / linux-known-issues / Day 03 — The height-field collider: contacts, recovery and snapshots

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] More than 1,024 triangles (`engine/physics/collide_hull.cpp`, `FieldTriangles::kMaxListed`, `TrianglesPair::kMaxTriangles`): collide a large shape against the field in blocks (the field's block minima and maxima prune), so a 20 m box on a 1 m grid gets contacts over its whole footprint; keep the step deterministic (a fixed block order, `+ - * /` and `Sqrt` only); test: a wide flat box rests level on a flat field and its state hash is equal at 1 to 32 threads; the terrain golden hashes are re-baselined once, deliberately, and listed for the PC and the Mac.
- [ ] A body wholly under the surface (a `SetTransform` or a fast fall): `QueryBox` keeps the cells under the box's lowest point when a body is under the field, and the push-out lifts it; test: teleport a sphere 5 m under flat ground, step, and it is on top.
- [ ] The ridge push-out (`collide_hull.cpp`, the `up_depth` measure): measure the depth to the triangle's own extent, not its infinite plane, when the contact is on an edge; test: a 3 by 3 ridge with 60 degree slopes and a sphere resting at the crest, contact normals and depths bounded.
- [ ] Snapshot identity (`snapshot.cpp`, `HullKnown`): store the field's `ContentHash()` in the snapshot (version 3, version 2 refused as version 1 was) and compare it on restore; `LibraryMismatch` for another map of similar size; test.
- [ ] `TerrainHeight` and `TerrainRayCast` through the query index (`query.cpp`) instead of `ForEachCollider`; a benchmark line in `physics_bench` for 200,000 calls over 100 colliders; the result is unchanged (test against the old scan).
- [ ] Ray edge slack and the block skip: replace the absolute `kSlack` of 1e-3 with a relative one (a few ulps of the height range) and test a grazing ray at y of 5e4; `QueryBox` and the debug draw read the same limit.
- [ ] Contact events of a character's capsule name the entity (Phase 25 Day 409), and destroying a body emits the end and exit events of its pairs (Day 405); tests; the physics guide says so.
- [ ] The character's step-up probe walks only as far as the tread needs (Phase 25 Day 404); test: a ledge higher than `step_height` is not climbed from a short tread.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** The height-field collider has no silent contact cap, recovers a buried body, restores only its own map, and the physics hashes are re-baselined once with the new values listed for the other machines; the entries for Days 405, 409, 491, 492 and the review's physics items are removed.

## Post-implementation code review

(To be filled in when the day is done.)
