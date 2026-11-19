# Container audit / Day 7: DenseMap, real key types, and the indexed-vector sites moved onto them

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-6. Inserted on 2026-11-19 at the user's request; the days after it moved up by one.

Days 4-6 wrote one pattern five times by hand. Entries sit in a `Vector`, a `HashMap` maps a key to an entry's position, and each entry carries its own key
so that a swap-remove can rewrite the moved entry's position. Every site has its own fix-up code (`ResourceTable::RelinkId`,
`DeviceResources::RemoveAt`, `SceneFrame::DropUnusedLabels`, the theme eviction, the pipeline index), and two caches key by a 64-bit hash with a
collision fallback because their key structs have no `Hash`. This day puts the pattern in one container, gives the keys real types, and moves each site
to the simplest container that fits.

## Tasks

- [ ] **`DenseMap<K, V, Allocator>`** in `containers/dense_map.h`:
  - the values are dense in slots, and a `HashMap<K, u32>` maps a key to its slot;
  - `Find`/`Contains` (heterogeneous too), `Emplace` (returns value, slot and whether it was made), `Remove`, `RemoveAt(slot)`, `RemoveIf(pred)`
    (one pass), `At(slot)`, `KeyAt(slot)`, `SlotOf`, `Size`, `Clear`, `Reserve`, and range-for over `{key, value}` in slot order;
  - removal moves the last entry into the hole and reports the slot it came from, so a caller holding slots can follow it;
  - slots are stable until a removal, and slot order is deterministic for a given sequence of calls;
  - tests against a `HashMap` oracle under churn: slot stability, `RemoveIf`, move-only values, range-for order, the moved-slot report.
- [ ] **Real keys:**
  - `PipelineKey` with `operator==` and `Hash<PipelineKey>` (`HashCombine`) replaces the material cache's `key_hash`, its collision scan and the
    re-indexing in `DestroyPipelines`;
  - the scene label cache is keyed by (pixel size, text), searched without making a string.
- [ ] **Sites moved:**
  - the material pipeline cache keeps its slots and eviction, and is indexed by `PipelineKey`;
  - the theme cache becomes a `DenseMap<u128, CacheEntry>`, evicting with `RemoveAt` round-robin;
  - `ResourceTable` becomes a `DenseMap<String, Entry>` by path, keeping the id chains relinked through the moved-slot report;
  - `DeviceResources` becomes a plain `HashMap<Recreatable*, Entry>`;
  - the scene label cache becomes a plain `HashMap` with a collect-then-remove sweep.
- [ ] **Docs:** a `DenseMap` row and section in `docs/guide/foundation-containers.md` (use it when you iterate in a stable order or hold slots, and
  `HashMap` otherwise); `///` with labels; `docs/reference` regenerated.
- [ ] **Standing rows:**
  - build clean (Clang unity); tests deferred to Day 13 (the series rule), but the new and touched tests are run while writing;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:**
- `DenseMap` exists and is tested against an oracle.
- No cache keys by a bare hash.
- Each of the five sites uses `DenseMap` or `HashMap`, with its hand-written fix-up code gone except the id-chain relink of `ResourceTable`.
