# Asset pipeline and resource system: design (Phase 22)

Settled on Phase 22 Day 313 (2026-11-13). Plan: `plans/phase-22-asset-pipeline.md` and its day files. Reasoning for the boundaries: `PLAN.md` assumption #12.

## Principles (the user, 2026-11-13)

1. **Loading performance is paramount.** Every format is designed so that the load is a read plus a bounded validation, nothing else.
2. **Load-time conversions are discouraged.** A loader never converts endianness, repacks, decompresses a format of ours, re-indexes or re-orders. The mint writes the bytes the GPU and the CPU want.
3. **Upgrades are enforced offline.** The runtime reads the current format version only and refuses an older one with a message naming `asset_mint remint`. Upgrade chains live in `tools/asset_mint` and never ship in a game binary. The counter `load_conversions` is exposed by the manager and stays zero in every test.

## Decisions

| Topic | Decision |
|---|---|
| Module | `engine/asset`, layer 13 (beside `scene`): depends on `foundation`, `jobs`, `ecs`, `storage`, `net`, `rhi`, `renderer`; nothing below includes it. |
| Tools | `tools/asset_mint` (mint, pack, verify, list, remint, unmint), `tools/resource_server` (network backend's peer). Minters and upgrade code are kept out of game binaries by the gate `asset_boundary_check` (Day 339). |
| Id | **Amended 2026-11-13 (the user, after the plan was written: broken dependencies must never happen).** `ResourceId` is a stable 64-bit id assigned once by `asset_mint adopt` and written into the source file itself (every source format is ours, so each has an id field; no sidecar, one file per asset; `mint` stays a pure function of the sources) and into the header of every minted blob; the normalized virtual path (`/`, no `.` or `..`) is an alias in a minted `NAMES` blob, so rename and move never change an id. A duplicate id is a mint error. `ResourceId::None()` is the explicit null reference. |
| Dependencies | Every blob has a standard, sorted dependency section `(id, type, type version, role)`; the loader follows only declared dependencies. The mint refuses an unresolved or wrongly typed reference, a cycle, a duplicate id, and the deletion of a referenced asset; `pack` refuses an incomplete closure (a patch pak names its base pak by content hash); the runtime loads a resource's whole closure atomically and publishes no handle on failure, naming the chain. No optional references; no silent fallback (fallbacks only behind `assets.allow_fallbacks`, counted, zero asserted in tests and demos). |
| Content hash | SHA-256, truncated to 128 bits, portable code (Spike 2: 375 MB/s on one core). Used for the cache key, the verify command and the pak; it is not on the load path of a local file. |
| Blob container | A fixed header (magic, container version, type, type version, section count, total size, header CRC), then sections: id, offset, size, each 64-byte aligned, bounded by settings. Everything little-endian. The loader validates the header and the section table, then hands the sections out as spans of the one buffer. |
| Versioning | Current version only at run time. An older or newer version fails with `Status::VersionMismatch` naming the command. |
| Load path | Read into a **pre-faulted, 64-byte-aligned buffer** from a pool (Spike 5), validate the section table, point the type's view at the sections. Textures and meshes go to the GPU from the span without an intermediate copy (needs the trusted entry points below). |
| Backends | filesystem, pak, network, behind `AssetBackend` (stat, read range, list); a mount table by priority; later mounts shadow earlier ones. |
| Pak | A single file: blobs, then an index sorted by id, then a **hash table built at open** (Spike 1), then a footer with the index offset and a checksum. |
| Source formats | Custom only: `.mesh.src`, `.tex.src` (parameters naming a raw `.rgba` or `.pgm`-like file), `.mat.src`, `.level.src`. No PNG or glTF reader in the runtime engine (the user, 2026-11-12; amended 2026-11-14: glTF, PNG and JPEG are read by the opt-in `engine/asset_import` library, which nothing in the engine depends on); **amended 2026-11-14 (the user): glTF 2.0 imports through the offline `asset_mint import-gltf` (Days 335-338), with PNG and JPEG decoders vendored for that tool only.** Phase 32 writes the same blob formats. |
| Texture compression | Own deterministic encoder: BC1, BC4, BC5 and BC7 **mode 6 only** (Spike 3). ASTC waits for the future Android and iOS port. The decoder is ours too (CPU fallback and tests). |
| Determinism | Minting is a pure function of the inputs and the tool version: no time stamps, no absolute paths, no hash-table order, no thread-count dependence. Tested by minting twice and with 1 and 8 workers. |
| Manager | Handle = `ResourceId` + generation. `Load<T>` synchronous; `LoadAsync<T>` on jobs (decode and validate on workers, GPU upload on the owner thread). Reference counts, a dependency graph, fallbacks, a streaming budget. `Recreatable` owners keep their loader, so a device reset reloads from the backend. |
| Network backend | Read-only over `net::TcpTransport`; requests stat, table (paged, with a generation), list, read range (protocol v1, Day 331: `asset/resource_protocol.h`, `asset::ResourceServer`, `tools/resource_server`; the message table is in `docs/guide/assets.md`); responses bounded by settings; an on-disk cache keyed by content hash; retry with back-off; offline mode serves the cache. No authentication and no encryption (trusted networks only, the user's deferral of 2026-11-12). |
| Levels | The binary form is the storage module's component records (`ComponentRegistry`) plus a resource-reference table. Spike 5's question (column memory images with a layout fingerprint) was answered on Day 334: **no**, see "Level column images" below. The text form is line-oriented. |
| Skeletal assets (added 2026-11-13) | `SKEL` and `ANIM` blobs and a mesh skin stream (4 influences, weights summing to 255, joints at most 256) in `engine/asset`; the runtime in the new module `engine/animation` (layer 12, below `asset`, no `asset` include); GPU skinning from a per-frame 48-byte-per-joint palette in the vertex shader (permutation `SKINNED`); packed vertex formats added to the RHI first (Day 317). Limits and deferrals: PLAN.md 2026-11-13 entry. |
| Settings | `assets.*` through `settings::Registry`: mounts, cache directory and size, streaming budget, worker count, network timeouts. |

## Spike results

All throwaway code, measured on the Linux machine (plain `-O2`, one core unless stated). Numbers are for the decisions, not benchmarks.

### Spike 2: content hash

Portable SHA-256: **375 MB/s**. A 256 MiB blob hashes in about 0.7 s, so the hash is for the mint, `verify` and the cache key, never for a local load. Test vectors for Day 314: the FIPS 180-4 vectors for "", "abc", the 448-bit message and one million `a`.

### Spike 1: pak layout and lookup

100,000 entries: a hash table built at open takes **0.42 ms** and a lookup **5 ns**, against **79 ns** for a binary search of the sorted index; opening (reading the index) takes about **1.1 ms**. Decision: the sorted index stays in the file (for `list` and a deterministic layout); the open builds the hash table in memory. No hash table is stored in the file, so there is no second layout to keep in step.

### Spike 5: the zero-copy load path (256 MiB)

| Step | Time | Rate |
|---|---|---|
| `pread` into a pre-faulted aligned buffer | 17.5 ms | 15.3 GB/s |
| `pread` into a fresh (unfaulted) aligned buffer | 75.0 ms | 3.6 GB/s |
| `memcpy`, warm destination | 103.7 ms | 2.6 GB/s |
| copy into a freshly allocated `Vector`-like buffer | 175.7 ms | 1.5 GB/s |
| `mmap` + populate | 0.1 ms to map | pages fault on touch |
| validation scan of an index range / finite positions | 6.3 / 6.1 ms | about 43 GB/s |

Conclusions: (1) a **pre-faulted buffer pool** is worth 4x on the read; a fresh buffer pays for page faults, not for the copy; (2) any copy costs 6x to 10x the read, so **no copy between the read and the GPU upload**; (3) validation is cheap (6 ms per 256 MiB), so it stays; (4) the renderer's `MeshData::Set` (validates and copies) and `TextureImage` (owned `Vector<u8>`) are copies the load path cannot afford. **Renderer changes this phase needs**: a trusted, zero-copy entry into `GpuMesh` and `GpuTexture` from a span of the blob (Days 319 and 323), and `BC4_UNorm` in `rhi::Format` with the backend mappings (Day 324).

### Spike 4: texture upload and the reset (by reading the code)

`TextureLoader` is the seam: a loader that asks the resource manager survives a device reset because the owner keeps its loader and rebuilds through `Recreate`. Gaps found: `rhi::Format` has no `BC4_UNorm` (has BC1, BC5, BC7); `GpuTexture` takes its pixels as an owned `Vector<u8>`.

### Spike 3: BC encoding (1024x1024, one core, unoptimized)

A range-fit encoder (principal axis, projection, endpoint refinement by least squares) for BC1; BC7 mode 6 (RGBA, 7-bit endpoints with p-bits, 4-bit indices) with the same fit and the four p-bit combinations; and an upper bound for a two-subset mode (2-means partition, 3-bit indices, RGB only).

| Image | BC1 PSNR | BC7 mode 6 PSNR (RGBA) | BC1 speed | mode 6 speed |
|---|---|---|---|---|
| gradient | 42.9 dB | 53.6 dB | 58 MP/s | 4.7 MP/s |
| checker + ramp | 44.5 dB | 52.4 dB | 76 MP/s | 7.4 MP/s |
| photo-like (fBm, alpha) | 44.8 dB | 54.5 dB | 42 MP/s | 3.7 MP/s |
| white noise | 13.7 dB | 15.2 dB | 40 MP/s | 3.7 MP/s |

The two-subset bound gave 57.1 dB on the photo-like image against 54.4 dB for mode 6 RGB (+2.7 dB) and 17.1 dB against 13.9 dB on noise; its smooth-image figures are unreliable (the throwaway bound code had a defect), and it ignores partition signalling, so treat it as an upper bound only. **Decision: BC1, BC4, BC5 and BC7 mode 6. The partitioned BC7 modes (0-3, 7) are not built in this phase**; they add a partition search that costs about 10x the encode time for about 3 dB at best, and mode 6 is already 8 to 10 dB above BC1. Revisit if a real asset shows a visible gap. A 1024x1024 mip chain mints in well under 2 s per core with mode 6; the mint parallelizes by block rows, and the output must not depend on the worker count (each block is encoded independently, so it does not).

## Pass conditions for Day 340 (planner's defaults, the user confirms)

- Warm local load of a 256 MiB blob within 1.5x of the raw `pread` time into a pre-faulted buffer (the Spike 5 row: about 26 ms).
- `load_conversions == 0` and zero allocations on the load path after the pool is warm.
- Pak open of 100,000 entries under 5 ms; lookup under 50 ns.

## Level column images (decided on Phase 22 Day 334: not built)

The question: store the components that are trivially copyable plain data as one memory image per component type, with a layout fingerprint (name, id, version, size, alignment, field kinds) checked at load, and let the ECS adopt a column into an archetype chunk without per-entity work.

Measured with the hidden case `A level of 100,000 entities: load time and the floor of creating the entities` (`tests/asset/level_test.cpp`; entities with a `local_transform` and a `mesh_reference`, a release build, 20,000 entities scaled by 5 because the level text minter is super-linear, see `KNOWN-ISSUES.md`): `LoadLevelBlob` takes **20.4 ms for 20,000 entities, 1.02 µs per entity, about 100 ms for 100,000**. Parsing and checking the records (`ParseLevelBlob`) is 5.8 ms of that (29 %, 0.29 µs per entity); the rest, 73 %, is decoding each record into its component, creating the entity and entering its key in the `KeyIndex`. Creating the entities and the two components directly, with no decoding and no key index, costs 23 ns per entity (0.5 ms). In a debug build the same load is 4.8 µs per entity.

Why not: (1) the ECS has no way to adopt a column: an archetype chunk is filled row by row (`World::CreateIn`, a `RowRef`), and every entity still needs its id, its `entity_key` and a `KeyIndex` entry, which are per-entity work whatever the components cost. (2) The best case, decoding for free, saves part of the 73 % and leaves the key and entity work; the parse part is already 0.29 µs. A level of 100,000 keyed entities is far above what a scene holds, and at about 100 ms it loads inside the streaming budget of Day 330 (the level is built on the owner thread in steps, and the closure is read on the workers). (3) A memory image makes the file depend on the layout of a C++ struct (padding, alignment, field order, endianness): a fingerprint refuses a mismatch but does not make the format portable, and a second form of the same data would need its own minter, verifier, upgrade chain and tests. The text form, the record form and the world must stay one thing (`FormatWorld` and `FormatLevelBlob` print the same text; Day 327). The decision is revisited if a measurement of a real level shows the load time above its budget; the first step would then be a batched record decoder, not an image.

## The JSON library (Phase 22 Day 335, the spike)

The in-memory glTF importer (`engine/asset_import`) parses files that may come from the network, so the library is judged on hostile input without exceptions, not on convenience. The user's rule: no hand-written JSON parser.

| Candidate | Verdict |
|---|---|
| **yyjson 0.13.0** (MIT, C99, `yyjson.c` 11,480 lines and `yyjson.h` 8,467) | **Chosen.** Builds clean with `-Wall -Wextra -Werror` as C99 on Clang and GCC. Strict RFC 8259 by default (a leading zero, a trailing comma, `NaN`, a BOM, invalid UTF-8, trailing bytes and `1e999` are refused); an iterative reader, with `YYJSON_READER_DEPTH_LIMIT` at 128 a nesting bomb of 2,000,000 `[` is refused (the default is no limit, so the definition is set); `yyjson_alc_pool_init` over a buffer of `yyjson_read_max_memory_usage(length)` bytes bounds a parse's memory by the input's length, with no `malloc` call; `YYJSON_DISABLE_FILE` removes `fopen`. A fuzz run of 20,000 mutated and truncated documents under asan and ubsan with the pool allocator: 1,328 accepted, 18,672 refused, no finding. It accepts a repeated object key (the importer's wrapper refuses it) and a big integer (read as a double). |
| RapidJSON 1.1.0 | Not chosen: the last release is from 2016, and its default parse recurses; kept as the fallback. |
| simdjson | Not chosen: a large C++ surface with SIMD code paths that would have to be proved deterministic and portable to every target (consoles, mobile) for no benefit at glTF sizes. |
| jsmn | Not chosen: a tokenizer only, so strings, numbers and UTF-8 would be ours to write. |
| nlohmann/json 3.12 | Not for this module: it throws, or aborts without exceptions, on a wrong type, and it needs `std` types. It stays in `tools/shader_mint`. |

## Results (Phase 22 Day 340, Linux x86-64, 16 hardware threads, Release, Clang)

Measured by `tests/asset/asset_bench.cpp` (`[.][asset-bench]`) and `tests/animation/animation_test.cpp` (`[.][animation-bench]`); the pass conditions are the
planner's defaults above, **not yet confirmed by the user**.

| Measure | Result | Pass condition | |
|---|---|---|---|
| Lookup by id in a table of 100,000 entries | 22 ns | under 1 microsecond | met |
| Lookup by path, same table (path formatting included) | 399 ns | under 1 microsecond | met |
| Opening and validating an 8 MiB blob | 0.1 microseconds (the sections are indexed, the bytes are not scanned; the content hash is a separate pass) | not stated | |
| BC1 encode, 1 / 8 workers | 42 / 208 megapixels per second | at least 20 on 8 workers | met |
| BC4 encode, 1 / 8 workers | 289 / 1,298 | | |
| BC5 encode, 1 / 8 workers | 153 / 660 | | |
| BC7 encode (`normal`), 1 / 8 workers | 13.6 / 77.4 | at least 2 on 8 workers | met |
| Minting a 1024 x 1024 BC7 texture with its full mip chain, 1 worker | 0.12 s, 1,402,240 bytes | | |
| Adding 10,000 blobs of 4 KiB to the memory backend | 270 ms (blob building included) | | |
| 10,000 characters of 20 joints through the animation system, 1 / 8 workers | 8.13 / 1.63 ms per update | 4 ms for 10,000 characters of 40 joints on 8 workers | 20 joints measured; 40 not measured |
| Clip samples per second (40 joints, 3 tracks) | 4.58 million | | |

**Not measured on Day 340, and open:** sequential pak read against loose-file read (80 %), the ranged read rates of the three backends, the load path at 90 % of
the raw read rate, a 10,000-resource level fully resident with the frame-time p99 while streaming, the bulk component load per 1,000 entities, the asynchronous
latency, and 40-joint characters. Spike 5's figures above (the zero-copy path) stand for the load path; the rest is in `KNOWN-ISSUES.md`.

## What was built that the decisions did not say (deviations from Day 313)

- Days were split and inserted: Day 333 became two (the tool's first and second parts), and Days 335-338 are the glTF importer, an in-memory library
  (`engine/asset_import`) for a host that imports at run time, added on 2026-11-14 with the user's decisions: no hand-written JSON (yyjson by a spike),
  zlib and libpng and stb_image's JPEG decoder vendored behind the importer, the command line through `os::CommandLine`.
- The texture and material minters' cores moved from `tools/asset_mint` into `asset` (`MintTextureFromPixels`) and `asset_import` (`MintMaterial`) so that the
  tool and the importer mint identically; `asset_mint` links `asset_import`.
- Not built: the level as a glTF scene, the project registration of a glTF, the golden-image render of an imported model, the animated demo, the
  three-backend screenshot comparison, device reset and hot reload in the demo, the loading tutorial (`KNOWN-ISSUES.md`, Days 338 and 339).
