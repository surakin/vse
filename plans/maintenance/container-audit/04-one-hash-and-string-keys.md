# Container audit / Day 4: One FNV, string keys compared, and the map workarounds removed

**Series:** [the container audit fixes](../container-audit.md)
**Depends on:** Days 2-3.

With a string hash, a set and a complete map, the workarounds can go. This day replaces the private FNV-1a copies with `Fnv1a64`, which leaves every
value unchanged. It turns the hash-only path keys into real string keys, so a collision can no longer lose an entry. It also removes the
re-create-to-clear and map-as-set patterns.

## Tasks

- [ ] **The FNV copies replaced by `Fnv1a64`.** Find them with `git grep -n "1099511628211\|0x100000001b3\|14695981039346656037\|0xcbf29ce484222325"`;
  the audit found 33 lines in 26 files. They are in `asset_import` (3), `asset/resource_table.cpp`, `audio/block_hash.h`, `io/path.cpp`, `settings/key.cpp`,
  `net/address.cpp`, `physics/state_hash.h`, `platform/cursor.h`, `script/*` (5 files), `storage/store.cpp`, `ui/widget.cpp`,
  `game/script_demo/demo_world.cpp` and `tools/asset_mint/project.cpp`.
  - `tools/shader_mint/main.cpp` may stay (a tool with its own std rules). Record the decision.
  - The `rhi/dx12` and `rhi/metal` copies are Day 11's, because they are not built on Linux.
  - Each replacement keeps its value. The proof is the unchanged outputs: the minting determinism tests, the physics and script reference hashes,
    the audio block hashes, the settings key tests and the ABI snapshot all pass unchanged.
- [ ] **Path keys compared on a hit, or keyed by the string itself:**
  - `tools/asset_mint/project.cpp:413,680-691`: `by_path` becomes `HashMap<String, u32>`, searched with `StringView`.
    - Test: two sources whose paths are made to collide through a test-only hash hook, or, if no hook is acceptable, a unit test of the map keyed by `String`. Show the old code losing one.
  - `script/luajit/luajit_backend.cpp:906-920` `ProfileKey`: key the profile by (source, line, name) stored as strings, or compare them on a hit.
  - `script/script_reload.h:99` `last_tick_`: key by the module key `String`.
  - `script/script_world.cpp:711-748`: key by the function's identity fields, not by a hash that is then scanned on.
  - `asset/resource_table.cpp`: the hand-chained `next_by_path`/`next_by_id` table becomes `HashMap<String, u32>` plus `HashMap<u64, u32>`, and removal stops
    rebuilding both indexes (the O(N²) of `Remove` + `RebuildIndexes`, `resource_table.cpp:122-127`). Day 1 number before/after.
- [ ] **Map workarounds removed:**
  - `storage/save_session.cpp:19-20` and `luajit_backend.cpp:2207` use `Clear()`;
  - `HashMap<K, u8>` sets become `HashSet`: `async_loader.cpp:512`, `world_storage.cpp:702/912`, `udp_transport.cpp:308`, `physics_bindings.cpp:691`;
  - `script/task_pool.h:153-156` keeps its slot vector only if an ordered walk needs it. Otherwise `ForEach`, recorded either way;
  - `asset/resource_table.cpp:44-50` key-by-key clearing becomes `Clear()`.
- [ ] **Hand-written hash tables** replaced where the foundation map now fits, each with its own module's tests:
  - `text/glyph_atlas.cpp:30-97` (struct key, `HashCombine`);
  - `settings/config_text.cpp:157` `SeenKeys`;
  - `asset/pak.cpp:520-560`: keep it if it reads a table laid out on disk (record that), otherwise replace it.
- [ ] The raw-pointer values in the net transports (`HashMap<ConnectionId, UdpConn*>` beside `Vector<UniquePtr<Conn>>`) are left as they are. The
  vector's order is the transports' iteration order and is deterministic. The pointer is to an object the vector owns for longer than the map entry.
  Record this as declined, with that reason, unless the review disagrees.
- [ ] **Standing rows** (as [the series rules](../container-audit.md#rules-for-every-day)):
  - runs: `--affected`, plain, then asan and tsan;
  - builds: both compilers, both unity modes;
  - `-DENGINE_BUILD_SHARED=ON` builds;
  - `ctest -L docs`;
  - `/code-review high` recorded below;
  - `STATUS.md` and `KNOWN-ISSUES.md` (the audit's hashed-key entry removed); commit and push.

## Runs

## Post-implementation code review

**Done:**
- One FNV-1a in the engine outside the RHI backends (Day 11) and `shader_mint`, with every hash and minted output unchanged.
- No path or name is keyed by its hash alone.
- The map and set workarounds are gone, and each kept exception is recorded with its reason.
