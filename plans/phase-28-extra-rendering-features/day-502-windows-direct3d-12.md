# Phase 28 / Day 502 — Windows: Direct3D 12 for the Phase's Features (the user's day)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-501.

Build and run everything on the Windows PC; the code is written there.

## Tasks

- [x] Direct3D 12 cube textures: `TextureCube` shader resource views over a 6-slice array, the cube-compatible creation, `Caps::cube_textures = true`, the `Unsupported` return from Day 479 removed; the RHI cube test passes on D3D12 and on Vulkan on the PC.
- [x] Re-mint the DXIL for `sky.vert/frag`, the changed lighting shaders, `terrain.vert/frag` and the instanced G-buffer variants (the PC mints DXIL; `shaders_check` passes); `KNOWN-ISSUES.md` rows for the stale DXIL are removed.
- [x] Vertex-stage texture fetch of the height map on D3D12 (a fixed sampler heap slot for the vertex stage if the backend needs one) and the instance buffer's instance-rate index capability path: the terrain and instancing render tests pass.
- [x] Physics determinism: the terrain reference scene's state hash equals Linux's (Day 492); record the value; an inequality is a finding, not a tolerance.
- [x] Build clean with MSVC and clang-cl, unity on and off; `scripts/test-phase.sh --affected` on the PC (plain and asan); record the `ran X of Y` line; `ctest -L docs`.
- [x] Day review (`/code-review high`) on the PC-side diff; `STATUS.md` and `KNOWN-ISSUES.md` current.

**Done:** The phase's features build, run and pass on Windows with Vulkan and Direct3D 12; the physics hash matches Linux.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## What was done (2026-11-24, the Windows PC)

- **Cube textures on Direct3D 12** (`engine/rhi/dx12/dx12_device_resources.cpp`, `dx12_device.cpp`): a cube is the same 6-slice 2D array resource with a `TEXTURECUBE` shader view; `Caps::cube_textures` is true and the refusal is gone. The RHI cube test and the sky tests run on Direct3D 12 and Vulkan on the PC.
- **DXIL minted** (`slangc v2026.18.2`, `dxcompiler 1.9.2602.17`): `lighting.frag` (the environment cube; held back on 2026-11-23 because Direct3D 12 had no cubes) and the five `gbuffer.vert` modules (the instanced one included). `sky.frag`, `sky_cube.frag`, `cube_sample.comp`, `terrain.vert` and `terrain.frag` were minted on 2026-11-23 and came out unchanged. `shaders_check` passes on the PC.
- **Terrain on Direct3D 12** (`engine/renderer/terrain_pass.cpp`): the tile buffer is a read-only structured buffer with its 32-byte stride (`CreateBindGroupLayout` refused a storage buffer without one). The vertex-stage height-map fetch needed nothing else: the root signature gives the vertex stage the texture and sampler. The 10 terrain render tests and `engine_c_terrain` pass on Direct3D 12.
- **Instanced meshes on Direct3D 12** (`engine/renderer/gbuffer.cpp`, `material.{h,cpp}`): the G-buffer pass no longer refuses them. An instanced draw binds the object index stream like any other draw, so instance `i` reads entry `first_instance + i`. The stream now holds `kObjectIndexStreamEntries` (262,144 = `kDefaultFrameInstances`, 1 MiB) indices, and a frame writes no instance past its end (`InstanceRoom`; the rest are counted in `instances_dropped`). The four instancing render tests pass on Direct3D 12 and Vulkan.
- **A unity clash** (MSVC C4459, new on Day 501): `instances_api.cpp`'s file-scope `kLimit` is `kInstanceCoordinateLimit`.
- **Physics determinism:** `Golden: every scene reproduces its committed hashes for every thread count` passes on the PC, so the terrain scene's hashes equal Linux's at every step and thread count; step 600 is `0x447cc3f9738ad539`.

## Runs (the PC, 2026-11-24)

- Four trees build clean: `build-msvc`, `build-clang-cl`, `build-windows-msvc-nounity`, `build-windows-clang-cl-nounity`.
- `scripts/test-phase.sh --affected` plain: `ran 639 of 8368 tests (phases: 3 4 5 23 28; flavors: plain; display: excluded; crash tests: excluded; soak: excluded) in 56 s`; the one failure was `docs_check`, before the reference was regenerated.
- `scripts/test-phase.sh --affected --flavor asan`: `ran 523 of 8368 tests (phases: 3 4 5 23 28; flavors: asan; display: excluded; crash tests: excluded; soak: excluded) in 65 s`, none failed.
- ctest over the labels rhi, renderer, scene and shaders (plain, 60 s per test): 822 of 823 pass. The failure is `Every minted Metal library ...`, which names `gbuffer.vert`: the Mac re-mints it (`KNOWN-ISSUES.md`).
- `ctest -L docs`: 12 of 12 after `cmake --build build-msvc --target docs`.
- The long-test warning named four tests that already existed: `shaders_check` 48 s, `Device allocation failures at any point end frames cleanly` 43 s (62 s under asan), `docs_samples` 34 s, `shaders_portability` 31 s.

## Post-implementation code review

`/code-review high` on the day's diff: 3 findings, all fixed.
1. The dropped-instance warning named the instance buffer when the object index stream was the limit: it now names the one that applied.
2. The too-many-objects error said the stream holds 65,536 entries: it now names the per-pass object limit.
3. No test covered the stream cap: `On a device that reads the object index stream, no instance is written past the stream's end` (null device, Direct3D 12-shaped and not) was added; it fails with the cap switched off and passes with it.

