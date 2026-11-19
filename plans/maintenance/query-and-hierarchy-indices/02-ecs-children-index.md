# Query and hierarchy indices / Day 2: ECS: a children index for SetParent, ClearParent and DestroyRecursive; the series close

**Series:** [the query index and the hierarchy index](../query-and-hierarchy-indices.md)
**Depends on:** Day 1 (only for the series close), Phase 17's hierarchy (`engine/ecs/transform.*`) and its tests.

Each `SetParent`, `ClearParent` and `DestroyRecursive` call collects every `Parent` link in the world (`CollectLinks`). `Descendants` then walks that
list once per level of the subtree, so a call costs O(N × height) and building a hierarchy costs O(N²). This day keeps a children index beside the
hierarchy. The three calls update it themselves. When something else changed `Parent` behind their back, they notice and rebuild the index once, with
one `CollectLinks`. That something else is a raw `Set<Parent>`, a `Remove<Parent>`, an entity's destruction, or the propagation removing a dead
parent's link.

## Tasks

- [ ] **Read before writing:**
  - every path that adds, changes or removes a `Parent`:
    - `World::Set`, `Remove` and `Destroy`;
    - the command buffer's playback;
    - archetype moves;
    - the propagation's orphan repair (`transform.cpp`);
    - storage loads (`engine/storage/world_storage.cpp`);
    - the C API's entity calls;
  - how `Changed<Parent>` and the removal logs (`RemovedCount<T>`, `EndFrame`) see those paths, and whether either can say "Parent changed since
    version V" without a scan.
- [ ] **The ECS addition** (the smallest one that works; the choice and the reason are recorded here):
  - the expected form is a per-component-type version in `World`, `template <class T> u64 ComponentVersion() const`:
    - bumped by every structural change that adds or removes `T` (add, remove, destroy, archetype move with `T`), and by every write of `T`
      (`Set<T>`, the non-const `Get<T>`, a query's write);
    - a counter, not a scan, and free for a type nobody asks about;
  - `///` with labels, a test per path above that it moves, and `docs/guide/ecs.md`.
- [ ] **The children index** (`engine/ecs/transform.cpp`, a world resource private to the hierarchy):
  - a `HashMap<Entity, Vector<Entity>>` from parent to children, in the order the links were made;
  - the `Parent` version it was last made at;
  - `SetParent`, `ClearParent` and `DestroyRecursive`:
    - check the version, and rebuild with one `CollectLinks` when it moved;
    - after a call's own `Set`/`Remove`, update the index and take the new version, so their own writes never force a rebuild;
  - `SubtreeHeight`, `RebaseDepths` and `Descendants` walk the index (breadth-first from the root), not the link list;
  - `DestroyRecursive` keeps its order: deepest first, then, within a depth, today's order. Today's order is `Query<Read<Parent>>` iteration order,
    and the index must reproduce it, or the commands' order changes; record which way it was matched.
- [ ] **The oracle:**
  - today's `CollectLinks` + `Descendants` path moves to the tests unchanged;
  - a random test that builds, reparents, clears, destroys (recursively and not), forces a raw `Set<Parent>`, runs the propagation and frames, and
    compares every call's status, every `HierarchyDepth`, every `WorldTransform` and the `DestroyRecursive` command list with the oracle's;
  - the existing `[ecs][transform]` cases pass unchanged.
- [ ] **Unchanged results:** the ECS, storage, engine_c, script and renderer scene tests pass, and `Transform: parallel propagation equals the
  sequential result` passes.
- [ ] **Numbers** (`ecs_bench` and `container_bench` `SetParent` at 1,000 and 4,000, 3 runs, alternating with the commit before this day):
  - Day 1 7,452 and 29,491 ns per call, after the audit 18,441 at 4,000; target about linear growth;
  - recorded here and in `KNOWN-ISSUES.md`, whose entry (5) is removed.
- [ ] **Docs:** `docs/guide/ecs.md` (the hierarchy's cost and the new version call); `transform.h`'s `SetParent` comment ("one pass over every entity
  that has a Parent per level") corrected; `docgen lint` at zero; `docs/reference` regenerated; `ctest -L docs`.
- [ ] **The series close:**
  - `scripts/test-phase.sh --affected --flavor all`, covering the phases of both days; quote the line;
  - `/code-review high --max-findings all` over the series' diff (from the commit before Day 1 to the working tree), recorded below;
  - the before/after table of both days in the series index;
  - `KNOWN-ISSUES.md`: the Windows and macOS runs of both days, waiting for the user's machines.
- [ ] **Standing rows:**
  - Clang unity, GCC non-unity, both clean; `module_deps_check`;
  - `/code-review high` on the day, recorded below, with extra care for the version counter's paths and the rebuild trigger;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

(written during the day)

## Runs

(written during the day)

## Post-implementation code review

(written during the day)

**Done when:**
- `SetParent`, `ClearParent` and `DestroyRecursive` no longer collect every link on a call whose world did not change behind their back.
- The oracle test agrees over random histories, including raw writes and destruction.
- The `SetParent` numbers grow about linearly and are recorded.
- The series' uncapped review and its runs are recorded.
- `KNOWN-ISSUES.md` lists only the Windows and macOS runs left by the series.
