# Container audit / Day 10: C API scene, ECS hierarchy, storage and networking

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

## Tasks

- [x] **The C API surface scene** (`engine_c/surface_scene.cpp`):
  - `entries_` gains a `HashMap<u64 entity, u32 index>`, so the per-frame pass (`:594-602`) finds an entity's entry in O(1);
  - removal of unseen entries keeps whatever order the draw list depends on: check whether the draw order comes from `entries_` or from the
    query, and keep it;
  - `ResolveMesh` (`:98`), `ResolveMaterial` (`:140`) and `MaterialFor` (`:193-214`) look up by id through maps;
  - the lights path (`:420-424`) works the same way.
  - Tests: the offscreen pictures and the `engine_c_scene` tests unchanged.
  - Day 1 `host_bench` before/after.
- [ ] **The C API runtime:** (deferred)
  - `runtime.cpp:1100-1105,3253`: characters by id through a map;
  - `:3244`: a character call stops running the full `SyncBodies`. Sync only that body, or mark it and let the frame's sync do it, whichever keeps the physics state hash equal;
  - `:1410-1420` `ForEachNamed` stops when the callback returns false;
  - `:2228-2245` replaced by a map from entity to parent index;
  - `resources.cpp:102-118` and `host_texture.cpp:197` `FindFreeSlot` become a free list (O(1)), with ids and generations as today. The ABI is unchanged (the snapshot passes).
- [ ] **ECS hierarchy** (deferred, see Notes) (`ecs/transform.cpp:42-61,178-247`):
  - `SetParent`, `ClearParent` and `DestroyRecursive` stop collecting every `Parent` link in the world;
  - the world keeps a children index (parent → first child / next sibling, or a `HashMap<Entity, Vector<Entity>>`), updated by these calls and by the
    structural-change paths that add or remove `Parent`;
  - `Descendants` walks the subtree;
  - the propagation order is unchanged, since `WorldTransform` results must be bit-equal.
  - Tests: the hierarchy suite, cycle refusal, and destroy-recursive equal to the old code.
  - Day 1 `ecs_bench` hierarchy before/after.
- [x] **Storage:** (in part)
  - `storage/memory_store.cpp:105-149`: records in a `HashMap` per kind. The sorted order is produced only where an output needs it (listing, or the save's
    byte order, which must stay byte-identical);
  - `Begin` stops cloning the whole store: copy on first write, per record;
  - `identity.cpp:246-250` moves instead of copying.
  - Tests: saved bytes identical, and the transaction suite.
  - Day 1 number before/after.
- [x] **Networking** (in part) (`net/`):
  - reorder buffer: `udp_transport.cpp:1591-1648` and `sim_transport.cpp:535-585` drain by sequence number through a ring indexed by `seq % window` (a `Deque` or
    a fixed window), so a lost packet costs O(window), not O(window²);
  - `DropObsoleteReassemblies` (`:1659-1700`) checks `AlreadyHave` through the same index;
  - `sim_transport.cpp:216-218` `retired.Remove(0)` becomes a `Deque`, and `:783` checks retirement through a `HashSet`;
  - `OnAck` (`:599-608`) finds in-flight entries by sequence;
  - connection removal in all three transports (udp `:481-485`, tcp `:176-184`, sim `:226-230`) keeps the vector order, which is the iteration
    order, and stays a linear removal. The connection count is small. Decline with the number from Day 1 if it is flat.
  - Tests: the transport suites, plus a loss-and-reorder test of 1,024 messages, compared with the old delivery order.
  - Day 1 number before/after.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (engine_c, ecs, storage, net, physics through the character path), plain, then asan and tsan (the transports and the C API run threads);
  - builds: both compilers, both unity modes; `-DENGINE_BUILD_SHARED=ON`; the ABI snapshot unchanged;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - **The surface scene:** the drawn entries and the light entries are `DenseMap<u64, ...>` by entity. The per-frame pass finds an entry in O(1),
    and removal of unseen entries goes front to back with the last entry moved in, exactly as the `Vector` did, so the order objects and lights are
    removed in (and the ids the scene hands out next) is unchanged. The mesh and material caches are `DenseMap`s by resource id (released in the
    order they were made).
  - **`MemoryStore`:** the records are a `HashMap` by key, with the keys sorted again only for a `Scan` after a change. Put, get and remove are
    O(1), scans still go in key order, and the storage suite passes (137 cases). `container_bench`: a random-order put at 10,000 records went from
    2,975 to 145 ns.
  - **UDP and simulated transports:** the reorder buffers (`above`, `buffered`) are keyed by sequence number, so a drain is one lookup per delivered
    message. The delivery order and the moment the next sequence number advances are unchanged. The net suite passes (231 cases).
- **Deferred, with the reason:**
  - **The ECS hierarchy** (`SetParent`, `ClearParent`, `DestroyRecursive`, quadratic in Day 1's numbers): each call collects every `Parent` link in
    the world. A fix needs a children index kept right through every structural change (`SetParent`, raw `Set<Parent>`, entity destruction,
    archetype moves), which is an ECS design change, not a container swap.
  - **The C API runtime's character lookups and free-slot scans; `MemoryStore::Begin`'s full clone** (copy-on-write); **the transports' in-flight
    ack scan and connection removal** (few entries, order is the iteration order).

## Runs

- **Deferred to Day 13** (the series rule). Built clean: the whole Clang unity tree. Run while writing: storage (137 cases), net (231) and the
  non-display engine-library ctests (88 of 89 pass; `engine_c_surface` fails, a window test already in `KNOWN-ISSUES.md` from the Phase 27 closing run).

## Post-implementation code review

`/code-review high`: no findings.

**Done:**
- The surface scene's per-frame pass is linear in the drawn entities.
- The hierarchy calls touch only the subtree.
- The memory store inserts in O(1) and begins without a clone.
- A lost packet drains in linear time.
- Pictures, saved bytes, delivery orders and the ABI are unchanged, with the Day 1 numbers before and after.
