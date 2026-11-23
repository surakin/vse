# Maintenance: double precision, Day 9 — Assets, levels and the importers

**Series:** [double precision](../double-precision.md)

## Tasks

- [ ] The level format's entity transforms (`LevelSummary`, `level_spawner`) hold `f64` translations; the level blob version moves; levels are re-minted offline, never converted at load.
- [ ] `asset_mint` and the glTF importer (`engine/asset_import`): node transforms accumulate in `f64`, vertex data stays `f32`; mesh blobs are unchanged (object space); the minted-level tests and the importer's golden outputs are regenerated once and the byte changes recorded.
- [ ] Terrain: the height map's own space stays `f32` heights; the terrain placement (a translation) is `f64`, `TerrainWorldValid` and `TerrainHeight` take `f64` coordinates; the sky has no position.
- [ ] The import format fingerprint (`engine_api.import_format_version`) changes and `host_import.c`'s expected value is updated in the same commit, as the other format moves did.
- [ ] Docs for the level and mint formats.
- [ ] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A level placed 1,000 km away loads, spawns and renders exactly.

## Notes

- Written 2026-11-23.

## Post-implementation code review

(To be filled in when the day is done.)
