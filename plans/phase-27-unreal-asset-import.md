# Phase 27 — Unreal Asset & Level Import

**Depends on:** Phase 22 (Asset pipeline) — this importer's job is to
produce Phase 22's engine-native formats from Unreal source data. Also
benefits from Phase 5 (Renderer) and Phase 17 (ECS) being real, since the
definition of done requires an imported level to actually render.
Deliberately sequenced after Phases 13, 24, 25 and 26 (Windows/macOS/iOS/Android/
console ports) rather than right after Phase 22 — Unreal import starts
once the engine has viable multiplatform support, not before.
**Goal:** load geometry, textures, materials, and level layout authored in
a real Unreal Engine project directly into this engine — no Epic editor
tools, no cooked/shipped-game formats. The offline import tool links a
DLL built from Unreal Engine's own source to do the actual parsing (root
`PLAN.md` assumption #6) — that dependency is scoped to this tool only
and never reaches the shipped engine/game runtime.
**Definition of done:** point the importer at a real, unmodified UE 5.3+
project's `Content/` folder; it produces engine-native assets (Phase 22
formats) for that project's static meshes, textures, materials
(best-effort), and at least one level, which then load and render
correctly in this engine.

This is one of the project's headline objectives (see root `PLAN.md`) —
treat it as a first-class track, not a footnote of Phase 22.

## BLOCKING — Epic UE EULA review (do before any other work here)

Root `PLAN.md` assumption #6 flags this as an unresolved open risk:
linking Unreal Engine's own source/binaries is governed by Epic's UE
EULA, a source-available license with its own rules about use and
redistribution — not permissively-licensed open source. Given this
project's stated mission is to build a *competing* engine, don't assume
building/linking a UE-derived DLL is fine by default. Review the actual
EULA terms and get an explicit go/no-go before writing any code in this
phase, including the DLL build spike below.

## Why this is still hard (read before estimating)

Linking Unreal's own code sidesteps reimplementing the `.uasset`/`.umap`
binary format by hand — correctness comes from the source, not
reverse-engineering guesswork. What's left is real work of a different
kind:

- **Building a minimal, headless UE DLL.** Unreal's full engine/editor is
  enormous; the goal is the smallest module set that can load packages
  and give us `UObject` data (likely `Core`, `CoreUObject`, and enough of
  `Engine`/`AssetRegistry` to resolve asset classes) without pulling in
  rendering, editor UI, or a full engine startup. Scope unknown until
  attempted — this is a build spike, not a known quantity.
- **Version matching.** The DLL must be built from a UE version compatible
  with a given target project (5.3+, per assumption #7). Whether one DLL
  build handles the whole 5.3+ range or multiple builds are needed per
  major version is unknown until real projects are tested against it.
- **Translating UE's live `UObject` data into Phase 22's formats** still
  requires understanding `UStaticMesh`/`UTexture2D`/`UMaterial`/`UWorld`
  data shapes — the parsing is solved, the mapping to our own mesh/
  texture/material/level formats is not.
- We still target **uncooked, editor-format assets from a real project**
  — not a shipped game's obfuscated IoStore runtime chunks, a different
  and out-of-scope problem (root `PLAN.md` assumption #5).

## Sub-tracks (to be expanded into day-by-day tasks when started)

1. **Unreal DLL build & embedding.** Build the minimal UE module set as a
   DLL our offline import tool links against; drive it headlessly to load
   a target project's `Content/` folder through UE's own package-loading
   system. Establish the toolchain for this before anything else in the
   phase.
2. **Static mesh export.** Read a loaded `UStaticMesh` object's
   vertex/index buffers (positions, normals, tangents, UVs, LOD levels)
   via UE's own reflection/data access and write into the Phase 22 mesh
   format.
3. **Skeletal mesh & skeleton export.** Bones, bind pose, skin weights,
   read the same way.
4. **Texture export.** Read `UTexture2D` GPU-native compressed payloads
   (BCn/ASTC — hardware formats, not a library, per assumption #4)
   straight into the Phase 22 texture format.
5. **Material export.** Unreal materials are node graphs
   (`UMaterial`/`UMaterialExpression*`), now walked as live loaded
   objects via UE's reflection rather than parsed from raw bytes. Scope
   is still a best-effort translator covering common nodes (texture
   sample, constant, math ops, multiply/add/lerp, scalar/vector params),
   with unsupported nodes producing a clearly-flagged placeholder
   material rather than a failed import.
6. **Level/world export.** Read a loaded `.umap`'s `UWorld`/`AActor`
   placement data (transform, mesh reference, component data) via UE's
   own objects and translate into Phase 17 ECS entities.

## Explicitly out of scope for this phase

Blueprint gameplay logic (Phase 28), Landscape/terrain, foliage systems,
Niagara/particle systems, Sequencer/cinematics, and cooked/shipped-game
formats. Each of these would be its own follow-on import track, scoped
against real demand once the core import path works.

## Notes / open questions

- **EULA review is a hard blocker — see above.** Nothing else in this
  phase should start until it's resolved.
- Target Unreal version pinned at 5.3+ for now (root `PLAN.md` assumption
  #7) — revisit if an older/newer target becomes more useful.
- Exact minimal UE module set needed for a headless DLL is unknown until
  the Day 1 build spike is attempted.
- Full day-by-day breakdown deferred until Phase 22 is complete; this
  phase's sub-tracks are large enough that it may itself get split further
  once the DLL embedding is working and the actual complexity is visible.
