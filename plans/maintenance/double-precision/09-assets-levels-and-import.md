# Maintenance: double precision, Day 9 — Assets, levels and the importers

**Series:** [double precision](../double-precision.md)

## Tasks

- [x] The level format's entity transforms (`LevelSummary`, `level_spawner`) hold `f64` translations; the level blob version moves; levels are re-minted offline, never converted at load.
- [x] `asset_mint` and the glTF importer (`engine/asset_import`): node transforms accumulate in `f64`, vertex data stays `f32`; mesh blobs are unchanged (object space); the minted-level tests and the importer's golden outputs are regenerated once and the byte changes recorded.
- [x] Terrain: the height map's own space stays `f32` heights; the terrain placement (a translation) is `f64`, `TerrainWorldValid` and `TerrainHeight` take `f64` coordinates; the sky has no position.
- [x] The import format fingerprint (`engine_api.import_format_version`) changes and `host_import.c`'s expected value is updated in the same commit, as the other format moves did.
- [x] Docs for the level and mint formats.
- [x] **Runs and review (the series' rules):** scoped tests plain, asan and tsan as the day's code needs; docs gates; `/code-review high`; `STATUS.md` and `KNOWN-ISSUES.md`; commit and push.

**Done:** A level placed 1,000 km away loads, spawns and renders exactly.

## Notes

- Written 2026-11-23.
- Day 1: only the level type version and placements change; mesh, skeleton, clip, texture and height-map blobs are object-space and stay; `import_format_version` moves with the level version.

## Post-implementation code review

`/code-review high` over the day's diff (own reading; the diff is small: the importer's translation, two version numbers, tests, docs): no findings. Notes: the level's entity positions already travel as the `local_transform` component at version 2 (Day 3); this day moved the importer's node translations to `f64` (bounded by 1e12 m, the world limit), the level type to version 2 and `kGltfImporterOutputVersion` to 6; `import_format_version` changed to 0x67e1da735a401f4b and `host_import.c` was updated. Terrain: heights stay `f32`, the placement is the entity's `WorldTransform` (render-relative since Day 4) and physics `TerrainHeight` takes `f64` (Day 6); the C API's `f32` terrain entry points are Day 10. Level blobs are re-minted, never converted; no committed level blobs needed regenerating (the tests mint from text). New tests: `A level placed far from the origin keeps its positions exactly`, `A level keeps a node's translation in double precision`.

Runs: plain `ran 842 of 12041 tests (phases: 3 22 23 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 25 s`, asan `ran 724 of 12041 tests (... asan ...) in 17 s`; no failure (the shared-library tests are skipped on this build). Reduced runs; no concurrency code, so no tsan. GCC unity-off builds clean; `docgen lint` 14191 of 14191; `ctest -L docs` 13 of 13.
