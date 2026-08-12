# Phase 8 — Unreal Asset & Level Import

**Depends on:** Phase 7 (Asset pipeline) — this importer's job is to
produce Phase 7's engine-native formats from Unreal source data. Also
benefits from Phase 3 (Renderer) and Phase 4 (ECS) being real, since the
definition of done requires an imported level to actually render.
**Goal:** load geometry, textures, materials, and level layout authored in
a real Unreal Engine project directly into this engine — no Epic tools,
no Epic source, no cooked/shipped-game formats.
**Definition of done:** point the importer at a real, unmodified UE 5.3+
project's `Content/` folder; it produces engine-native assets (Phase 7
formats) for that project's static meshes, textures, materials
(best-effort), and at least one level, which then load and render
correctly in this engine.

This is one of the project's headline objectives (see root `PLAN.md`) —
treat it as a first-class track, not a footnote of Phase 7.

## Why this is hard (read before estimating)

Unreal serializes everything into `.uasset` (+ companion `.uexp`/`.ubulk`
for larger payloads) using an internal binary format that has changed
across engine versions. We target **uncooked, editor-format assets from a
real project** — not a shipped game's obfuscated IoStore runtime chunks,
which is a different and out-of-scope problem (see root `PLAN.md`
assumption #5). The container format itself (package summary, name table,
import/export tables, tagged property serialization, including
"unversioned" properties since UE 4.25) is public knowledge via community
reverse-engineering (UAssetAPI, FModel, CUE4Parse docs) — we build our own
parser from that documented knowledge, using none of their code (root
`PLAN.md` assumption #6).

## Sub-tracks (to be expanded into day-by-day tasks when started)

1. **Container parser.** `.uasset`/`.uexp`/`.ubulk` package summary, name
   table, import table, export table, and the tagged property system
   (property bag schema resolution for unversioned properties).
2. **Static mesh importer.** Decode a `UStaticMesh` export's serialized
   vertex/index buffers (positions, normals, tangents, UVs, LOD levels)
   into the Phase 7 mesh format.
3. **Skeletal mesh & skeleton importer.** Bones, bind pose, skin weights.
4. **Texture importer.** Decode `UTexture2D` GPU-native compressed
   payloads (BCn/ASTC — hardware formats, not a library per assumption #4)
   straight into the Phase 7 texture format.
5. **Material importer.** Unreal materials are node graphs
   (`UMaterial`/`UMaterialExpression*`). Full-fidelity translation of an
   arbitrary graph is a large effort on its own; scope is a best-effort
   translator covering common nodes (texture sample, constant, math ops,
   multiply/add/lerp, scalar/vector params), with unsupported nodes
   producing a clearly-flagged placeholder material rather than a failed
   import.
6. **Level/world importer.** Parse `.umap` packages (same container format
   as above) into actor placement lists (transform, mesh reference,
   component data) and translate into Phase 4 ECS entities.

## Explicitly out of scope for this phase

Blueprint gameplay logic (Phase 10), Landscape/terrain, foliage systems,
Niagara/particle systems, Sequencer/cinematics, and cooked/shipped-game
formats. Each of these would be its own follow-on import track, scoped
against real demand once the core import path works.

## Notes / open questions

- Target Unreal version pinned at 5.3+ for now (root `PLAN.md` assumption
  #7) — revisit if an older/newer target becomes more useful.
- Full day-by-day breakdown deferred until Phase 7 is complete; this
  phase's sub-tracks are large enough that it may itself get split further
  once real `.uasset` samples are being parsed and the actual complexity
  is visible.
