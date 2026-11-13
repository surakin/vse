# Phase 22 — Asset Pipeline & Resource System

**Status: in progress (2026-11-13): Day 313 done (spec, spikes, skeleton); `CURRENT_PHASE` is 22. Days 314-330 next. Needs from the user: confirm the benchmark pass conditions on Day 329 (planner's defaults).**

**Depends on:** Phase 17 (ECS), Phase 5 (renderer core: `MeshData`, `GpuTexture`, `MaterialSystem`), Phase 8 (`Recreatable`, the device reset), Phase 19 (storage: the component registry and entity keys the level format reuses), Phase 20 (networking: the network backend is a user of `net::TcpTransport`), Phase 9 (file layer), Phase 11 (settings), Phase 2 (jobs). Placed before physics (23) and audio (24) so both load minted assets. Phase 21 (macOS) is complete on the Mac: the resource system must build and run there too.
**Goal:** a resource system with pluggable storage backends, versioned per-type binary formats, offline tooling that produces them deterministically, and a runtime manager that loads them synchronously and asynchronously: the foundation Phase 30 (Unreal import) targets when converting external content into engine-native form. See root `PLAN.md` assumption #12 for the settled design decisions this phase implements.
**Definition of done:** (the principles above hold: loads do no conversions, old versions are refused and upgraded offline) an offline mint converts source assets (our own custom formats) into engine-ready blobs, packed into a pak, loadable at runtime through the filesystem, pak and network backends with identical results, with no editor or tool code linked into the game binary; minting is deterministic (same inputs and tool version give byte-identical outputs, tested for every minter, across worker counts and across the three machines); a device reset recreates every loaded resource; hostile blobs, paks and requests are refused; the benchmark table is in the spec.

## Principles (the user, 2026-11-13) — they override anything below that reads otherwise

- **Loading performance is paramount.** A minted blob is laid out for the machine that loads it: sections aligned for direct GPU upload, vertex and index data in the exact layout the RHI consumes, textures in their final GPU format with every mip level already generated, strings and references already resolved to ids, no text and no compression on the load path. The loader's job is to read the bytes into an aligned buffer and hand spans of it to the GPU upload; every cost that can be paid at mint time is paid at mint time. Load speed is measured (Day 329) and has pass conditions.
- **Load-time conversions are discouraged.** No format conversion, mip generation, color-space conversion, index re-ordering, re-validation of minted data, decompression or re-encoding when a resource loads. A conversion at load time is a defect in the minter or the format, not a feature of the loader; the manager counts any it is forced to do (`Stats::load_conversions`) and the demo and the benchmarks assert the count is zero. What runtime still checks is bounds (sizes and offsets against the buffer, cheap) so a corrupt file cannot crash it; the content hash is verified when a pak or a network cache entry is opened or fetched and by `asset_mint verify`, not again for every load (a setting turns per-load verification on for development).
- **A resource dependency can never be broken (the user, 2026-11-13: in Unreal, broken asset dependencies are a never-ending source of bugs; here they must not exist).** Not detected late: made impossible by construction, and checked at every stage so that a hole in one is caught by the next. (1) **Identity is stable:** a `ResourceId` is a permanent id assigned once and recorded beside the source; the path is only a name for it, so rename and move never change an id and there are no redirectors. (2) **References are typed ids, resolved at mint time:** a source may write paths, the minter resolves them, and an unresolved or wrongly typed reference is a mint error. (3) **Every blob declares its dependencies in one standard section**, and the loader follows only declared dependencies. (4) **The mint tool refuses what would break a reference:** deleting an asset that is referenced, an id used twice, a cycle; rename, move and delete are `asset_mint` commands that know the references. (5) **`pack` refuses an incomplete closure**, and a pak names any base pak it relies on by content hash. (6) **The runtime loads a resource and its whole closure atomically:** a handle is published only when every dependency is loaded and of the declared type; a failure names the chain and leaves nothing half-loaded. (7) **No silent fallback:** a missing dependency is an error; fallback resources exist only behind a development setting, and every use is counted and fails the demos and tests. There are no optional references: a reference that may be absent is an explicit null id, not a pointer to something that may not exist.
- **Asset upgrades are enforced, offline, whenever possible.** The runtime loads the current version of every format and refuses an older one with `UnsupportedVersion` and a message naming the fix (`asset_mint remint`); the upgrade chains live in `tools/asset_mint`, not in the game. Enforcement points: the minter only writes the current version; `asset_mint pack` refuses a blob that is not current and verified; a format version bump changes the manifest so the next `mint` rebuilds everything of that type; `asset_mint verify --require-current` runs in ctest over the sample project and over a fixture of every older version (which must be reported, and upgraded by `remint` to blobs byte-identical to a fresh mint); a pak and a network server announce their format versions and the manager refuses a mount that serves old ones at mount time, not at first use.

## Decisions made for this plan (the user, 2026-11-12)

- **Source formats are custom only** (assumption #4): text sources for meshes, textures' parameters, materials and levels; a trivial raw image container; no PNG or glTF reader.
- **Texture compression is ours:** a deterministic BC1, BC4, BC5 and BC7-subset encoder in the minter, decoders at runtime; ASTC waits for Phase 28.
- **The network backend is read-only over `net::TcpTransport`** with a content-hash disk cache, retry with back-off and an offline mode; no authentication and no encryption (the user deferred encryption on 2026-11-12: trusted networks only).

## Key deliverables (broken into days below)

- **Resource identity and the blob container** (Day 314): `ResourceId`, `ContentHash`, a versioned blob with bounded sections and an upgrade chain.
- **Backends and mounts** (Days 315, 316, 326): filesystem (loose files, for fast iteration), memory (content the host application provides at run time, reachable from Phase 23's C API), pak (the shipping default), network; upper layers ask by id and never know which served it.
- **Per-type formats and minters** (Days 317, 318-320, 321, 322): mesh, texture (with BCn), material, level (with the only optional text form); each with a version, golden old blobs and a deterministic minter.
- **The resource manager** (Days 323, 324): handles, cache, dependency graph, fallbacks, device-reset survival, asynchronous loading with priorities, cancellation and a per-frame streaming budget.
- **The network side** (Days 325, 326): a protocol and server over the TCP transport, and the caching, retrying, offline-capable client backend.
- **The tool** (Day 327): `asset_mint` (mint, pack, verify, list, remint, unmint), incremental and deterministic.
- **Integration** (Days 328-330): settings, the boundary gate, `asset_demo`, benchmarks, guides, closing runs on three machines and the phase review.

## Days

313. [Design Spec, Spikes (Pak Layout, Hashing, BC Encoding) & Phase Setup](phase-22-asset-pipeline/day-313-design-spec-spikes-and-setup.md)
314. [Resource Ids, the Content Hash & the Versioned Blob Container](phase-22-asset-pipeline/day-314-resource-ids-content-hash-and-blob-container.md)
315. [The Resource Backend Interface, the Filesystem Backend & Mounts](phase-22-asset-pipeline/day-315-backend-interface-filesystem-backend-and-mounts.md)
316. [The Pak Format: Writer, Reader & Pak Backend](phase-22-asset-pipeline/day-316-pak-format-writer-and-reader.md)
317. [The Mesh Format, Its Source Form & the Mesh Minter](phase-22-asset-pipeline/day-317-mesh-format-and-minter.md)
318. [The Texture Format, Mip Generation & the Texture Minter](phase-22-asset-pipeline/day-318-texture-format-mips-and-minter.md)
319. [BC Decoders & the BC1, BC4 and BC5 Encoders](phase-22-asset-pipeline/day-319-bc-decoders-and-bc1-bc4-bc5-encoders.md)
320. [The BC7 Encoder (a Subset) & Quality Settings](phase-22-asset-pipeline/day-320-bc7-subset-encoder.md)
321. [The Material Format & the Material Minter](phase-22-asset-pipeline/day-321-material-format-and-minter.md)
322. [The Level Format, Its Text Form & the Level Minter](phase-22-asset-pipeline/day-322-level-format-text-form-and-minter.md)
323. [The Resource Manager: Handles, Cache, Dependencies & Synchronous Loading](phase-22-asset-pipeline/day-323-resource-manager-handles-and-synchronous-loading.md)
324. [Asynchronous Loading, Cancellation & the Streaming Budget](phase-22-asset-pipeline/day-324-asynchronous-loading-and-streaming-budget.md)
325. [The Resource Server & the Network Protocol](phase-22-asset-pipeline/day-325-resource-server-and-network-protocol.md)
326. [The Network Backend: Cache, Retry & Offline Mode](phase-22-asset-pipeline/day-326-network-backend-cache-and-offline-mode.md)
327. [The `asset_mint` Tool: Project Files, Incremental Minting & Determinism](phase-22-asset-pipeline/day-327-asset-mint-tool-incremental-minting-and-determinism.md)
328. [Settings, the No-Tools-In-The-Game Gate & the `asset_demo`](phase-22-asset-pipeline/day-328-settings-boundary-gate-demo-and-integration.md)
329. [Benchmarks, Guides & the Spec's Results](phase-22-asset-pipeline/day-329-benchmarks-guides-and-spec-results.md)
330. [Closing Runs on Three Machines & the Phase Review](phase-22-asset-pipeline/day-330-closing-runs-and-phase-review.md)

## Notes / open questions

- The BC7 subset (which modes and partitions) is decided by Day 313's spike, not here.
- Whether the pak gains compression (and which codec: a vendored one would be a new third-party entry) is out of this phase; the format reserves a flag.
- ASTC, PNG, glTF, a network write path, authentication and encryption are all later work; each is filed in `KNOWN-ISSUES.md` on Day 329.
- The Unreal importer (Phase 30) writes the same blob formats through the same writers; nothing in this phase links Unreal code.
- Windows and macOS: no resource-system code is platform specific except the filesystem backend's directory walk, which uses `io::Directory`; the BC formats exist on both (Metal supports BC on the Mac's GPU; arm64 and iOS devices do not, which is why ASTC is Phase 28).
- Day numbers: Phase 27 (3D text) moved from Days 313-328 to 331-346 to make room for this phase.
