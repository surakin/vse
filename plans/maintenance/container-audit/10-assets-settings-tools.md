# Container audit / Day 10: Assets, settings, database, tools and text normalization

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-4.

These are mostly load-time and startup paths, but they are quadratic or worse at real content sizes. **Minting must stay deterministic:** the same inputs give
byte-identical outputs.

## Tasks

- [ ] **Async loader** (`asset/async_loader.cpp`):
  - `BestOf` (`:158-169`, "a linear scan as priority queue") becomes a `BinaryHeap` keyed by (priority, submit sequence), so the pick order is unchanged;
  - `FindActiveLocked` (`:330`) and `CompleteLocked` (`:236`) use a map by request id;
  - `PumpLocked` (`:430`) re-resolves only the waiting entries whose dependency just completed (an index from dependency to waiters), not every one on every `Submit`.
  - Tests: completion order equal to the old code.
  - Day 1 drain before/after.
- [ ] **Resource manager** (`asset/resource_manager.cpp`):
  - a reverse-dependency index (resource → dependents), maintained on load, replace and unload;
  - `DependentsLocked` (`:356-373`) reads it;
  - `Replace` (`:1063-1190`) dedups with a `HashSet` and orders dependents after their dependencies with one topological sort, in a fixed tie-break order (id), not the repeated passes;
  - `Unload` (`:854`) and `ResourceInUse` (`:923`) use the index;
  - `animation/system.cpp:73-75` resolves the skeleton and clip once per player when the player's resource changes, not twice under the recursive mutex each frame.
  - Tests: replace order and outcomes equal to the old code.
  - Day 1 `Replace` before/after.
- [ ] **Blob cache and backends:**
  - `asset/blob_cache.cpp:204-262`: lookups through `HashMap<ContentHash, u32>`;
  - LRU through an intrusive list (O(1) touch and evict, `:305`);
  - the open dedup (`:148`) through a `HashSet`;
  - `WriteIndexLocked` (`:349`) appends to the index, or rewrites it at most once per batch, not once per `Put`. Recovery after a crash is unchanged, with a test;
  - `memory_backend.cpp:313-327` `AddMany` dedups with a `HashSet`;
  - `network_backend.cpp:320-326,978-1094` finds slots by request id through a map;
  - `network_backend.cpp:160-164` and `resource_manager.cpp:927-930` move instead of copying.
- [ ] **Settings:**
  - `config_text.cpp:447-456` `FindLast`: the parsed text keeps a `HashMap<String, u32>` from key to last line, so `FileConfigStore::Get` is O(1);
  - `env_source.cpp:216`, `memory_store.cpp:88,283` work the same way;
  - `registry.cpp:258` sorts once after registration, or inserts into a map (the sorted order `--list-settings` prints stays);
  - a `Find` miss checks a `HashMap` of deprecated aliases (`:318-328`).
  - Tests: the settings suite, and `--list-settings` output byte-identical.
  - Day 1 startup before/after.
- [ ] **Database:**
  - `sqlite_database.cpp:146-149` `idle.Remove(0)` becomes a `Deque`;
  - the statement cache (`:155-163`) becomes a `HashMap<String, ...>` searched by the SQL text, with the LRU bound unchanged.
- [ ] **Tools** (`tools/asset_mint`):
  - `level_source.cpp:949-958` `FindByName`, `:774` `FindResolved`, `:838` and `:1542-1544` work through `HashMap<String, u32>`;
  - `project.cpp:2386,2490-2501,2913-2930` `FindAsset`, `Referrers` (a reverse index) and the transitive remove use a `HashSet`.
  - Test: the minting determinism tests, with outputs byte-identical.
  - Day 1 level load before/after.
- [ ] **Text and import:**
  - `string/normalize.cpp:125`: the stable insertion sort by combining class is bounded. Use `StableSort` (Day 3) for runs longer than 32, so hostile Zalgo
    input is n log n, with a hostile-input test and a time bound;
  - `asset_import/json_view.cpp:192-208` builds the error path string only when an error is reported, not on every `Get`/`At`;
  - `asset_import/gltf_names.cpp`'s quadratic fallback past 32 passes (in `KNOWN-ISSUES.md` since Phase 24) uses a `HashSet` of taken names. Remove the entry.
- [ ] `audio/music.cpp:465-472`'s hand-written shift queue becomes a fixed ring. It runs on the audio thread, so it must stay allocation-free, and it is not a `Deque`.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected` (asset, asset_import, animation, settings, database, audio, tools), plain, then asan and tsan (the loader, the cache and the backends run threads);
  - builds: both compilers, both unity modes; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF`;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

## Runs

## Post-implementation code review

**Done:** every P3 path of the audit in these modules is linear or n log n, or declined with a Day 1 number. Minted outputs, settings listings, load orders and
cache recovery are unchanged. Hostile normalization input is bounded.
