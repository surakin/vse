# Query and hierarchy indices / Day 1: Physics: a query index kept current, queries through it

**Series:** [the query index and the hierarchy index](../query-and-hierarchy-indices.md)
**Depends on:** the container audit (Day 8's notes on why the broad phase's set was not used), Phase 18's `SpatialSet`, Phase 25's queries and
determinism tests.

The queries test every collider. This day gives the world a second spatial set, the query index, that matches the world at every moment a query can run.
A query then asks it for candidates, sorts them by collider slot and runs today's per-collider code on them unchanged. A collider that is not a candidate
fails today's box test anyway, and the candidates are visited in today's order with today's reach. The result is therefore bit-identical by construction,
and no proof about the reach is needed.

## Tasks

- [x] **Read before writing:**
  - every writer of a body's pose or a collider's shape. Known so far:
    - `CreateBody`, `DestroyBody`, `AddCollider`, `SetCollider`, `SetTransform` (`world.cpp`);
    - `IntegratePosition` (`step.cpp`, `solver.cpp`), the CCD rewind (`ccd.cpp`), the island sleep and wake paths (`island.cpp`);
    - `Restore` (`snapshot.cpp`).
  - Also every caller outside `engine/physics/` that writes through the non-const `PhysicsWorld::Find` or `FindCollider` (grep
    `engine/`, `game/`, `tests/`). Every pose or shape write outside the world goes through `SetTransform` or `SetCollider`; record the list here.
  - when in `Step` the pre-step hook and the character controller's queries run, and what they must see.
- [x] **The query index** (`engine/physics/query_index.h/.cpp`, private to the module; the world owns one):
  - a `spatial::SpatialSet` keyed by `ColliderId::ToKey()`: static colliders in its static tree, the others in its dynamic tree with fat boxes (the
    broad phase's margins);
  - every box it holds contains the collider's exact current bounds (`Shape::Bounds` at `PoseOf`), so a candidate list is always a superset;
  - updated eagerly by the world's mutating calls, never lazily inside a query: queries are `const` and run concurrently from jobs
    (`Query: queries run from jobs ...`), so the index is not changed while they read it;
  - after a step moves bodies: one update of the awake moving bodies' colliders, so a box is re-inserted only when its exact bounds leave the fat box;
  - `Restore` rebuilds it whole;
  - `Stats()` (reinsertions, size), for the tests.
- [x] **Queries through it** (`query.cpp`):
  - `ForEachCandidate(world, region or ray, filter, f)` collects candidate slots from the index, sorts them by slot and calls today's per-collider
    lambda on each. The lambda bodies of `RayCast`, `RayCastAll`, `ShapeCast`, `Overlap` and `ClosestPoint` do not change;
  - rays use `TraverseRay` with the full `max_distance` (the shrinking reach stays in the lambda, as today);
  - shape casts use the swept box, overlaps the shape's box, and `ClosestPoint` the point grown by `max_distance`;
  - the candidate list is scratch the caller owns: one `Vector` per call on the stack, no shared mutable state (thread-safe as today).
- [x] **The oracle:**
  - today's `ForEachCollider` loop moves to the tests (`tests/physics/query_oracle.h`) unchanged;
  - a test over random scenes (every shape kind, static, dynamic, kinematic, sleeping, triggers, filters and layers), with bodies created, moved,
    teleported, destroyed and stepped between queries, compares every query's full result with the oracle's bit for bit;
  - a query from the pre-step hook and the character controller's casts inside a step agree with the oracle;
  - a query after `Restore` agrees.
- [x] **Unchanged results:**
  - `golden_test`, `determinism_test`, `snapshot_test`, `character_test`, `ccd_test` and the script physics tests pass with unchanged hashes;
  - the C physics sample prints checksum `532bfea0`;
  - the script world hashes are unchanged at 1 to 16 threads.
- [x] **Numbers** (`physics_bench --scene rays` and `--scene characters`, 3 runs, alternating with a build of the commit before this day):
  - rays at 5,000 bodies: Day 1 8,699 per second, target at least 10 times that;
  - characters at 100, 500 and 2,000: Day 1 1.38, 20.4 and 288 ms per step, target about linear growth;
  - `Step` alone at the `drop` and `large` scenes, which must not get slower by more than the noise (the index update is new work per step);
  - recorded in this file and in `KNOWN-ISSUES.md`'s entry (3), which loses its query part.
- [x] **Docs:**
  - `docs/guide/physics.md`: the queries' cost (candidates from an index; the cost is the colliders near the query) and when the index is updated;
  - `///` on any public header that changes; `docgen lint` at zero; `docs/reference` regenerated.
- [x] **Standing rows:**
  - `scripts/test-phase.sh --affected` (physics, script_physics, engine_c), then `--flavor asan` and `--flavor tsan`; quote each `ran X of Y` line here
    and in `STATUS.md`;
  - Clang unity, GCC non-unity, both clean; `module_deps_check` (physics already depends on spatial, so no new edge);
  - `/code-review high`, recorded below, with extra care for the eager-update paths and concurrent queries;
  - `STATUS.md` and `KNOWN-ISSUES.md` (Windows and macOS runs waiting); commit and push.

## Notes

- **Who writes a pose or a shape** (the read-before-writing task):
  - in the world: `CreateBody` (no collider yet), `DestroyBody`, `AddCollider`, `SetCollider`, `SetSettings` (the class), `SetTransform`;
  - inside `Step`: `IntegratePosition` (step and solver), the CCD rewind (`SweepContinuous`), and the sleep paths (`put_to_sleep` writes only
    the previous pose);
  - `Restore` builds a new world and moves it in.
  - Outside `engine/physics/`, nothing writes a pose or a shape through the mutable `Find`/`FindCollider`: the script bindings add torque,
    `sync.cpp` calls `SetSettings`/`SetCollider`, and the C API and the character controller only read. `Find` and `FindCollider` now say that a
    pose or shape changes only through those calls.
  - The pre-step hook runs at the start of `Step`, before anything moves. The character controller casts from `Move`, outside `Step`, and moves
    its body by velocity, so the step's motion is what the index must follow.
- **The index** (`engine/physics/query_index.h/.cpp`, excluded from the reference as internal):
  - a `spatial::SpatialSet` keyed by collider slot. Static colliders go in its static tree. A list of waiting static colliders is rebuilt into the
    tree when it reaches a quarter of the static colliders, and at a step's end. The other colliders go in its dynamic tree;
  - the dynamic side's margins are wider than the broad phase's (fat 0.5 of the largest extent, predictive 4 times the last move). The broad
    phase's margins (0.25, 2) cost 0.9 ms per step at 5,000 falling bodies in reinsertions; these cost 0.56 ms;
  - the index holds each collider's exact box, made by the same `ColliderPose` the queries use (`PoseOf` now calls it), so a stored box equals the
    box a query computes, bit for bit;
  - the world refreshes it in the six calls above, at the end of `Step` (`RefreshMoving`), and with a rebuild after `Restore`. `RefreshMoving`
    skips a body that slept at its last refresh and still sleeps (a step does not move it, and every change from outside wakes it);
  - a collider whose box the set refuses (non-finite or empty), or one more than the set holds, makes every query fall back to the full scan.
- **Why the results are bit-identical** (the plan's worry about `RayCast`'s shrinking reach):
  - each query's candidate test is its own box test (`BoxOverlapsRay` along the full `max_distance` for rays, the swept box for `ShapeCast`,
    the shape's box for `Overlap`);
  - IEEE rounding is monotone, so the test accepts every box that contains a box it accepts, including the tree's node boxes and fat boxes;
  - no collider the old loop would test is left out, the candidates are visited in slot order, and the per-collider code is unchanged.
  - The plan had said rays would use `TraverseRay`. They use `Traverse` with `BoxOverlapsRay` instead, because the spatial module's ray test is a
    different slab code and is not proven to accept a superset at the edges.
  - **`ClosestPoint`** had no box test (the narrow phase measures the gap). Its candidates are the colliders whose box is within twice the
    distance plus the probe, so a collider is left out only if the narrow phase underestimated a gap by more than the distance itself. The tests
    found no difference.
- **The oracle is the old loop, kept in the engine:** `ForEachCollider` stays, as the fallback, and `PhysicsWorld::SetQueryIndexEnabled(false)`
  (labeled `Internal:`) makes every query use it. That replaces the planned copy in `tests/physics/query_oracle.h`, since the same code is then
  both the reference and the fallback.
  - In builds with slow checks, a query first checks that the index holds every collider's current box, and aborts otherwise.
- **Tests** (`tests/physics/query_index_test.cpp`, 4 cases):
  - **Random changing scenes:** six scenes of 20 to 170 bodies, with spheres, boxes, capsules, hulls, compounds and static meshes; static,
    dynamic and kinematic bodies; triggers, layers and filters. Bodies are created, destroyed, teleported, reshaped, changed in class and stepped
    between queries. 3,900 queries of the five kinds give equal results both ways, and after every change the index holds every collider's exact
    box;
  - **Restore and the pre-step hook:** the same comparison after a `Restore`, and 200 queries from the pre-step hook;
  - **Resting boxes:** they cause at most 5 reinsertions over 60 steps once settled;
  - **A sleeping pile woken by a heavy ball:** compared after every step for 400 steps.
  - **Checked against breakage:** with the step-end refresh removed, the random scenes give 18 mismatches and the hook case 1. With the sleep skip
    loosened to skip every sleeping body, the index lags 244 times (a body that falls asleep has almost stopped, so its results seldom differ,
    which is why the test checks the index's boxes directly).
- **Numbers** (Release, one thread for `drop`; the old build is the commit before this day, built in a worktree; runs alternated):

  | Scene | Before | After |
  |---|---|---|
  | rays, 1e6 against 5,000 bodies | 8,637 per second | 834,000 to 863,000 per second (98 times) |
  | characters 100 / 500 / 2,000 | 1.38 / 20.6 / 294 ms per step | 0.45-0.49 / 2.3-2.5 / 9.8-10.0 ms per step |
  | drop 5,000, falling | 17.5-17.7 ms per step | 18.0-18.6 ms (+0.53 to 0.56 ms at the step's end, the index) |
  | drop 5,000, at rest (88 asleep) | 32.9-33.5 ms | 33.1-33.6 ms (+0.3 ms) |
  | large 50,000 (32 threads) | 712 ms per step (after 37.4) | 712 ms (after 41.3) |

  - Rays at 5,000 bodies are 98 times Day 1's rate (the target was 10 times).
  - Characters now grow about linearly (500 to 2,000: 4 times the characters, 4.1 times the time), against 14 times before.
  - **The step pays for the index:** about 3% for 5,000 falling bodies on one thread, and under 1% at rest or at 50,000 bodies. That is more than
    the noise the plan allowed.
  - Declined: a lazy refresh (only after a step in which someone queried) would make a query-free world pay nothing, but a `const` query cannot
    catch up, so a world queried without stepping, the rays bench's pattern, would scan. Recorded in `KNOWN-ISSUES.md`.

## Runs

- **Plain:** `scripts/test-phase.sh --affected`: `ran 4236 of 11391 tests (phases: all; flavors: plain; display: included; crash tests: excluded)
  in 452 s`. The selection is every phase, because `engine/physics/CMakeLists.txt` had a line edited.
  - Failed, then passed on rerun: `script_bench_check` (a timing bound under load, as on the Phase 27 close) and `LastSaveStatus can be read while
    another thread saves` (phase 11, seen once before on Phase 20 Day 266).
  - Known: `engine_c_surface` (a window test) and the Job-destroyed test.
  - `docs_check`, `docs_lint` and the baseline test failed until `query_index.h` was excluded from the reference and the docs were regenerated.
- **asan:** `ran 3494 of 11391 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 70 s`.
  - Known flakes, both passing alone right after: `engine_c_scene_asan` and `Spike 1: a renderer on a window's device ... [asan]`.
  - Known: the Job-destroyed test.
- **tsan:** `ran 3149 of 11391 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 145 s`. Known: the LuaJIT sandbox
  flake and the Job-destroyed test.
- **The final code:** the review's refactor landed while the sanitizer runs were building, so `physics_tests` was run again on it in all three
  flavors: 229 cases each, all pass, golden and determinism hashes included.
- **The C sample:** `engine_c_physics_sample`, which checks checksum `532bfea0`, passes in the three flavors.
- **Builds and docs:** Clang unity (`build`, Release) and GCC non-unity (`build-gcc`) are both clean, and `ctest -L docs` passes 9 of 9.
- **Not run:** Windows and macOS (recorded in `KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high` on the day's diff:
- **Fixed:** the step-end refresh looked each collider and its body up twice, once for the sleep check and again inside `Refresh`. It now passes
  them to `Update`.
- **Checked, no change:**
  - the superset argument holds for the static tree's node boxes (unions of exact boxes), the waiting static list (item boxes), stale tree
    entries (skipped by the set) and the dynamic tree's fat leaves;
  - `Collect` and `Holds` are `const`, and the set's `const` traversals are safe on many threads between changes, as before;
  - `Restore`'s error paths leave the old world and its index untouched;
  - a moved-from world has no index, and its queries scan an empty world.
- **Declined:**
  - the `Vector` of candidates allocates once per query that has candidates. Rays are 98 times faster with it, and a fixed buffer would need a
    size limit and a fallback.
  - `ClosestPoint`'s candidate region is a margin argument, not a proof (see Notes). The oracle found no difference.

**Done when:**
- No query loops over every collider.
- Every query equals the oracle bit for bit over the random scenes.
- The physics hashes, the C sample checksum and the script world hashes are unchanged.
- Rays at 5,000 bodies are at least 10 times Day 1's rate, and the character numbers are recorded.
- The scoped runs in three flavors pass apart from known failures, and the review is recorded.
