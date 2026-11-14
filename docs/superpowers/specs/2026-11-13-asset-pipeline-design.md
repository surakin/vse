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
| Tools | `tools/asset_mint` (mint, pack, verify, list, remint, unmint), `tools/resource_server` (network backend's peer). Minters and upgrade code are kept out of game binaries by the gate `asset_boundary_check` (Day 337). |
| Id | **Amended 2026-11-13 (the user, after the plan was written: broken dependencies must never happen).** `ResourceId` is a stable 64-bit id assigned once by `asset_mint adopt` and written into the source file itself (every source format is ours, so each has an id field; no sidecar, one file per asset; `mint` stays a pure function of the sources) and into the header of every minted blob; the normalized virtual path (`/`, no `.` or `..`) is an alias in a minted `NAMES` blob, so rename and move never change an id. A duplicate id is a mint error. `ResourceId::None()` is the explicit null reference. |
| Dependencies | Every blob has a standard, sorted dependency section `(id, type, type version, role)`; the loader follows only declared dependencies. The mint refuses an unresolved or wrongly typed reference, a cycle, a duplicate id, and the deletion of a referenced asset; `pack` refuses an incomplete closure (a patch pak names its base pak by content hash); the runtime loads a resource's whole closure atomically and publishes no handle on failure, naming the chain. No optional references; no silent fallback (fallbacks only behind `assets.allow_fallbacks`, counted, zero asserted in tests and demos). |
| Content hash | SHA-256, truncated to 128 bits, portable code (Spike 2: 375 MB/s on one core). Used for the cache key, the verify command and the pak; it is not on the load path of a local file. |
| Blob container | A fixed header (magic, container version, type, type version, section count, total size, header CRC), then sections: id, offset, size, each 64-byte aligned, bounded by settings. Everything little-endian. The loader validates the header and the section table, then hands the sections out as spans of the one buffer. |
| Versioning | Current version only at run time. An older or newer version fails with `Status::VersionMismatch` naming the command. |
| Load path | Read into a **pre-faulted, 64-byte-aligned buffer** from a pool (Spike 5), validate the section table, point the type's view at the sections. Textures and meshes go to the GPU from the span without an intermediate copy (needs the trusted entry points below). |
| Backends | filesystem, pak, network, behind `AssetBackend` (stat, read range, list); a mount table by priority; later mounts shadow earlier ones. |
| Pak | A single file: blobs, then an index sorted by id, then a **hash table built at open** (Spike 1), then a footer with the index offset and a checksum. |
| Source formats | Custom only: `.mesh.src`, `.tex.src` (parameters naming a raw `.rgba` or `.pgm`-like file), `.mat.src`, `.level.src`. No PNG or glTF reader in the engine (the user, 2026-11-12); **amended 2026-11-14 (the user): glTF 2.0 imports through the offline `asset_mint import-gltf` (Days 334-336), with PNG and JPEG decoders vendored for that tool only.** Phase 31 writes the same blob formats. |
| Texture compression | Own deterministic encoder: BC1, BC4, BC5 and BC7 **mode 6 only** (Spike 3). ASTC waits for Phase 28. The decoder is ours too (CPU fallback and tests). |
| Determinism | Minting is a pure function of the inputs and the tool version: no time stamps, no absolute paths, no hash-table order, no thread-count dependence. Tested by minting twice and with 1 and 8 workers. |
| Manager | Handle = `ResourceId` + generation. `Load<T>` synchronous; `LoadAsync<T>` on jobs (decode and validate on workers, GPU upload on the owner thread). Reference counts, a dependency graph, fallbacks, a streaming budget. `Recreatable` owners keep their loader, so a device reset reloads from the backend. |
| Network backend | Read-only over `net::TcpTransport`; requests stat, list, read range; responses bounded by settings; an on-disk cache keyed by content hash; retry with back-off; offline mode serves the cache. No authentication and no encryption (trusted networks only, the user's deferral of 2026-11-12). |
| Levels | The binary form is the storage module's component records (`ComponentRegistry`) plus a resource-reference table. Spike 5's question (column memory images with a layout fingerprint) is answered on Day 333's planning step, not here. The text form is line-oriented. |
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

## Pass conditions for Day 338 (planner's defaults, the user confirms)

- Warm local load of a 256 MiB blob within 1.5x of the raw `pread` time into a pre-faulted buffer (the Spike 5 row: about 26 ms).
- `load_conversions == 0` and zero allocations on the load path after the pool is warm.
- Pak open of 100,000 entries under 5 ms; lookup under 50 ns.
