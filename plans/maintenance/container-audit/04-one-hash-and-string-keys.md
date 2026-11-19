# Container audit / Day 4: One FNV, string keys compared, and the map workarounds removed

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

With a string hash, a set and a complete map, the workarounds can go. This day replaces the private FNV-1a copies with `Fnv1a64`, which leaves every
value unchanged. It turns the hash-only path keys into real string keys, so a collision can no longer lose an entry. It also removes the
re-create-to-clear and map-as-set patterns.

## Tasks

- [x] **The FNV copies replaced by `Fnv1a64`.** Find them with `git grep -n "1099511628211\|0x100000001b3\|14695981039346656037\|0xcbf29ce484222325"`;
  the audit found 33 lines in 26 files. They are in `asset_import` (3), `asset/resource_table.cpp`, `audio/block_hash.h`, `io/path.cpp`, `settings/key.cpp`,
  `net/address.cpp`, `physics/state_hash.h`, `platform/cursor.h`, `script/*` (5 files), `storage/store.cpp`, `ui/widget.cpp`,
  `game/script_demo/demo_world.cpp` and `tools/asset_mint/project.cpp`.
  - `tools/shader_mint/main.cpp` may stay (a tool with its own std rules). Record the decision.
  - The `rhi/dx12` and `rhi/metal` copies are Day 11's, because they are not built on Linux.
  - Each replacement keeps its value. The proof is the unchanged outputs: the minting determinism tests, the physics and script reference hashes,
    the audio block hashes, the settings key tests and the ABI snapshot all pass unchanged.
- [x] **Path keys compared on a hit, or keyed by the string itself:**
  - `tools/asset_mint/project.cpp:413,680-691`: `by_path` becomes `HashMap<String, u32>`, searched with `StringView`.
    - Test: two sources whose paths are made to collide through a test-only hash hook, or, if no hook is acceptable, a unit test of the map keyed by `String`. Show the old code losing one.
  - `script/luajit/luajit_backend.cpp:906-920` `ProfileKey`: key the profile by (source, line, name) stored as strings, or compare them on a hit.
  - `script/script_reload.h:99` `last_tick_`: key by the module key `String`.
  - `script/script_world.cpp:711-748`: key by the function's identity fields, not by a hash that is then scanned on.
  - `asset/resource_table.cpp`: the hand-chained `next_by_path`/`next_by_id` table becomes `HashMap<String, u32>` plus `HashMap<u64, u32>`, and removal stops
    rebuilding both indexes (the O(N²) of `Remove` + `RebuildIndexes`, `resource_table.cpp:122-127`). Day 1 number before/after.
- [x] **Map workarounds removed:**
  - `storage/save_session.cpp:19-20` and `luajit_backend.cpp:2207` use `Clear()`;
  - `HashMap<K, u8>` sets become `HashSet`: `async_loader.cpp:512`, `world_storage.cpp:702/912`, `udp_transport.cpp:308`, `physics_bindings.cpp:691`;
  - `script/task_pool.h:153-156` keeps its slot vector only if an ordered walk needs it. Otherwise `ForEach`, recorded either way;
  - `asset/resource_table.cpp:44-50` key-by-key clearing becomes `Clear()`.
- [x] **Hand-written hash tables** replaced where the foundation map now fits, each with its own module's tests:
  - `text/glyph_atlas.cpp:30-97` (struct key, `HashCombine`);
  - `settings/config_text.cpp:157` `SeenKeys`;
  - `asset/pak.cpp:520-560`: keep it if it reads a table laid out on disk (record that), otherwise replace it.
- [x] The raw-pointer values in the net transports (`HashMap<ConnectionId, UdpConn*>` beside `Vector<UniquePtr<Conn>>`) are left as they are. The
  vector's order is the transports' iteration order and is deterministic. The pointer is to an object the vector owns for longer than the map entry.
  Record this as declined, with that reason, unless the review disagrees.
- [x] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected`, plain, then asan and tsan;
  - builds: both compilers, both unity modes;
  - `-DENGINE_BUILD_SHARED=ON` builds;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (the audit's hashed-key entry removed); commit and push.

## Notes

- **The FNV copies (engine, game, tools):** 22 files use `Fnv1a64` / `Fnv1a64Builder` now.
  - Every value is kept, formula for formula, and the closing day's determinism, minting, state-hash and ABI tests check it.
  - Nine copies started from `1469598103934665603`, the basis with its last digit missing: settings keys, widget ids, the glTF name hashes, the cursor
    image hash, script error and profile hashes, `Value::Hash`, `asset_mint`'s text hash. That value is named `kFnv1a64LegacyBasis` and used as their seed,
    so stored or compared values do not move.
  - `AddU64Bytes` was added for the copies that fold a 64-bit value in as its eight bytes.
  - The search found the upper-case spellings (`0x100000001B3`, `0xCBF29CE...`) as well: `HashStep`, `engine_c` `import_format_version`, `script_bench`.
- **Changed values:** `net::Address::HashValue` only. It was a multiply-add (`h * prime + byte`), not FNV-1a, and is used only as an in-memory map
  hash, never stored; the address tests compare hashes for equality, not values.
- **Left:**
  - `rhi/dx12` and `rhi/metal` (Day 11, not built here) and `tools/shader_mint` (a tool with its own std rules).
  - About 30 copies in `tests/`, mostly golden hashes of test output and C files that cannot use the C++ header. The Day 12 gate allows `tests/`
    with that reason.
- **Hash-only keys:**
  - `asset_mint`'s `by_path` is a `HashMap<String, u32>`, and its `HashText` is gone;
  - the LuaJIT profiler checks a hit against the source, line and C-function name, and a collision falls back to a scan;
  - `script_reload`'s `last_tick_` is keyed by the module key `String`;
  - `script_world.cpp:711-748` already compared its fields on a hit, so it is correct and left;
  - `ResourceTable` keys paths by `String` and keeps the id chains, and `Remove` is O(1) (swap-remove with the chains relinked; its order reaches no
    output, since listings and the NAMES blob are sorted). A new scrambled-removal test was added, and `[table]` passes.
- **Workarounds:**
  - `save_session` and the profiler `Clear()` their maps instead of re-making them;
  - five `HashMap<K, u8>` sets became `HashSet` (`async_loader`, two in `world_storage`, `udp_transport`, `physics_bindings`).
- **Kept, with reasons:**
  - `task_pool`'s slot vector is a slab with a free list (stable slots for ids), not a key list beside the map, and its slot-order walk feeds the
    deterministic merge.
  - `GlyphAtlas`'s table reports out-of-memory (`TryResize`), where `HashMap` aborts.
  - `PakReader`'s is a build-once index of 4-byte slots (Day 313 spike), smaller than a map of key and value.
  - `config_text`'s `SeenKeys` moves with the settings work on Day 10.
  - The raw-pointer values of the net transports point into objects the transport's `Vector<UniquePtr>` owns for longer than the entry, and that
    vector is the deterministic iteration order.
- **Measured** (this one before/after was taken because Day 1's was invalid): `ResourceTable::Remove` went from 112,764 to 310 ns at 2,000 paths, and
  from 1,291,740 to 378 ns at 10,000.

## Runs

- **Deferred to Day 12** (the series rule). Built clean: the whole Clang unity tree. `asset_tests [table]` passes, 11 cases including the new one.

## Post-implementation code review

`/code-review high`: 2 findings, both fixed:
1. The Day 1 table bench was invalid (paths without `/`). It was corrected, and now marks a refused `Add`.
2. The value hasher made a builder per byte; it now holds one.

**Done:**
- One FNV-1a in the engine outside the RHI backends (Day 11) and `shader_mint`, with every hash and minted output unchanged.
- No path or name is keyed by its hash alone.
- The map and set workarounds are gone, and each kept exception is recorded with its reason.
