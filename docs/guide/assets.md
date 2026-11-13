# Assets and resources: identity, blobs, backends and the resource manager

Status: Phase 22. Built: the module skeleton and the container version constant (Day 313). Next: resource identity and the blob container (Day 314).

**Audience:** engine and game programmers who load meshes, textures, materials and levels. Design, spikes and the benchmark pass conditions:
`docs/superpowers/specs/2026-11-13-asset-pipeline-design.md`.

The module `engine/asset` (namespace `asset`) loads minted assets. Three rules shape it (the user, 2026-11-13):

- **Loading performance is paramount.** A load is a read into a pre-faulted, aligned buffer and a bounded validation; nothing is copied between the read and the GPU upload.
- **Load-time conversions are discouraged.** The loaders never repack, re-order or byte-swap; the mint writes what the GPU and the CPU want.
- **Upgrades are enforced offline.** The runtime reads the current version only (`asset::kContainerVersion`) and refuses an older asset with a message naming
  `asset_mint remint`. The upgrade chain lives in `tools/asset_mint`.

Minting is deterministic and the offline tools are called mints (`tools/asset_mint`); "cook" is Unreal's word.
