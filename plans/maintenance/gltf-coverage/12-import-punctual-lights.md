# glTF coverage / Day 12: the importer: `KHR_lights_punctual`

**Series:** [glTF coverage](../gltf-coverage.md) (decision 7)
**Depends on:** Day 11.

## Tasks

- [ ] **Settings** (`asset_import/import_settings.*`): `assets.import.light_intensity_scale` (default 1), `assets.import.sun_intensity_scale`
  (default 1/10000) and `assets.import.light_cutoff` (default 1/256), with `env`/`flag` names, read into `ImportOptions`.
- [ ] **Parse** (`gltf_container.cpp`, a new `gltf_lights.cpp`): the extension joins the supported list; `extensions.KHR_lights_punctual.lights`
  (`type`, `color` default white, `intensity` default 1, `range`, `spot.innerConeAngle` default 0, `spot.outerConeAngle` default pi/4),
  checked (a negative intensity or range, a cone out of order or over pi/2 refused naming the light); a node's
  `extensions.KHR_lights_punctual.light` index checked.
- [ ] **Into the level** (`gltf_level.cpp`): a node with a light gets a `light_source` piece: directional and spot point along the node's
  `-Z` (so `direction` = `+Z` in entity space), the intensity times its scale setting; `range` is the radius, or none gives the radius where
  `intensity * attenuation(d)` falls under `light_cutoff * intensity` (solved on the CPU with the engine's attenuation). A light on a node
  with a mesh shares the entity.
- [ ] **Summary:** the lights (name, type, world position).
- [ ] **The model viewer:** a file's own lights are used; its default light only when the file has none (or a key toggles it: decide).
- [ ] **Tests** (a new `tests/asset/gltf_lights_test.cpp`, its line in `tests/phases.txt`): a glTF built in the test with each light type, a spot with default cones, a light with no range (the computed radius), a
  bad light (refused), a light required by `extensionsRequired` (no longer refused); the spawned level's `LightSource` values;
  deterministic re-import.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a glTF's punctual lights arrive in the level as light sources of the right type, place, direction, colour and strength.
