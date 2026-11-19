# Container audit / Day 7: DenseMap, real key types, and the indexed-vector sites moved onto them

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-6. Inserted on 2026-11-19 at the user's request; the days after it moved up by one.

Days 4-6 wrote one pattern five times by hand. Entries sit in a `Vector`, a `HashMap` maps a key to an entry's position, and each entry carries its own key
so that a swap-remove can rewrite the moved entry's position. Every site has its own fix-up code (`ResourceTable::RelinkId`,
`DeviceResources::RemoveAt`, `SceneFrame::DropUnusedLabels`, the theme eviction, the pipeline index), and two caches key by a 64-bit hash with a
collision fallback because their key structs have no `Hash`. This day puts the pattern in one container, gives the keys real types, and moves each site
to the simplest container that fits.

## Tasks

- [x] **`DenseMap<K, V, Allocator>`** in `containers/dense_map.h`:
  - the values are dense in slots, and a `HashMap<K, u32>` maps a key to its slot;
  - `Find`/`Contains` (heterogeneous too), `Emplace` (returns value, slot and whether it was made), `Remove`, `RemoveAt(slot)`, `RemoveIf(pred)`
    (one pass), `At(slot)`, `KeyAt(slot)`, `SlotOf`, `Size`, `Clear`, `Reserve`, and range-for over `{key, value}` in slot order;
  - removal moves the last entry into the hole and reports the slot it came from, so a caller holding slots can follow it;
  - slots are stable until a removal, and slot order is deterministic for a given sequence of calls;
  - tests against a `HashMap` oracle under churn: slot stability, `RemoveIf`, move-only values, range-for order, the moved-slot report.
- [x] **Real keys:**
  - `PipelineKey` with `operator==` and `Hash<PipelineKey>` (`HashCombine`) replaces the material cache's `key_hash`, its collision scan and the
    re-indexing in `DestroyPipelines`;
  - the scene label cache is keyed by (pixel size, text), searched without making a string.
- [x] **Sites moved:**
  - the material pipeline cache keeps its slots and eviction, and is indexed by `PipelineKey`;
  - the theme cache becomes a `DenseMap<u128, CacheEntry>`, evicting with `RemoveAt` round-robin;
  - `ResourceTable` becomes a `DenseMap<String, Entry>` by path, keeping the id chains relinked through the moved-slot report;
  - `DeviceResources` becomes a plain `HashMap<Recreatable*, Entry>`;
  - the scene label cache becomes a plain `HashMap` with a collect-then-remove sweep.
- [x] **Docs:** a `DenseMap` row and section in `docs/guide/foundation-containers.md` (use it when you iterate in a stable order or hold slots, and
  `HashMap` otherwise); `///` with labels; `docs/reference` regenerated.
- [x] **Standing rows:**
  - build clean (Clang unity); tests deferred to Day 13 (the series rule), but the new and touched tests are run while writing;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **`DenseMap`** (`containers/dense_map.h`):
  - the entries in a `Vector`, a hash per slot, and an index of 4-byte slot numbers (`slot + 1`, empty and tombstone cells) probed by mask and
    compared against the stored keys, so a key lives once (a `String` key is move-only) and growth never hashes a key again;
  - `RemoveAt` returns the slot it moved from. Tests check it against a `HashMap` oracle over 20,000 random operations, plus slot stability,
    `RemoveIf` and `String` keys with move-only values.
- **What each site became** (two differ from the plan, from reading them):
  - **The material pipeline cache:** a `HashMap<detail::PipelineKey, u32>` over its fixed slots, not a `DenseMap`. Callers keep a slot (it goes
    into the draw sort key), and a destroyed template frees its slots without moving the others, which a dense container would do. The key hash,
    the collision scan and the re-indexing are gone.
  - **The theme cache:** `DenseMap<u128, CacheEntry>`. A full cache removes one slot in turn (`RemoveAt`) and appends.
  - **`ResourceTable`:** `DenseMap<String, Entry>` by path; the id chains are relinked through the slot `RemoveAt` reports. `At(index)`
    walks slots, in insertion order until a removal; its callers copy tables or report diagnostics on tables read from a blob, which have no
    removals.
  - **`DeviceResources`:** `HashMap<Recreatable*, Entry>`; each step sorts (tier, sequence), which is unique.
  - **The label cache:** `HashMap<detail::LabelKey, CachedLabel>`, searched with `LabelKeyView` (no string made); the sweep collects, then removes.
- Pages touched: `docs/guide/foundation-containers.md` (a `DenseMap` row and section; keys made of several fields get a struct and a `Hash`, never a
  bare hash) and `docs/reference`.
- **`HashSet` on its own table** (the user's question, then "yes, do it now"):
  - `HashSet` was a `HashMap<K, empty>`, which doubled a `u64` set's slot size and inverted the layering.
  - Both now sit on `detail::OpenTable<Element, KeyOf, Allocator>` (`containers/open_table.h`), which holds the probing, growth, tombstones,
    `Clear`, `Reserve`, the change counter and the walk. `HashSet` stores bare keys (`SetKeyOf`), and `HashMap` stores `HashMapEntry` (`MapKeyOf`).
  - Neither public API changed. The map's tests, the set's and `DenseMap`'s pass (17 cases), and the whole tree builds unchanged.
  - Spot numbers (`container_bench`, 3 runs, not the Day 13 table): `HashMap<u64>` find hit at 1e5 was 6.9 ns on Day 1 and is 4.7 ns now (the
    mask probe of Day 2), and insert at 1e6 is about 100 ns (88 on Day 1).

## Runs

- **Deferred to Day 13** (the series rule). Built clean: the whole Clang unity tree. Run while writing: foundation `[dense_map]` (3 cases),
  renderer `[material]` (18) and `[reset],[geometry]` (4), UI `[theme]` (11) and asset `[table]` (11); all pass.

## Post-implementation code review

`/code-review high`: 1 finding, fixed. The reset loops looked owners up by the address in the step's visit list, so an owner moved by a `Rebind` during
the step would have been looked up as null and dereferenced. The step now records moves (old address to new, with no cycles) and follows them, as the
old array did; the record is cleared when the step ends.

The `OpenTable` change got its own `/code-review high`: 1 finding, fixed. `Insert` of a new key probed twice (find, then slot) where the old
code probed once; it probes once again when no growth is due.

**Done:**
- `DenseMap` exists and is tested against an oracle.
- No cache keys by a bare hash.
- Each of the five sites uses `DenseMap` or `HashMap`, with its hand-written fix-up code gone except the id-chain relink of `ResourceTable`.
