# Phase 28 / Day 503 — macOS: Metal for the Phase's Features (the user's day)

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 479-502.

Build and run everything on the Mac; the code is written there.

## Tasks

- [ ] Metal cube textures (`MTLTextureTypeCube`, `replaceRegion` per face and slice, `Caps::cube_textures = true`, the `Unsupported` return removed); the RHI cube test passes with the Metal validation layer on (`MTL_DEBUG_LAYER=1 ...` as in CLAUDE.md).
- [ ] Re-mint the Metal libraries (`tools/metal_mint`) for the sky, lighting, terrain and instanced shaders (the `main` rename and the clip-space Y flip per `shaders/README.md`); `shaders_check` passes; `KNOWN-ISSUES.md` rows removed.
- [ ] Vertex-stage texture reads of the height map and the instance buffer on Metal (argument limits, `memoryless` not applicable); the terrain and instancing render tests pass validated.
- [ ] Physics determinism: the terrain scene's hash equals Linux's and the PC's on x86-64; record it.
- [ ] Build clean with Apple clang, unity on; plain tests through `scripts/test-phase.sh --affected` (no sanitizer flavors on the Mac for now, no unity-off or arm64 builds, no script demo, per the standing notes); record the `ran X of Y` line.
- [ ] Day review on the Mac-side diff; `STATUS.md` and `KNOWN-ISSUES.md` current.

**Done:** The phase's features build, run and pass on macOS with Metal validation; the physics hash matches Linux and Windows.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
