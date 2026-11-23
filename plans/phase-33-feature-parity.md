# Phase 33+ — Feature-Parity Push

**Depends on:** a working engine core and at least one real demo (Phase
29) to ground priorities against real usage rather than a feature
checklist copied from Unreal.
**Goal:** ongoing work toward matching/exceeding Unreal Engine's
capability set.
**Definition of done:** not a single milestone — this phase is scoped in
detail only once reached, against real priorities at that time.

## Candidate areas (not yet scoped — placeholders for future planning)

- Advanced rendering (the sky box, cube maps and reflections are Phase 28's;
  the lighting pass has no shadows yet): shadow mapping, global illumination, virtualized geometry/textures
  (Nanite/Lumen-class features), advanced shadowing.
- World streaming / world partition for large open worlds.
- Replication, prediction and matchmaking on top of Phase 20's transport layer (the sockets, TCP/UDP and the transport interface are Phase 20's).
- Animation systems (blend trees, IK, state machines, retargeting, compute
  pre-skinning) beyond Phase 22's skeleton and clip formats, sampler,
  blending and GPU skinning. (Morph targets are built: the glTF coverage
  series, `MESH` v3 and the GPU morph pass.)
- Terrain beyond Phase 28's height-map terrain (editing, streaming of tiles,
  holes, virtual texturing) and foliage systems beyond its instanced and
  hierarchical instanced meshes (wind, scattering tools, impostors).
- Niagara-equivalent particle/VFX system.
- Further Unreal import coverage: latent Blueprint nodes, Sequencer/
  cinematics, cooked-game import if ever justified.

## Notes / open questions

- Checked 2026-11-23 against what is built: morph targets, terrain, sky and
  instancing (Phase 28) are out of the list or narrowed; replication,
  particles, world streaming, shadows and the rest are not built (`engine/net`
  has delta writers but no replication; `storage` has `Merge` as the
  primitive for cell loading).
- Deliberately left unscoped. Break this into real phase-NN files (34,
  35, ...) once Phase 29 is complete and it's clear what the engine
  actually needs next.
