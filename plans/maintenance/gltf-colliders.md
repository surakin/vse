# Maintenance: colliders from an imported glTF, and a character that walks on them

Not a numbered phase. A host's request (`plans/maintenance/host-gltf-colliders-and-character.md`, 2026-11-20), brainstormed with the
user the same day. **Design: [docs/superpowers/specs/2026-11-20-gltf-colliders-design.md](../../docs/superpowers/specs/2026-11-20-gltf-colliders-design.md),
approved by the user on 2026-11-20.** Read it first: this index and the day files carry the plan, the design carries the reasons.

## Decisions (the user's, 2026-11-20)

1. **Both conventions**: node-name suffixes (`-col`, `-colonly`, `-convcol`, `-convcolonly`, Blender's `.NNN` ignored) and
   `OMI_physics_body` / `OMI_physics_shape`.
2. **One static collider per marked node** (approach A): one `MCOL` per glTF mesh in the mesh's own space, shared; primitives as values;
   a new level component `static_collider`; `level_spawn` makes the bodies.
3. **Colliders follow their entities**: a static body is re-placed when its entity's world position or rotation changes. The scale is
   the entity's world scale when the body is made; a later scale change is counted and ignored (Claude's proposal in the design,
   approved with it).

## Days

1. [Physics: static bodies with a parent, placed by and following the world transform](gltf-colliders/01-physics-parented-static-bodies.md)
2. [Assets: the `static_collider` level component, the `MCOL` loader, one degenerate-triangle rule](gltf-colliders/02-asset-static-collider.md)
3. [The importer: name suffixes, `MCOL` per mesh, limits and the summary](gltf-colliders/03-import-name-suffixes.md)
4. [The importer: `OMI_physics_body` and `OMI_physics_shape`](gltf-colliders/04-import-omi-physics.md)
5. [The C API: collider sources, scaled shapes and `level_spawn`](gltf-colliders/05-c-api-collider-sources.md)
6. [The C API: `ENGINE_SHAPE_MESH`](gltf-colliders/06-c-api-mesh-shape.md)
7. [The character on an imported world, hostile files, the guides and the closing runs (Linux)](gltf-colliders/07-character-and-closing.md)
8. [The PC and the Mac (the user's machines)](gltf-colliders/08-windows-macos.md)

Day 1 and Days 2-4 are independent strands; Day 5 needs Days 1-3; Day 6 needs Day 5; Day 7 needs all before it; Day 8 runs after Day 7.

**Definition of done:** an artist's `.glb` with `-col`/`-colonly` meshes or `OMI_physics_*` shapes imports with collision; `level_spawn`
makes it solid; a character lands on its ground, climbs a step of `step_height`, is stopped by a higher step, a steep slope and a wall,
and falls off an edge; moving the level root moves its colliders; a file without marks imports as before with `"colliders":0`; hostile
collision is refused or warned about with no crash under asan; two imports give equal `MCOL` bytes; a host gives a body a mesh collider
from an `MCOL` through the C API; and all of it passes on Linux, Windows and macOS.

## Rules for every day

- The usual day rules: `scripts/test-phase.sh --start-day`, the scoped runs (`--affected`, and the asan and tsan flavors for physics
  sync, spawn and cache code), `/code-review high`, `docgen lint` at zero and `docs/reference` regenerated for a changed public header,
  reference-style updates of the module's guide (the narrative waits for Day 7), `STATUS.md` and `KNOWN-ISSUES.md`, commit and push.
- **No client name** in code, comments, tests, docs or file names (describe the host's need). The request file is the host's own.
- A level or import format change bumps its version (`kGltfImporterOutputVersion` 5 on Day 3; a stored component gets its version and
  registration); minting stays deterministic; the C API grows only by appending (struct fields at the end, new kinds, the ABI snapshot
  regenerated); every hostile-input path has a test that runs under asan.
- Physics stays deterministic to the bit: visit order is entity order, no new float reductions, the physics hash tests keep passing.
- Fix `ARCHITECTURE.md`'s stale line on the way (`engine_c`'s script module "Linux x86-64 so far": Linux, Windows and macOS since Days
  471-474) on whichever day first touches that file.
