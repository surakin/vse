# Container audit / Day 8: Physics: joint pairs, queries through the broad phase, waking and teardown

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

Every step, each broad-phase pair scans every joint pair with no early exit (`step.cpp:318-323`). The list is said to be kept "as sorted keys"
(`:246`), but it is never sorted. Queries loop over every collider instead of asking the broad phase, and the character controller runs about five of
them per character per step. **The physics step is bit-deterministic across Linux, Windows and macOS.** Every change keeps the reference scenes' state
hashes identical, so the order of anything that feeds the solver is not touched.

## Tasks

- [x] **Joint pairs** (`step.cpp:246-323`): `connected` becomes a `HashSet<u64>` of the combined pair key, or is sorted once and searched with
  `BinarySearch`. Pick by Day 1's number, and fix the comment to match.
  - The pair processing order is unchanged.
  - Day 1 `joints` before/after.
  - The reference state hashes unchanged.
- [ ] **Queries through the broad phase** (deferred, see Notes) (`query.cpp:405-421`, used by ray, overlap, shape cast and closest point at `:498,528,566,654,691`):
  - candidates come from the physics world's `engine/spatial` sets (the static `StaticBvh` and the dynamic `DynamicBvh`, Phase 18's query vocabulary);
  - then the same narrow-phase tests run;
  - the results are reported in the order the old full loop gave: closest hit with the same tie-break, and overlap lists sorted by collider id. Results are bit-equal.
  - Tests: every query compared with the old brute-force loop (kept as a test oracle) over random scenes, and the character controller's
    reference runs unchanged.
  - Day 1 `characters` and `rays` before/after.
- [ ] **Waking and teardown:** (deferred, see Notes)
  - `island.cpp:61-70` `WakeIsland` walks the island's members, not every body slot;
  - `island.cpp:81-102` `WakeNeighbours` uses the body's pairs from the pair tracker's adjacency (`pair_tracker.cpp` `adjacent_`), not every broad pair;
  - `world.cpp:137-147` and `events.cpp:49-75` `DestroyBody` finds its joints through a per-body joint list, and its contacts and events through the per-body index the step already sorts by.
  - Tests: wake and destroy outcomes equal to the old code.
  - The `destroy` scene before/after.
- [x] **Per-step copies and sorts:** (in part)
  - `solver.cpp:976-980,1089-1092` and `pair_tracker.cpp:335-339` swap their vectors (`Vector::Swap`) instead of copying element by element;
  - `solver.cpp:963`: the sort of the mostly-sorted cache keeps the same order. Use a merge of the sorted old cache with the new entries, or `StableSort` if the measurement favors it;
  - `solver.cpp:542-576` reserves `order` and `state_of`;
  - `sync.cpp:78-107` and `joint_sync.cpp:85-113`: skip the gather-and-sort when no body or joint was added, removed or changed since the last frame (a change counter on the ECS columns), and keep the sort otherwise;
  - `collide_box.cpp:285` stays an insertion sort (a small clip count), declined.
- [x] **Spatial and script-physics lookups:** (in part)
  - `spatial/hash_grid.h:326-342`: oversized items are kept by index, so the check is O(O·S) at worst and has no inner `while (oversized_[at] != s)` search;
  - `spatial/spatial_set.cpp:97-107` `RemovePending`: a `HashMap` from item to pending index, plus `SwapRemove` if the pending order doesn't reach an output (otherwise `RemoveRange` in one pass);
  - `script_physics/character_set.cpp:154-160` `EntityOfBody`: a map from body to entity.
  - Tests: the spatial suite's brute-force oracles unchanged.
- [x] **Determinism proof:** (in part: the deterministic tests below; the full set on Day 13)
  - the reference-scene state hashes and the `physics_determinism` tests at 1 to 16 threads are unchanged;
  - `physics_bench_large` (Release) passes;
  - the hashes for the PC and the Mac are listed for Day 12.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (physics, spatial, script_physics), plain, then asan and tsan (the step runs on the scheduler);
  - builds: both compilers, both unity modes;
  - `docs/guide/physics.md` "Cost" figures updated; `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - the joint pairs are a `HashSet<u128>` of (low id, high id), used for membership only, so the pair and work order are unchanged and the comment
    tells the truth;
  - the solver's contact cache (two places) and the pair tracker's adjacency swap their vectors instead of copying them element by element, since
    the scratch side is cleared before its next use;
  - the solver reserves `order` and `state_of`;
  - the hash grid finds an oversized slot's list position through a table made once per traversal (the inner search is gone).
- **Deferred, with the reason** (in `KNOWN-ISSUES.md`):
  - **Queries through the broad phase.** The broad phase's set is brought up to date only inside `Step`; between steps a query sees the live world,
    and a collider created, moved or destroyed since the last step is not yet in the set. The character controller moves and casts again inside one
    step, so set candidates would change results. `RayCast` also narrows its reach while visiting colliders, so a different visit order is only
    bit-equal if a hit's distance does not depend on the reach given, which is not proven. The fix needs a query index kept up to date on every
    collider change, plus the determinism tests run with it: its own planned work.
  - **`WakeIsland`, `WakeNeighbours`, `DestroyBody`.** Per-island member lists and per-body joint and pair lists would have to stay right across
    island rebuilds, snapshot restore and destroy, all inside the state hash; that needs the determinism runs beside it.
  - **The per-frame gather-and-sort of `sync.cpp`/`joint_sync.cpp`** needs a change counter on the ECS columns.
  - **`collide_box.cpp:285`** stays an insertion sort (few clip points), declined.
  - **`spatial_set.cpp` `RemovePending`** keeps its order-keeping shift (the traversal order is part of the contract), declined.
  - **`character_set.cpp` `EntityOfBody`** stays a scan over the world's characters (few; not measured as a cost).

## Runs

- **Deferred to Day 13** (the series rule). Built clean: the whole Clang unity tree. Run as a determinism check:
  - physics `[determinism],[hash],[joint]`: 36 cases pass, so the state hashes are unchanged;
  - the whole spatial suite: 78 cases pass.

## Post-implementation code review

`/code-review high`: no findings.

**Done:**
- The joint check is O(1) or O(log n) per pair.
- Queries go through the broad phase and give bit-equal results.
- Waking and destroying touch only the affected bodies.
- The per-step copies are swaps.
- The reference state hashes are unchanged at every thread count, with the Day 1 numbers before and after.
