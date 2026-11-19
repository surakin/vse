# Query and hierarchy indices / Day 1: Physics: a query index kept current, queries through it

**Series:** [the query index and the hierarchy index](../query-and-hierarchy-indices.md)
**Depends on:** the container audit (Day 8's notes on why the broad phase's set was not used), Phase 18's `SpatialSet`, Phase 25's queries and
determinism tests.

The queries test every collider. This day gives the world a second spatial set, the query index, that matches the world at every moment a query can run.
A query then asks it for candidates, sorts them by collider slot and runs today's per-collider code on them unchanged. A collider that is not a candidate
fails today's box test anyway, and the candidates are visited in today's order with today's reach. The result is therefore bit-identical by construction,
and no proof about the reach is needed.

## Tasks

- [ ] **Read before writing:**
  - every writer of a body's pose or a collider's shape. Known so far:
    - `CreateBody`, `DestroyBody`, `AddCollider`, `SetCollider`, `SetTransform` (`world.cpp`);
    - `IntegratePosition` (`step.cpp`, `solver.cpp`), the CCD rewind (`ccd.cpp`), the island sleep and wake paths (`island.cpp`);
    - `Restore` (`snapshot.cpp`).
  - Also every caller outside `engine/physics/` that writes through the non-const `PhysicsWorld::Find` or `FindCollider` (grep
    `engine/`, `game/`, `tests/`). Every pose or shape write outside the world goes through `SetTransform` or `SetCollider`; record the list here.
  - when in `Step` the pre-step hook and the character controller's queries run, and what they must see.
- [ ] **The query index** (`engine/physics/query_index.h/.cpp`, private to the module; the world owns one):
  - a `spatial::SpatialSet` keyed by `ColliderId::ToKey()`: static colliders in its static tree, the others in its dynamic tree with fat boxes (the
    broad phase's margins);
  - every box it holds contains the collider's exact current bounds (`Shape::Bounds` at `PoseOf`), so a candidate list is always a superset;
  - updated eagerly by the world's mutating calls, never lazily inside a query: queries are `const` and run concurrently from jobs
    (`Query: queries run from jobs ...`), so the index is not changed while they read it;
  - after a step moves bodies: one update of the awake moving bodies' colliders, so a box is re-inserted only when its exact bounds leave the fat box;
  - `Restore` rebuilds it whole;
  - `Stats()` (reinsertions, size), for the tests.
- [ ] **Queries through it** (`query.cpp`):
  - `ForEachCandidate(world, region or ray, filter, f)` collects candidate slots from the index, sorts them by slot and calls today's per-collider
    lambda on each. The lambda bodies of `RayCast`, `RayCastAll`, `ShapeCast`, `Overlap` and `ClosestPoint` do not change;
  - rays use `TraverseRay` with the full `max_distance` (the shrinking reach stays in the lambda, as today);
  - shape casts use the swept box, overlaps the shape's box, and `ClosestPoint` the point grown by `max_distance`;
  - the candidate list is scratch the caller owns: one `Vector` per call on the stack, no shared mutable state (thread-safe as today).
- [ ] **The oracle:**
  - today's `ForEachCollider` loop moves to the tests (`tests/physics/query_oracle.h`) unchanged;
  - a test over random scenes (every shape kind, static, dynamic, kinematic, sleeping, triggers, filters and layers), with bodies created, moved,
    teleported, destroyed and stepped between queries, compares every query's full result with the oracle's bit for bit;
  - a query from the pre-step hook and the character controller's casts inside a step agree with the oracle;
  - a query after `Restore` agrees.
- [ ] **Unchanged results:**
  - `golden_test`, `determinism_test`, `snapshot_test`, `character_test`, `ccd_test` and the script physics tests pass with unchanged hashes;
  - the C physics sample prints checksum `532bfea0`;
  - the script world hashes are unchanged at 1 to 16 threads.
- [ ] **Numbers** (`physics_bench --scene rays` and `--scene characters`, 3 runs, alternating with a build of the commit before this day):
  - rays at 5,000 bodies: Day 1 8,699 per second, target at least 10 times that;
  - characters at 100, 500 and 2,000: Day 1 1.38, 20.4 and 288 ms per step, target about linear growth;
  - `Step` alone at the `drop` and `large` scenes, which must not get slower by more than the noise (the index update is new work per step);
  - recorded in this file and in `KNOWN-ISSUES.md`'s entry (3), which loses its query part.
- [ ] **Docs:**
  - `docs/guide/physics.md`: the queries' cost (candidates from an index; the cost is the colliders near the query) and when the index is updated;
  - `///` on any public header that changes; `docgen lint` at zero; `docs/reference` regenerated.
- [ ] **Standing rows:**
  - `scripts/test-phase.sh --affected` (physics, script_physics, engine_c), then `--flavor asan` and `--flavor tsan`; quote each `ran X of Y` line here
    and in `STATUS.md`;
  - Clang unity, GCC non-unity, both clean; `module_deps_check` (physics already depends on spatial, so no new edge);
  - `/code-review high`, recorded below, with extra care for the eager-update paths and concurrent queries;
  - `STATUS.md` and `KNOWN-ISSUES.md` (Windows and macOS runs waiting); commit and push.

## Notes

(written during the day)

## Runs

(written during the day)

## Post-implementation code review

(written during the day)

**Done when:**
- No query loops over every collider.
- Every query equals the oracle bit for bit over the random scenes.
- The physics hashes, the C sample checksum and the script world hashes are unchanged.
- Rays at 5,000 bodies are at least 10 times Day 1's rate, and the character numbers are recorded.
- The scoped runs in three flavors pass apart from known failures, and the review is recorded.
