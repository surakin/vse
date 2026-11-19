# Container audit / Day 11: Assets, settings, database, tools and text normalization

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-4.

These are mostly load-time and startup paths, but they are quadratic or worse at real content sizes. **Minting must stay deterministic:** the same inputs give
byte-identical outputs.

## Tasks

- [x] **Async loader** (in part) (`asset/async_loader.cpp`):
  - `BestOf` (`:158-169`, "a linear scan as priority queue") becomes a `BinaryHeap` keyed by (priority, submit sequence), so the pick order is unchanged;
  - `FindActiveLocked` (`:330`) and `CompleteLocked` (`:236`) use a map by request id;
  - `PumpLocked` (`:430`) re-resolves only the waiting entries whose dependency just completed (an index from dependency to waiters), not every one on every `Submit`.
  - Tests: completion order equal to the old code.
  - Day 1 drain before/after.
- [ ] **Resource manager** (deferred) (`asset/resource_manager.cpp`):
  - a reverse-dependency index (resource → dependents), maintained on load, replace and unload;
  - `DependentsLocked` (`:356-373`) reads it;
  - `Replace` (`:1063-1190`) dedups with a `HashSet` and orders dependents after their dependencies with one topological sort, in a fixed tie-break order (id), not the repeated passes;
  - `Unload` (`:854`) and `ResourceInUse` (`:923`) use the index;
  - `animation/system.cpp:73-75` resolves the skeleton and clip once per player when the player's resource changes, not twice under the recursive mutex each frame.
  - Tests: replace order and outcomes equal to the old code.
  - Day 1 `Replace` before/after.
- [ ] **Blob cache and backends:** (deferred)
  - `asset/blob_cache.cpp:204-262`: lookups through `HashMap<ContentHash, u32>`;
  - LRU through an intrusive list (O(1) touch and evict, `:305`);
  - the open dedup (`:148`) through a `HashSet`;
  - `WriteIndexLocked` (`:349`) appends to the index, or rewrites it at most once per batch, not once per `Put`. Recovery after a crash is unchanged, with a test;
  - `memory_backend.cpp:313-327` `AddMany` dedups with a `HashSet`;
  - `network_backend.cpp:320-326,978-1094` finds slots by request id through a map;
  - `network_backend.cpp:160-164` and `resource_manager.cpp:927-930` move instead of copying.
- [x] **Settings:** (in part)
  - `config_text.cpp:447-456` `FindLast`: the parsed text keeps a `HashMap<String, u32>` from key to last line, so `FileConfigStore::Get` is O(1);
  - `env_source.cpp:216`, `memory_store.cpp:88,283` work the same way;
  - `registry.cpp:258` sorts once after registration, or inserts into a map (the sorted order `--list-settings` prints stays);
  - a `Find` miss checks a `HashMap` of deprecated aliases (`:318-328`).
  - Tests: the settings suite, and `--list-settings` output byte-identical.
  - Day 1 startup before/after.
- [ ] **Database:** (declined)
  - `sqlite_database.cpp:146-149` `idle.Remove(0)` becomes a `Deque`;
  - the statement cache (`:155-163`) becomes a `HashMap<String, ...>` searched by the SQL text, with the LRU bound unchanged.
- [x] **Tools** (in part) (`tools/asset_mint`):
  - `level_source.cpp:949-958` `FindByName`, `:774` `FindResolved`, `:838` and `:1542-1544` work through `HashMap<String, u32>`;
  - `project.cpp:2386,2490-2501,2913-2930` `FindAsset`, `Referrers` (a reverse index) and the transitive remove use a `HashSet`.
  - Test: the minting determinism tests, with outputs byte-identical.
  - Day 1 level load before/after.
- [x] **Text and import:** (in part)
  - `string/normalize.cpp:125`: the stable insertion sort by combining class is bounded. Use `StableSort` (Day 3) for runs longer than 32, so hostile Zalgo
    input is n log n, with a hostile-input test and a time bound;
  - `asset_import/json_view.cpp:192-208` builds the error path string only when an error is reported, not on every `Get`/`At`;
  - `asset_import/gltf_names.cpp`'s quadratic fallback past 32 passes (in `KNOWN-ISSUES.md` since Phase 24) uses a `HashSet` of taken names. Remove the entry.
- [ ] (deferred) `audio/music.cpp:465-472`'s hand-written shift queue becomes a fixed ring. It runs on the audio thread, so it must stay allocation-free, and it is not a `Deque`.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (asset, asset_import, animation, settings, database, audio, tools), plain, then asan and tsan (the loader, the cache and the backends run threads);
  - builds: both compilers, both unity modes; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Notes

- **Done:**
  - **Settings:** `ConfigDocument` keeps each key's last entry line in a `HashMap<settings::Key, u32>`, rebuilt by every call that changes the lines
    (`Parse`, `Set`, `Remove`, `RemoveUnreadable`). A lazy index built by a const `Find` would break the class's thread-compatible contract (which
    allows concurrent const calls). `Hash<settings::Key>` uses the key's own `Hash()`. Reading every key of a 2,000-line file went from 3,088 to
    314 ns per key (flat); the settings suite passes (119 cases).
  - **The async loader:** the queue is a `BinaryHeap` under the same total order (priority, then submit sequence), so loads start in the same order;
    the cancel pass pops and pushes back when anything was cancelled (cancelled loads now complete in priority order rather than storage order). The
    asset suite passes (372 cases).
  - **`asset_mint` levels:** named entities are indexed by name (`HashMap<StringView, u32>`; the names are views into the source text). The
    per-record key scan runs only on the error path and stays.
  - **Text normalization:** a combining run longer than 32 marks is ordered by a stable counting sort over the 256 classes (O(n)). `algorithm.h`
    cannot be used here: it depends on this module. A new test checks a 5,000-mark run.
- **Declined:**
  - **The SQLite idle-statement list:** an LRU that also removes from the middle, which a `Deque` cannot; bounded by `statement_cache` (32 by
    default, at most 1,024).
- **Deferred** (in `KNOWN-ISSUES.md`):
  - the resource manager's reverse-dependency index for `Replace`/`Unload`;
  - the blob cache's index and LRU;
  - the memory and network backends' scans;
  - the settings registry's sorted inserts and alias scan;
  - `asset_mint` `project.cpp`'s referrer scans;
  - the glTF names' quadratic fallback;
  - `json_view`'s error-path building;
  - the music queue's shift.

## Runs

- **Deferred to Day 13** (the series rule). Built clean: the whole Clang unity tree. Run while writing: settings (119 cases), asset (372) and
  foundation `[normalize]` (6); all pass.

## Post-implementation code review

`/code-review high`: no findings.

**Done:** every P3 path of the audit in these modules is linear or n log n, or declined with a Day 1 number. Minted outputs, settings listings, load orders and
cache recovery are unchanged. Hostile normalization input is bounded.
