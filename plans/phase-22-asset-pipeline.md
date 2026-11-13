# Phase 22 — Asset Pipeline & Resource System

**Status: planned (2026-11-13): day files written, Days 313-330. Nothing started; `CURRENT_PHASE` stays 20 until Day 313 (its first task). Needs from the user: confirm the benchmark pass conditions on Day 329 (planner's defaults).**

**Depends on:** Phase 17 (ECS), Phase 5 (renderer core: `MeshData`, `GpuTexture`, `MaterialSystem`), Phase 8 (`Recreatable`, the device reset), Phase 19 (storage: the component registry and entity keys the level format reuses), Phase 20 (networking: the network backend is a user of `net::TcpTransport`), Phase 9 (file layer), Phase 11 (settings), Phase 2 (jobs). Placed before physics (23) and audio (24) so both load minted assets. Phase 21 (macOS) is complete on the Mac: the resource system must build and run there too.
**Goal:** a resource system with pluggable storage backends, versioned per-type binary formats, offline tooling that produces them deterministically, and a runtime manager that loads them synchronously and asynchronously: the foundation Phase 29 (Unreal import) targets when converting external content into engine-native form. See root `PLAN.md` assumption #12 for the settled design decisions this phase implements.
**Definition of done:** an offline mint converts source assets (our own custom formats) into engine-ready blobs, packed into a pak, loadable at runtime through the filesystem, pak and network backends with identical results, with no editor or tool code linked into the game binary; minting is deterministic (same inputs and tool version give byte-identical outputs, tested for every minter, across worker counts and across the three machines); a device reset recreates every loaded resource; hostile blobs, paks and requests are refused; the benchmark table is in the spec.

## Decisions made for this plan (the user, 2026-11-12)

- **Source formats are custom only** (assumption #4): text sources for meshes, textures' parameters, materials and levels; a trivial raw image container; no PNG or glTF reader.
- **Texture compression is ours:** a deterministic BC1, BC4, BC5 and BC7-subset encoder in the minter, decoders at runtime; ASTC waits for Phase 27.
- **The network backend is read-only over `net::TcpTransport`** with a content-hash disk cache, retry with back-off and an offline mode; no authentication and no encryption (the user deferred encryption on 2026-11-12: trusted networks only).

## Key deliverables (broken into days below)

- **Resource identity and the blob container** (Day 314): `ResourceId`, `ContentHash`, a versioned blob with bounded sections and an upgrade chain.
- **Backends and mounts** (Days 315, 316, 326): filesystem (loose files, for fast iteration), pak (the shipping default), network; upper layers ask by id and never know which served it.
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
- The Unreal importer (Phase 29) writes the same blob formats through the same writers; nothing in this phase links Unreal code.
- Windows and macOS: no resource-system code is platform specific except the filesystem backend's directory walk, which uses `io::Directory`; the BC formats exist on both (Metal supports BC on the Mac's GPU; arm64 and iOS devices do not, which is why ASTC is Phase 27).
- Day numbers: Phase 26 (3D text) moved from Days 313-328 to 331-346 to make room for this phase.
