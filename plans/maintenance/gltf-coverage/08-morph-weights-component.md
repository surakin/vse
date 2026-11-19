# glTF coverage / Day 8: morph weights in the ECS and in levels

**Series:** [glTF coverage](../gltf-coverage.md) (decision 5)
**Depends on:** Day 7.

## Tasks

- [ ] **The component** (`engine/scene/components.h`, `scene_components.cpp`): `MorphWeights` (`u32 count`, `Array<f32, 64> weights`),
  stored, a new stable id, registered as `morph_weights` (fields `count`, `weights`); scripts reach it through the component registry
  (mark its binding `world-read`/`world-write` as the registry does).
- [ ] **The sync** (`scene/render_sync.cpp`): an entity with `MeshRenderer` and `MorphWeights` sets the object's weights when the component
  changes (the change tracking the sync already uses); a count over the mesh's targets is clamped with a counted warning.
- [ ] **Levels** (`asset/level.*`, `scene/level_spawner.*`): a level entity may carry `morph_weights`; `level_spawn` adds it.
- [ ] **C API:** check that a host sets it through the generic component calls (`engine_c`); a test in `tests/engine_library` if a new
  path is needed.
- [ ] **Tests:** the component round-trips through storage (save and load), the sync sets the render object's weights (a frame later, as
  the sync's rule is), a script sets a weight (`tests/script/component_bindings_test.cpp`), a level with weights spawns them.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** an entity's morph weights reach its render object, and levels, saves, scripts and hosts can set them.
