# Maintenance: the query index and the hierarchy index

This is not a numbered phase. It is a maintenance series of two days, which the user asked for on 2026-11-19 ("plan both"). It changes neither
`CURRENT_PHASE` nor any phase or day number.

**Origin:** three items the [container audit](container-audit.md) left for later, with Day 1's numbers (`KNOWN-ISSUES.md`, the container audit
entries (3) and (5)):
- **Rays:** 8,699 rays per second against 5,000 bodies, 115 µs per ray (`physics_bench --scene rays`).
- **Characters:** 288 ms per step for 2,000 characters, quadratic (`physics_bench --scene characters`).
- **`SetParent`:** 29,491 ns per call at 4,000 entities, 18,441 after the audit, still quadratic (`ecs_bench`, `container_bench`).

The first two have one cause. `RayCast`, `RayCastAll`, `ShapeCast`, `Overlap` and `ClosestPoint` test every collider (`ForEachCollider`,
`engine/physics/query.cpp`), and the character controller makes about five of those queries per character per step. The third is `SetParent`,
`ClearParent` and `DestroyRecursive` collecting every `Parent` link in the world on each call and walking the list once per level
(`engine/ecs/transform.cpp`).

**Why the audit did not fix them** (container audit Days 8 and 10):
- **The broad phase's spatial set cannot serve the queries as it is.** It is brought up to date inside `Step`, before the bodies move, so between steps
  and during a step's character moves it does not match the world.
- **Visit order and `RayCast`'s reach.** `RayCast` shrinks its reach as it finds hits, so a different visit order was not proven to give bit-equal results.
- **The ECS has no children index,** and no per-type change counter that would show when a raw `Set<Parent>`, a removal or a destruction changed the
  hierarchy behind `SetParent`'s back.

**Depends on:** Phase 18 (`engine/spatial`: `SpatialSet`, `DynamicBvh`, `StaticBvh`), Phase 25 (physics, its determinism tests and state hashes),
Phase 17 (ECS), and the container audit (`DenseMap`, `HashMap`, `Sort`).

**Goal:**
- Queries visit only the colliders whose bounds can be hit, and return bit-identical results to today's loop.
- `SetParent`, `ClearParent` and `DestroyRecursive` cost the size of the subtree they change, not the size of the world.

**Definition of done:**
- **Rays and characters:** `physics_bench` rays at 5,000 bodies are at least 10 times Day 1's rate, and characters grow about linearly from 500 to 2,000
  (the numbers are recorded).
- **`SetParent`:** building a hierarchy of 1,000 and 4,000 entities grows about linearly (the numbers are recorded).
- **Determinism:** the physics reference state hashes, the C sample checksum `532bfea0`, the script world hashes (1 to 16 threads) and the propagation
  results are unchanged.
- **Oracles:** every query is checked against today's brute-force loop, kept as a test oracle, and the hierarchy against today's link scan.
- **Runs and review:** the scoped runs in every flavor that each day names pass. Windows and macOS are listed in `KNOWN-ISSUES.md` for the user's
  machines. Each day gets its `/code-review high`, recorded in its file.

## Days

1. [Physics: a query index kept current, queries through it](query-and-hierarchy-indices/01-physics-query-index.md)
2. [ECS: a children index for SetParent, ClearParent and DestroyRecursive; the series close](query-and-hierarchy-indices/02-ecs-children-index.md)

## Rules for every day

- **The usual day rules apply:** `scripts/test-phase.sh --affected` plus the asan and tsan flavors (both days touch lifetime code, and physics queries
  run from jobs), `/code-review high`, `docgen lint` at zero, `docs/reference` regenerated, `STATUS.md` and `KNOWN-ISSUES.md` updated, commit and push.
  The container audit's "no per-day tests" rule does not carry over.
- **Results stay identical:** a change that moves a hash, a checksum or a query result is wrong, not a new baseline.
- **Windows and macOS** build and run both days on the user's machines, no later than the next phase close. Each day lists what is waiting in
  `KNOWN-ISSUES.md`.
