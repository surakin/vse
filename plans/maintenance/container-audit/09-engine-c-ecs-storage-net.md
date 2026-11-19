# Container audit / Day 9: C API scene, ECS hierarchy, storage and networking

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

## Tasks

- [ ] **The C API surface scene** (`engine_c/surface_scene.cpp`):
  - `entries_` gains a `HashMap<u64 entity, u32 index>`, so the per-frame pass (`:594-602`) finds an entity's entry in O(1);
  - removal of unseen entries keeps whatever order the draw list depends on: check whether the draw order comes from `entries_` or from the
    query, and keep it;
  - `ResolveMesh` (`:98`), `ResolveMaterial` (`:140`) and `MaterialFor` (`:193-214`) look up by id through maps;
  - the lights path (`:420-424`) works the same way.
  - Tests: the offscreen pictures and the `engine_c_scene` tests unchanged.
  - Day 1 `host_bench` before/after.
- [ ] **The C API runtime:**
  - `runtime.cpp:1100-1105,3253`: characters by id through a map;
  - `:3244`: a character call stops running the full `SyncBodies`. Sync only that body, or mark it and let the frame's sync do it, whichever keeps the physics state hash equal;
  - `:1410-1420` `ForEachNamed` stops when the callback returns false;
  - `:2228-2245` replaced by a map from entity to parent index;
  - `resources.cpp:102-118` and `host_texture.cpp:197` `FindFreeSlot` become a free list (O(1)), with ids and generations as today. The ABI is unchanged (the snapshot passes).
- [ ] **ECS hierarchy** (`ecs/transform.cpp:42-61,178-247`):
  - `SetParent`, `ClearParent` and `DestroyRecursive` stop collecting every `Parent` link in the world;
  - the world keeps a children index (parent → first child / next sibling, or a `HashMap<Entity, Vector<Entity>>`), updated by these calls and by the
    structural-change paths that add or remove `Parent`;
  - `Descendants` walks the subtree;
  - the propagation order is unchanged, since `WorldTransform` results must be bit-equal.
  - Tests: the hierarchy suite, cycle refusal, and destroy-recursive equal to the old code.
  - Day 1 `ecs_bench` hierarchy before/after.
- [ ] **Storage:**
  - `storage/memory_store.cpp:105-149`: records in a `HashMap` per kind. The sorted order is produced only where an output needs it (listing, or the save's
    byte order, which must stay byte-identical);
  - `Begin` stops cloning the whole store: copy on first write, per record;
  - `identity.cpp:246-250` moves instead of copying.
  - Tests: saved bytes identical, and the transaction suite.
  - Day 1 number before/after.
- [ ] **Networking** (`net/`):
  - reorder buffer: `udp_transport.cpp:1591-1648` and `sim_transport.cpp:535-585` drain by sequence number through a ring indexed by `seq % window` (a `Deque` or
    a fixed window), so a lost packet costs O(window), not O(window²);
  - `DropObsoleteReassemblies` (`:1659-1700`) checks `AlreadyHave` through the same index;
  - `sim_transport.cpp:216-218` `retired.Remove(0)` becomes a `Deque`, and `:783` checks retirement through a `HashSet`;
  - `OnAck` (`:599-608`) finds in-flight entries by sequence;
  - connection removal in all three transports (udp `:481-485`, tcp `:176-184`, sim `:226-230`) keeps the vector order, which is the iteration
    order, and stays a linear removal. The connection count is small. Decline with the number from Day 1 if it is flat.
  - Tests: the transport suites, plus a loss-and-reorder test of 1,024 messages, compared with the old delivery order.
  - Day 1 number before/after.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (engine_c, ecs, storage, net, physics through the character path), plain, then asan and tsan (the transports and the C API run threads);
  - builds: both compilers, both unity modes; `-DENGINE_BUILD_SHARED=ON`; the ABI snapshot unchanged;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:**
- The surface scene's per-frame pass is linear in the drawn entities.
- The hierarchy calls touch only the subtree.
- The memory store inserts in O(1) and begins without a clone.
- A lost packet drains in linear time.
- Pictures, saved bytes, delivery orders and the ABI are unchanged, with the Day 1 numbers before and after.
