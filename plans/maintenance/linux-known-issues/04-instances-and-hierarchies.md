# Maintenance / linux-known-issues / Day 04 — Instance sets and trees: bookkeeping, costs and the C API's missing data

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Linear lists in the sync systems (`engine/scene/instanced_sync.cpp`, `hierarchy_sync.cpp`, `Hold` and `Drop`): index by the render scene slot or an id map; test: 100,000 instanced entities created in one frame sync in a time bound (a counted-operation test, not wall time).
- [ ] `Detach` and `Attach` (`engine/renderer/instance_tree.cpp`): recompute the old ancestors' subtree bounds before the move, and move the range without rewriting the whole array when only part of it changes; tests: the old root's `subtree_bounds` equal a brute-force union after a `Detach`; the cost counter of a move in a 100,000-node tree is proportional to the moved range.
- [ ] `UpdateWorldTransforms` recomposes the ranges of the marked subtrees, not one span from the first to the last mark of a root; test: two marks far apart in one big tree recompose their own subtrees (counted).
- [ ] The builder's 2^22-node refusal (`Full`) has a test; a node record's size drops (the root origin lives in a side table, not on every node) if that is under 30 lines of change, otherwise the day records the measurement.
- [ ] The instance set's chunk memory: `NewChunk` and `Rebuild` use `TryResize` and return `Full`/`OutOfMemory`; a cap on the chunk count or the bytes a set may hold (a set parameter, default 4 times the instances' chunks); a scatter far apart does not open a 256-slot chunk per instance without limit; test with 65,536 instances a key cell apart.
- [ ] `Rebuild` keeps a record's stored offset exact (derive from the world position kept in `f64`, or round once from the original); `ChooseChunk` weighs distance, not only the key order; tests: 100 rebuilds move no instance by more than one ulp of the first placement; a distant instance joins a chunk whose box does not balloon.
- [ ] `TreeLodState` knows its tree (the tree id and the node count) and refuses another tree's state with a reset and a log; the requirement that a coarser level or stand-in fit the level 0 box is checked by the builder (`LodValid` takes the mesh bounds); frustum-culled nodes drop their state entry; tests.
- [ ] C API: `instance_set_set_transform` keeps the tint and custom value unless the item's flags say to set them, and applies the transform and the data together or neither; `engine_tree_node` carries the mesh slots, the levels and the stand-in (a sized struct extension), and an instanced object can name a material resource instead of the one default; tests through `host_instances.c` and `engine.h`.
- [ ] `instance_set_update` respects the set's size; the 4,096-sets and 1,024-surface limits become documented settings or are raised with a measurement; the scripts learn the id of an instance they add through a completion value delivered the next tick (`instances.add` returns a handle resolved by the merge), or the doc says why not.
- [ ] `Scatter` is tested on a real terrain through `TerrainRayCast` (the demo's way) and the height function takes the terrain's query as a typed callable; the level reference and the world save/load of `scene::InstancedMesh` and the terrain component (Day 498 and Day 493 items): a spike day-part decides the level format change (version, a `INST` and `HGHT` reference component) with the user's confirmation before any format is changed.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** Instance sets and trees keep their invariants under every edit, cost what the measured models say, and the C API carries their LOD and material data; the entries for Days 497 to 501 and the review's instance items are removed (the level format change waits for the user's answer, and its entry stays if the answer is no).

## Post-implementation code review

(To be filled in when the day is done.)
