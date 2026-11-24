# Maintenance / linux-known-issues / Day 01 — Terrain correctness: tiles, seams, drop order and the height map's checks

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Tile levels with tall tiles (`engine/renderer/terrain_tiles.cpp`, `TerrainSelectSettingsValid`): make the one-level-difference guarantee hold when a tile box is tall, either by adding the box's height to the split distance rule or by splitting on the horizontal distance only; correct the claim in `terrain_tiles.h`. Test: a quadtree built from block minima and maxima with a 200 m cliff, then the level difference across every shared edge of `Select`'s output is at most 1 (fails before the fix).
- [ ] Make the seam test detect a crack: `Tiles of different levels leave no hole between them` passes with `morph_fraction` 0. Replace it (or add one) that compares, along every shared edge, the heights the vertex shader computes (a CPU twin of the morph) for the fine and the coarse tile, and fails with the morph turned off. Also bring the vertex shader's morph distance and the selection's box distance to one rule, so a seam vertex cannot sit just under a morph factor of 1.
- [ ] A frame past `kMaxTerrainDrawTiles` (4,096): drop the farthest tiles by distance (not the last of the sort order) and count them in `GBufferStats` or `TerrainPass` stats instead of only a log line; test with a scene of more than 4,096 tiles that the nearest tiles are the ones kept.
- [ ] Tile boxes: pad each box by the mip filter's reach (one texel of the tile's level in height range) so a coarse tile's mesh never leaves its box; test against a ridge just outside a tile.
- [ ] Non-power-of-two maps (`engine/asset/height_map.cpp`, `terrain.vert.slang` line 75): make the coarse mips register with the level 0 samples (the uv of a mip level uses that level's width), or restrict the pyramid to sizes where it is exact; test a 1025 by 1025 map's morphed height against the true height at the tile's coarse vertices.
- [ ] An opt-in `asset::VerifyHeightMap(view)` that checks every pyramid block against the samples (the loader keeps the cheap check); the minter and `asset_mint` call it; test with a blob whose block maximum lies about a sample.
- [ ] `TerrainPass::Add` asserts `level < kMaxTerrainLevels` and the tile's range; `Recreate` returns false when `Build` fails and the registry reports it; `Add` logs why it refused (invalid height texture, too many terrains, bad layer texture).
- [ ] `asset_mint mint` (the project command) knows `.hgt.src` (Day 486 of the phase): the project file lists height maps, and `asset_mint mint-heightmap` stays as the thin command; test: a project with a height map mints the same bytes as the single command.
- [ ] A look at the real thing: run `render_features_demo --screenshot` from a low oblique camera at three distances and look at the seams and the layer blend; record what was seen in the day file (a human check, not a test).
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** Terrain's selection, seams, drop order, pyramid and passes behave as their guides say; each fix has a test shown to fail before it; the Day 488, 489, 490 and 505 entries about them are removed from `KNOWN-ISSUES.md`.

## Post-implementation code review

(To be filled in when the day is done.)
