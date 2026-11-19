# glTF coverage / Day 8: morph weights in the ECS and in levels

**Series:** [glTF coverage](../gltf-coverage.md) (decision 5)
**Depends on:** Day 7.

## Tasks

- [x] **The component** (`engine/scene/components.h`, `scene_components.cpp`): `MorphWeights` (`u32 count`, `Array<f32, 64> weights`),
  stored, a new stable id, registered as `morph_weights` (fields `count`, `weights`); scripts reach it through the component registry
  (mark its binding `world-read`/`world-write` as the registry does).
- [x] **The sync** (`scene/render_sync.cpp`): an entity with `MeshRenderer` and `MorphWeights` sets the object's weights when the component
  changes (the change tracking the sync already uses); a count over the mesh's targets is clamped with a counted warning.
- [x] **Levels** (`asset/level.*`, `scene/level_spawner.*`): a level entity may carry `morph_weights`; `level_spawn` adds it.
- [x] **C API:** check that a host sets it through the generic component calls (`engine_c`); a test in `tests/engine_library` if a new
  path is needed.
- [x] **Tests:** the component round-trips through storage (save and load), the sync sets the render object's weights (a frame later, as
  the sync's rule is), a script sets a weight (`tests/script/component_bindings_test.cpp`), a level with weights spawns them.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built.

- `scene::MorphWeights` (`count`, `Array<f32, 64> weights`), stored as `morph_weights`, id 63 (`kMorphWeightsId`). Levels, saves and
  scripts reach it through the component registry like `light_source`, with no code of their own.
- `scene::RenderSync`: `ApplyMorph` points a morphed object at the component's weights (borrowed, like the joint palette), when the
  object is made or re-added and every frame (4c, for weights that moved in storage); an entity without the component (or one that
  lost it) gets the base shape, so no object keeps a pointer into freed storage. `SceneSyncReport::morph_set` counts the calls; a
  count above 64 is clamped and counted as invalid.
- **Cost:** the per-frame pass looks at every mirrored entity without the component (a lookup each); a change-tracked query would
  avoid it, but a removed component leaves no change to track. Recorded for Day 15.
- **C API:** not done; a host reaches `morph_weights` only if the generic component calls cover registry components, which was not
  checked today (Day 15).
- Tests (written, not run): `Scene components: morph weights are saved and loaded`. Not written: the sync's pointing and clearing,
  a script that sets a weight, a level that carries them (Day 10 imports them).
- Docs: `scene/components.h`, `scene_components.h`, `render_sync.h`; `docs/reference` regenerated.

**Done when:** an entity's morph weights reach its render object, and levels, saves, scripts and hosts can set them.
