# Maintenance / linux-known-issues / Day 05 — Sky, cube textures and the shader reflection

**Series:** [Known issues Linux can fix](../linux-known-issues.md)

## Tasks

- [ ] `scripts/test-phase.sh --start-day` at the start; scoped runs (`--affected` plain, then asan and tsan when the day touches concurrency or lifetimes) at the end, quoting the script's `ran X of Y` line.
- [ ] Cube minting validates half floats: `MintTextureFromHalfPixels` (`engine/asset/texture_mint.cpp`) rejects NaN and Inf texels on the direct faces path as the equirectangular path does, and `PrefilterEnvironmentChains` stops on a failed `HalfBitsToFloat` instead of counting zero; tests in `tests/asset` with one NaN texel.
- [ ] Vulkan cube creation checks `maxImageDimensionCube` (`engine/rhi/vulkan/vulkan_device_textures.cpp`) and returns an invalid handle with a log line; a null-device test of the cap, and a note for the DX12 and Metal backends in their `BACKEND.md`.
- [ ] The demo's cube map sky is a minted asset, not generated at start (Day 504, item 1): `asset_mint` builds `forest_sky.cube` from the procedural function in the build, the demo loads it through the resource path, and the screenshot reference is re-made once.
- [ ] The demo's window and offscreen plumbing moves to a shared helper in `game/common` (about 150 lines shared with `model_viewer`); the demo's far-tree stand-in is tinted lighter than the trunk; the overlay shows the terrain tile count (`SceneFrame` exposes `TerrainStats`).
- [ ] The benchmark measures the sky pass with GPU timestamps where the device has them (`rhi` timestamp queries; the Vulkan backend) and keeps the frame-time difference as the fallback; `render_features_bench --scene sky` prints which it used.
- [ ] The shader reflection records a texture's dimension (the blob version rises and every `.refl.inc` is re-minted by `shader_mint`), so binding a 2D texture where a shader declares a cube is refused by the layout check, not only by validation; test; the day lists the files that change.
- [ ] Specular-glossiness and other maintenance leftovers that touch the sky or the lighting pass are not part of this day.
- [ ] Both compilers, unity on and off; public API documented (`docgen lint` zero, `docs` target, the guide pages); `/code-review high` on the day's diff, findings verified, fixed with a regression test or declined with a reason, recorded below.
- [ ] `KNOWN-ISSUES.md` entries removed or rewritten, `STATUS.md` updated; commit and push.

**Done:** The cube pipeline refuses bad input at the minter, the device and the layout; the demo's sky is minted; the entries for Days 479, 483, 504 and the review's sky items are removed.

## Post-implementation code review

(To be filled in when the day is done.)
