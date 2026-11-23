# Phase 28 / Day 502 — Windows: Direct3D 12 for the Phase's Features (the user's day)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-501.

Build and run everything on the Windows PC; the code is written there.

## Tasks

- [ ] Direct3D 12 cube textures: `TextureCube` shader resource views over a 6-slice array, the cube-compatible creation, `Caps::cube_textures = true`, the `Unsupported` return from Day 479 removed; the RHI cube test passes on D3D12 and on Vulkan on the PC.
- [ ] Re-mint the DXIL for `sky.vert/frag`, the changed lighting shaders, `terrain.vert/frag` and the instanced G-buffer variants (the PC mints DXIL; `shaders_check` passes); `KNOWN-ISSUES.md` rows for the stale DXIL are removed.
- [ ] Vertex-stage texture fetch of the height map on D3D12 (a fixed sampler heap slot for the vertex stage if the backend needs one) and the instance buffer's instance-rate index capability path: the terrain and instancing render tests pass.
- [ ] Physics determinism: the terrain reference scene's state hash equals Linux's (Day 492); record the value; an inequality is a finding, not a tolerance.
- [ ] Build clean with MSVC and clang-cl, unity on and off; `scripts/test-phase.sh --affected` on the PC (plain and asan); record the `ran X of Y` line; `ctest -L docs`.
- [ ] Day review (`/code-review high`) on the PC-side diff; `STATUS.md` and `KNOWN-ISSUES.md` current.

**Done:** The phase's features build, run and pass on Windows with Vulkan and Direct3D 12; the physics hash matches Linux.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
