# Container and algorithm audit (2026-11-19)

Read-only audit of every module (`engine/`, `game/`, `tools/`; tests skipped). It covers which containers and algorithms the code
uses, where they are slow, and where a slow choice was made because the fast container lacked a feature. Nothing has been changed.

**How it was done:** I read the foundation containers in full. Three parallel sweeps then read the modules, split
(1) physics/spatial/ecs/script/engine_c, (2) renderer/rhi/text/ui/platform, (3) asset/storage/net/audio/foundation/tools.
Items marked **[v]** I re-read in the code myself. The other items are the sweeps' readings with file:line, and none has been timed. There are no
measurements here; the impact estimates come from how often a path runs and how large the container can grow.

## 1. Root cause: what the foundation containers lack

Almost every finding below goes back to a handful of missing features in `engine/foundation/containers/` and
`algorithm.h`:

| Missing | What it caused |
|---|---|
| **A hash for strings and bytes** (`Hash<K>` exists only for integers, pointers and a few structs) **[v]** | FNV-1a written out again in ~26 files in 20 modules; string keys stored as a 64-bit hash, sometimes **never compared on a hit** (see §2); most string-keyed lookups are linear scans or sorted vectors |
| **HashMap iteration** **[v]** | a parallel `Vector` beside the map (`script/task_pool.h:153-156`), or a full slot walk; or no map at all |
| **HashMap `Clear`, move** **[v]** | `UniquePtr<HashMap>` re-created to clear it (`storage/save_session.cpp:19-20`, `script/luajit/luajit_backend.cpp:2207`); `asset/resource_table.cpp:44-50` removes keys one by one |
| **Move-only HashMap values** (`Insert(const K&, const V&)`; `V` must be default-constructible and copyable) | raw-pointer values beside a `Vector<UniquePtr<Conn>>` in all three net transports |
| **HashSet** | `HashMap<K, u8>` used as a set (async_loader.cpp:512, world_storage.cpp:702/912, udp_transport.cpp:308, physics_bindings.cpp:691) |
| **Heterogeneous lookup / struct keys without boilerplate** | hand-written open-addressing tables: `text/glyph_atlas.cpp:30-97`, `asset/pak.cpp:520-560`, `settings/config_text.cpp:157` (`SeenKeys`), `resource_table.cpp` (hand-chained `next_by_path`) |
| **A priority queue / heap** | `asset/async_loader.cpp:158-169` `BestOf`, "a linear scan as priority queue" (O(n²) drain); a hand-written heap sort in `script/component_bindings.cpp:1093-1132` |
| **A deque / ring queue (growable)** | `Remove(0)` used as a FIFO: `database/sqlite/sqlite_database.cpp:146`, `net/sim_transport.cpp:216`, `script/script_debug.cpp:308`, `script/script_runtime.cpp:582`, `audio/music.cpp:465-472` (inlined) |
| **Swap-remove, bulk append, copy/clone** for `Vector` **[v]** | order-keeping `Remove(i)` inside scans (O(n²)), element-by-element copy loops (solver cache every step, pair tracker every update, clipboard, text field on every edit), hand-rolled swaps |
| **A sorted map / flat map** | ~15 sorted vectors with `LowerBound` + `Insert(at)` standing in, several O(n²) on random-order input (`storage/memory_store.cpp:105-149`, `settings/registry.cpp:258`) |
| **`StableSort` for non-trivial types; `Sort` on a const `rhi::Span`** | insertion sorts with "a handful"/"tiny" comments that don't hold (`ui/draw_list.h:131-149`, `string/normalize.cpp:125`); `SortRaw` (`qsort` through a function pointer) used on typed vectors every frame (`renderer/gbuffer.cpp:625`, `frame_data.cpp:348`) **[v]** |

There are also performance problems inside the containers themselves:

- `HashMap` computes `(start + probe) % cap` on every probe step **[v]**, a 64-bit division where a power-of-two mask would
  do. Its rehash copies every live slot into a temporary vector and then re-inserts it. Each constructor allocates 16 slots up front, so a local
  map in a per-call function (`ecs/transform.cpp:66/81/228`) allocates on every call.
- `String::Append` / `AppendRawBytes` push one byte at a time, with a growth check on each push, and never reserve **[v]**.
- `Vector::TryReserve` moves elements one at a time even when they are trivially copyable. This is minor, because compilers usually turn it into `memcpy`.

## 2. Correctness problems the audit turned up

These are not just slow:

1. **The renderer caps device-resource owners at 256 in total** **[v]**. `renderer/device_resources.h:83,172` is a fixed C array,
   `Entry entries_[kMaxOwners]`, with `kMaxOwners = 256`. Every `GpuMesh`, `GpuTexture` and `GpuSampler` registers in it, along with
   ~10 renderer systems. Once the array is full, `Attach` fails, and `GpuMesh` creation releases everything and fails (`mesh.cpp:1217-1222`).
   A level with more than ~240 meshes plus textures therefore cannot load. This is not in `KNOWN-ISSUES.md`. It also breaks the "no raw arrays" rule.
2. **Path keys that are never compared**:
   - `tools/asset_mint/project.cpp:680-691` **[v]**: a hash collision between two different paths makes the second `Insert` overwrite the
     first, and the first path becomes unfindable.
   - `luajit_backend.cpp:906-920` `ProfileKey` **[v]**: an FNV hash of source+line+name, never compared, so functions whose hashes collide merge in the profile.
   - `script/script_reload.h:99` / `.cpp:91,144`: `last_tick_` is keyed by the key hash only.

   With 64-bit FNV a collision is very unlikely, but the code is wrong in principle, and a string hash plus string keys would remove it.
3. **`physics/step.cpp:246` says the joint pairs are kept "as sorted keys", but the list is never sorted**, and it is scanned linearly with no
   early exit (§3, P1) **[v]**.

## 3. Hot-path findings, by likely impact

**P1: per frame, tick or step, superlinear in the scene size**

| Where | What | Cost |
|---|---|---|
| `physics/step.cpp:318-323` **[v]** | every broad-phase pair scans every joint pair, with no early exit | pairs × joints per step |
| `engine_c/surface_scene.cpp:594-602` **[v]** | every drawn entity scans `entries_` (one per drawn entity); also `ResolveMesh`/`ResolveMaterial`/`MaterialFor` and the light path | O(N²) per frame |
| `script/luajit/luajit_backend.cpp:2100-2124` **[v]** (also `script/expr/expr_backend.cpp:1216,1309`) | resuming a coroutine scans `suspended_`, then calls order-keeping `Remove(i)` | O(N²) per tick with N waiting scripts |
| `renderer/material.cpp:1803-1806` **[v]** | each visible object scans the pipeline cache (up to 256 entries) with attribute-wise compares | objects × pipelines per frame |
| `ui/theme.cpp:629-653` **[v]** | each widget draw scans a style cache of up to 4096 entries, which is cleared when full (it thrashes); a cache hit on a missing style builds a heap `String` | widgets × cache per frame |
| `physics/query.cpp:405-421` | ray/overlap/shape casts loop over every collider and skip the broad phase; the character controller runs ~5 per character per step | characters × colliders |
| `rhi/dx12/dx12_command_list.cpp:158-189` | `FindBuffer`/`FindTexture`: a reverse linear scan per vertex/index/group bind | draws × buffers per list |
| `script/value.cpp:413` **[v]** | `Value::Set`/`Find` are linear over the keys (maps of up to 65536); every event payload and every decode builds maps this way, allocating a `String` per key | O(n²) per map |
| `script/script_runtime.cpp:1499-1506` | `HasHandler` does a linear string compare over the waiters, once per contact or trigger event | events × waiters per tick |
| `script/command_buffer.cpp:108-127` | the overlay `Put`/`Find` is linear, on every `get_position`/`set_position` | O(k²) per callback |
| `script/task_pool.cpp:172-186` | `TakeTask` scans `taken` (up to 1024) and the requests on every take | O(n²) per tick |
| `net/udp_transport.cpp:1591-1648` (and `sim_transport.cpp:535-585`) | draining the reorder buffer restarts a scan after every match, plus `Remove(i)`; up to 1024 buffered messages | O(n²) per lost packet |

**P2: per frame or step, linear or n log n but avoidable**

- Sorting every frame:
  - `physics/sync.cpp:78-107` and `joint_sync.cpp:85-113` gather and sort all bodies and joints.
  - `solver.cpp:963` sorts the mostly-sorted contact cache every step, then copies it element by element (976-980, 1089-1092).
  - `render_scene.cpp:350`, then `gbuffer.cpp:625` (`qsort`), sorts the visible slots twice.
  - `frame_data.cpp:348` fully sorts all point lights to keep the first `max_lights_`; a partial selection would do.
- Text is relaid out every frame:
  - `scene_frame.cpp:285` relays out up to 1024 labels, and `TextRenderer::Draw` relays out on every call.
  - `Font::SetPixelSize` calls FreeType even when the size is unchanged.
  - `text/layout.cpp:48-74` `AdvanceCache` is rebuilt on every layout, so `FT_Load_Glyph` runs per glyph per layout.
- Transient bind groups are rebuilt on every texture switch (`ui_renderer.cpp:427`) and per atlas page per frame (`text_renderer.cpp:428`), and
  each Vulkan bind group makes 4 heap allocations.
- Per-tick rebuilds without `Reserve` in `script_runtime.cpp`: `WakeWaiters`/`WakeTasks` (760-834), `Compact` (409-423), and `Reconcile` (338-350), which sorts twice.
- The per-frame insertion sort in `ui/draw_list.h:137-149`, whose "a handful" comment doesn't hold: a new command starts on every clip, texture or layer change.
- `ui/debug_overlay.cpp:128-149`: with a filter set, every visible row does a substring scan over 512 lines.

**P3: load time, startup or rare events, but quadratic**

- Assets:
  - `asset/resource_manager.cpp:356-373, 1063-1190`: hot-reload dependents, O(n³·d), with "the set is small".
  - `asset/resource_table.cpp:122-127`: every `Remove` rebuilds both indexes, so N replacements cost O(N²).
  - `asset/blob_cache.cpp:204-349`: lookups are linear because the 128-bit `ContentHash` has no `Hash`; an LRU min-scan runs per eviction, and every `Put` rewrites the index.
  - `asset/memory_backend.cpp:313-327`: the batch duplicate check is O(n²).
- Settings: `settings/config_text.cpp:447-456` `FindLast` runs per setting × layer, so startup is O(settings × lines). The `registry.cpp:258` sorted insert
  is O(n²), and a miss scans every deprecated alias.
- Tools: `tools/asset_mint/level_source.cpp:949-958` `FindByName` is O(E²) over a level's entities, and `project.cpp:2490-2501`/`2913-2930` has the same pattern.
- Entities and storage:
  - `ecs/transform.cpp:178-247`: every `SetParent`/`ClearParent`/`DestroyRecursive` collects every `Parent` link in the world, so a hierarchy is O(N²) to build.
  - `storage/memory_store.cpp:105-149`: a sorted-vector store, O(n²) for saves in random order, and `Begin` clones the whole store.
- Physics teardown and waking: `physics/world.cpp:137-147` and `events.cpp:49-75`, where `DestroyBody` scans every joint and contact list; `island.cpp:61-102`, where waking scans every body or pair.
- Rarer, smaller paths:
  - `net/windows/poller_select.cpp:75-345`: linear sets, O(N²) per wait.
  - `script/component_bindings.cpp:1137-1164`: paging re-sorts every match each page.
  - `spatial/hash_grid.h:326-342`: oversized items are O(O·S + O³).
  - `string/normalize.cpp:125`: an insertion sort with an unbounded run length, so hostile Zalgo text goes O(n²).
  - `engine_c/resources.cpp:102-118`: a linear free-slot scan over up to 65536 slots per add.
  - `vk_memory.cpp`: a best-fit free list with linear `Free` and shifting inserts.
  - Thread contention: `animation/system.cpp:73-75` takes the resource manager's recursive mutex twice per player per frame from the job workers.

**Clean:** the job scheduler, the IO thread and the audio mixer/voice paths (fixed arrays, SPSC rings) touch no growable
container on the hot path.

## 4. Suggested order of fixes (for a plan; nothing started)

1. **Foundation first. One day of containers clears most of the list:**
   - a byte/string hash (`HashBytes`, `Hash<StringView>`, `Hash<String>`, and one FNV/wyhash in one place);
   - `HashMap` with iteration, `Clear`, move, move-only values (`Insert(K&&, V&&)`/`Emplace`), heterogeneous `Find(StringView)`, power-of-two masking, and lazy first allocation;
   - `HashSet`;
   - `Vector::SwapRemove`, `Append(Span)`, `Clone`, `Emplace`;
   - a growable `Deque`/ring;
   - a binary heap / `PriorityQueue`;
   - `NthElement`/`PartialSort`;
   - `StableSort` for movable types;
   - `String::Append` by bulk copy with reserve.
2. **Fix the correctness items in §2.** First, make `DeviceResources` growable (or an intrusive list). Then make the hashed path keys compare their strings.
3. **Fix the P1 table**, mostly by swapping a linear scan for a map or set with the new containers. The P2 and P3 items follow, module by module.
4. **Gate it:** a grep gate, or a clang-tidy check, for `Remove(0)` and for FNV constants outside the foundation hash.

None of this has been measured. A plan should start with a benchmark of the P1 paths at scene sizes like the host's, to rank by real cost.
