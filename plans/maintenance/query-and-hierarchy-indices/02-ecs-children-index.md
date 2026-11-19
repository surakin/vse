# Query and hierarchy indices / Day 2: ECS: a children index for SetParent, ClearParent and DestroyRecursive; the series close

**Series:** [the query index and the hierarchy index](../query-and-hierarchy-indices.md)
**Depends on:** Day 1 (only for the series close), Phase 17's hierarchy (`engine/ecs/transform.*`) and its tests.

Each `SetParent`, `ClearParent` and `DestroyRecursive` call collects every `Parent` link in the world (`CollectLinks`). `Descendants` then walks that
list once per level of the subtree, so a call costs O(N × height) and building a hierarchy costs O(N²). This day keeps a children index beside the
hierarchy. The three calls update it themselves. When something else changed `Parent` behind their back, they notice and rebuild the index once, with
one `CollectLinks`. That something else is a raw `Set<Parent>`, a `Remove<Parent>`, an entity's destruction, or the propagation removing a dead
parent's link.

## Tasks

- [x] **Read before writing:**
  - every path that adds, changes or removes a `Parent`:
    - `World::Set`, `Remove` and `Destroy`;
    - the command buffer's playback;
    - archetype moves;
    - the propagation's orphan repair (`transform.cpp`);
    - storage loads (`engine/storage/world_storage.cpp`);
    - the C API's entity calls;
  - how `Changed<Parent>` and the removal logs (`RemovedCount<T>`, `EndFrame`) see those paths, and whether either can say "Parent changed since
    version V" without a scan.
- [x] **The ECS addition** (the smallest one that works; the choice and the reason are recorded here):
  - the expected form is a per-component-type version in `World`, `template <class T> u64 ComponentVersion() const`:
    - bumped by every structural change that adds or removes `T` (add, remove, destroy, archetype move with `T`), and by every write of `T`
      (`Set<T>`, the non-const `Get<T>`, a query's write);
    - a counter, not a scan, and free for a type nobody asks about;
  - `///` with labels, a test per path above that it moves, and `docs/guide/ecs.md`.
- [x] **The children index** (`engine/ecs/transform.cpp`, a world resource private to the hierarchy):
  - a `HashMap<Entity, Vector<Entity>>` from parent to children, in the order the links were made;
  - the `Parent` version it was last made at;
  - `SetParent`, `ClearParent` and `DestroyRecursive`:
    - check the version, and rebuild with one `CollectLinks` when it moved;
    - after a call's own `Set`/`Remove`, update the index and take the new version, so their own writes never force a rebuild;
  - `SubtreeHeight`, `RebaseDepths` and `Descendants` walk the index (breadth-first from the root), not the link list;
  - `DestroyRecursive` keeps its order: deepest first, then, within a depth, today's order. Today's order is `Query<Read<Parent>>` iteration order,
    and the index must reproduce it, or the commands' order changes; record which way it was matched.
- [x] **The oracle:**
  - today's `CollectLinks` + `Descendants` path moves to the tests unchanged;
  - a random test that builds, reparents, clears, destroys (recursively and not), forces a raw `Set<Parent>`, runs the propagation and frames, and
    compares every call's status, every `HierarchyDepth`, every `WorldTransform` and the `DestroyRecursive` command list with the oracle's;
  - the existing `[ecs][transform]` cases pass unchanged.
- [x] **Unchanged results:** the ECS, storage, engine_c, script and renderer scene tests pass, and `Transform: parallel propagation equals the
  sequential result` passes.
- [x] **Numbers** (`ecs_bench` and `container_bench` `SetParent` at 1,000 and 4,000, 3 runs, alternating with the commit before this day):
  - Day 1 7,452 and 29,491 ns per call, after the audit 18,441 at 4,000; target about linear growth;
  - recorded here and in `KNOWN-ISSUES.md`, whose entry (5) is removed.
- [x] **Docs:** `docs/guide/ecs.md` (the hierarchy's cost and the new version call); `transform.h`'s `SetParent` comment ("one pass over every entity
  that has a Parent per level") corrected; `docgen lint` at zero; `docs/reference` regenerated; `ctest -L docs`.
- [x] **The series close:**
  - `scripts/test-phase.sh --affected --flavor all`, covering the phases of both days; quote the line;
  - `/code-review high --max-findings all` over the series' diff (from the commit before Day 1 to the working tree), recorded below;
  - the before/after table of both days in the series index;
  - `KNOWN-ISSUES.md`: the Windows and macOS runs of both days, waiting for the user's machines.
- [x] **Standing rows:**
  - Clang unity, GCC non-unity, both clean; `module_deps_check`;
  - `/code-review high` on the day, recorded below, with extra care for the version counter's paths and the rebuild trigger;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Who changes a `Parent`** (the read-before-writing task):
  - every path goes through `World`: `Set` (in place, or `Add` through a migration), the non-const `Get` (and `GetCell`), `Poke`, `Stamp`,
    the non-const `CellOf`, `Remove`, `Destroy`, `Create`/`CreateMany` with it, and the queries that declare a write, through `BeginIteration(writes)`;
  - command buffers, storage loads (`world_storage.cpp` writes cells through `CellOf`), the C API and the script merge all call those;
  - the propagation's orphan repair removes `Parent` through its command buffer;
  - `ArchetypeAt` hands out archetypes, but every caller outside the ECS only reads through it.
  - `Changed<Parent>` and the removal logs cannot answer "did `Parent` change since V?" without a pass over the chunks, so a counter was added.
- **The ECS addition: `World::TrackComponentVersion<T>()` and `ComponentVersion<T>()`.**
  - It is per world and opt-in: a set of tracked ids, plus a relaxed `Atomic<u64>` per id (128 ids), since `Get` may run from jobs.
  - `Bump(id)` is one bit test for an untracked type. It runs at each path above: on a migration for the components dropped and added, and on
    `Create` and `Destroy` for every component of the archetype.
  - The counter can grow without a change (a query that declares a write and writes nothing), but never stays the same across one.
  - `ecs_bench` shows no cost: iteration at 100,000 entities is 0.278-0.299 ms against 0.275-0.285, and churn 1.56-1.64 ms against 1.51-1.79.
- **`World::StorageOrder(entity)`:** archetype, chunk and row as one key, the order a query visits entities in.
  - The old code destroyed descendants in the link pass's order, and rebased depths in it. The destroy order reaches the entity free list, and
    so which entity ids are made next and the script world hashes; the depth order decides where a migration puts rows.
  - The new code sorts the subtree by this key, taken at the moment the old code collected its links: before the child's own `Set` in
    `SetParent`, and after the removals in `ClearParent`.
- **The index** (`HierarchyIndex`, a world resource private to `transform.cpp`):
  - a map from parent to children, plus the `Parent` version it matches;
  - `SetParent` and `ClearParent` update it after their own writes and take the new version, so their own writes never cause a rebuild;
  - anything else that moved the version makes the next call rebuild it with one query pass (`TransformReport::index_rebuilds`, new);
  - `Descendants` walks it breadth-first with a seen set (a forced cycle ends), and returns at once for a leaf.
- **The oracle** is the old code, copied unchanged into `tests/ecs/hierarchy_index_test.cpp`, running on a twin world beside the new code.
  - Six seeds of 1,500 random steps: `SetParent`, `ClearParent`, `DestroyRecursive` (the buffers applied), plain `Destroy`, raw `Set<Parent>`
    (cycles included), `Remove<Parent>`, writes through `Get`, new entities, and propagation frames.
  - After every step both worlds must agree on each entity's life, `Parent`, `HierarchyDepth`, storage order and world matrix. Every status
    and destroy count must agree, and the next entity made must be the same.
  - **Checked against breakage:** removing the bump from `Get`, and dropping the order sort, each fail the comparison.
  - Further cases: a 2,000-entity tree built with one index build, then reparenting and clearing with no rebuild, and one rebuild after a raw
    write. The version case walks every path above, plus the reads that must not move it.
- **Numbers** (`container_bench --section paths`, Release, 3 runs alternating with the commit before this day):

  | Entities | Before (ns per `SetParent`) | After |
  |---|---|---|
  | 1,000 | 4,870-5,111 | 192-207, then 113 with the review's leaf fast path |
  | 4,000 | 19,079-19,699 | 200-217, then 122 |

  The growth is now flat, about linear for the whole build, against quadratic before. Day 1 of the container audit measured 7,452 and 29,491.

## Runs

- **The series close:** `scripts/test-phase.sh --affected --flavor all`: `ran 10888 of 11400 tests (phases: all; flavors: all; display:
  included; crash tests: excluded) in 1078 s`. The selection is every phase, because `engine/ecs` headers and CMake lines changed.
  - The only failures are known ones: the Job-destroyed test in three flavors, `engine_c_surface` (a window test), the voice-cost snapshot
    (timing, phase 26) and the tsan LuaJIT sandbox flake.
- **After the review's fix:** the whole tree was rebuilt, then:
  - `ecs_tests` in plain, asan and tsan: 129 cases each, all pass;
  - the script, storage and C API scene tests: 357 of 357;
  - the gates: 24 of 24;
  - `ctest -L docs`: 9 of 9.
- **Builds:** Clang unity and GCC non-unity (`build-gcc`), both clean.
- **Not run:** Windows and macOS (recorded in `KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high --max-findings all` over the series' diff, from `7d4e886e` (the plan) to the working tree, covering both days. Day 1 had its own
review, recorded in its file. 4 findings:
- **Fixed:** `Descendants` made a seen set and two vectors even for a leaf, the case every new child of a hierarchy is in. It now returns at once,
  and `SetParent` went from about 200 to 120 ns.
- **Declined, recorded in `KNOWN-ISSUES.md`:**
  - **Removing a child scans its siblings** (`RemoveChild`), so clearing every child of a parent with k children one by one is O(k²). Each call
    was O(N) before, so this is still a gain. A set per parent would cost memory on every parent for a rare pattern.
  - **Writes into an archetype's columns by hand** (`ArchetypeAt`) are not counted by `ComponentVersion`. The doc says so, and every caller of
    `ArchetypeAt` outside the ECS only reads.
- **No change:** `DestroyRecursive` called while a query that writes `Parent` runs still aborts as documented. The query's start moved the
  version, so the index is rebuilt by a nested query, which refuses.

**Done when:**
- `SetParent`, `ClearParent` and `DestroyRecursive` no longer collect every link on a call whose world did not change behind their back.
- The oracle test agrees over random histories, including raw writes and destruction.
- The `SetParent` numbers grow about linearly and are recorded.
- The series' uncapped review and its runs are recorded.
- `KNOWN-ISSUES.md` lists only the Windows and macOS runs left by the series.
