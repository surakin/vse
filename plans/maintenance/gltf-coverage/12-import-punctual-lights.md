# glTF coverage / Day 12: the importer: `KHR_lights_punctual`

**Series:** [glTF coverage](../gltf-coverage.md) (decision 7)
**Depends on:** Day 11.

## Tasks

- [x] **Settings** (`asset_import/import_settings.*`): `assets.import.light_intensity_scale` (default 1), `assets.import.sun_intensity_scale`
  (default 1/10000) and `assets.import.light_cutoff` (default 1/256), with `env`/`flag` names, read into `ImportOptions`.
- [x] **Parse** (`gltf_container.cpp`, a new `gltf_lights.cpp`): the extension joins the supported list; `extensions.KHR_lights_punctual.lights`
  (`type`, `color` default white, `intensity` default 1, `range`, `spot.innerConeAngle` default 0, `spot.outerConeAngle` default pi/4),
  checked (a negative intensity or range, a cone out of order or over pi/2 refused naming the light); a node's
  `extensions.KHR_lights_punctual.light` index checked.
- [x] **Into the level** (`gltf_level.cpp`): a node with a light gets a `light_source` piece: directional and spot point along the node's
  `-Z` (so `direction` = `+Z` in entity space), the intensity times its scale setting; `range` is the radius, or none gives the radius where
  `intensity * attenuation(d)` falls under `light_cutoff * intensity` (solved on the CPU with the engine's attenuation). A light on a node
  with a mesh shares the entity.
- [x] **Summary:** the lights (name, type, world position).
- [x] **The model viewer:** a file's own lights are used; its default light only when the file has none (or a key toggles it: decide).
- [x] **Tests** (a new `tests/asset/gltf_lights_test.cpp`, its line in `tests/phases.txt`): a glTF built in the test with each light type, a spot with default cones, a light with no range (the computed radius), a
  bad light (refused), a light required by `extensionsRequired` (no longer refused); the spawned level's `LightSource` values;
  deterministic re-import.
- [x] **Standing rows.**

## Notes

- **Where the code went:** the parse lives in `gltf_level.cpp` beside the level writer, not a new `gltf_lights.cpp`: the lights are read only
  for the level, and the file stays small. `asset_import` now depends on `scene` (the `light_source` component), a new downward edge
  (`tools/module_deps_check/modules.txt`, `ARCHITECTURE.md`).
- **Settings:** `assets.import.light_intensity_scale`, `sun_intensity_scale` and `light_cutoff` (env `ENGINE_ASSETS_IMPORT_*`, flags
  `--assets-import-*`), read by `ApplyImportLightSettings`; `asset_demo` and the C API (`ImportLimitValues` carries the three values) use them.
- **The radius without a range** solves `intensity / (d^2 + 1) = cutoff` (the point-light pass's falloff before its window), giving
  `Sqrt(intensity / cutoff - 1)`, at least 0.01.
- **Registries:** a level with an unknown component is refused, so every program that loads imported levels now registers
  `scene::RegisterSceneComponents`: the model viewer, `asset_mint` and the C API's level spawn.
- **The model viewer** (decided): the file's lights when it has any, its default sun only when it has none; the ambient term stays.
- **Not done:** a world of the C API does not take the lights over (its `LightComponent` has no spot and its own direction
  rule): in `KNOWN-ISSUES.md`. (Done since, 2026-11-20: [c-api-gltf-coverage.md](../c-api-gltf-coverage.md).) (The summary's lights were done on 2026-11-20, below.) The test is a case in
  `tests/asset/gltf_mesh_test.cpp` (no new file, so no new `tests/phases.txt` line): each light type, units, a spot's cones, a light with no
  range, determinism, six refusals. `extensionsRequired` naming the extension is covered by the supported list.
- **Tests:** not run (the user, 2026-11-19); `build-msvc` builds clean. The whole suite runs after this day.
- **Review:** kept light, as for the series since Day 2; it found the missing registrations above (a file with lights would have been
  refused by the viewer, `asset_mint` and the C API).

### The summary's lights (2026-11-20, Linux)

- `BuildImportSummary` (`asset_import/gltf_summary.cpp`) writes `"lights":[{"name","type","node","position"},...]` after `animations`:
  one entry per level node that names a light, in level order, the position under its parents times `scale`. The member is added, so
  `kImportSummaryVersion` stays 1; `kGltfImporterOutputVersion` is now 2 (the series changes what an import writes: maps, colours,
  morphs, lights, the summary), which moves the C API's format fingerprint (`tests/engine_library/host_import.c`:
  `0x3f6608f70145ded4`). Documented in `gltf_summary.h`, `include/engine/engine_c.h` and `docs/guide/assets.md`; reference regenerated.
- Tests: a new case, "The import summary lists the level's lights" (a named and an unnamed light, one under a moved parent, scale 2), and
  `"lights":[]` in the existing summary case.
- Runs (Linux, the first since Day 2): every phase plain over the series' diff, `ran 4282 of 11297 tests (phases: all; flavors: plain;
  display: included; crash tests: excluded; soak: excluded) in 77 s`; failures: the three known ones (`engine_c_surface`, the
  Job-destroyed test, the voice-cost snapshot) and `docs_check` before the reference was regenerated. After it: `ctest -L docs` 9 of 9;
  the summary and lights cases under asan 5 of 5; `engine_c_import` passes.

### Post-implementation code review (the summary's lights)

`/code-review high`: 1 finding, declined. The layout grew without raising `kImportSummaryVersion`: the member is only added, and the
importer output version moved, which changes the fingerprint every host cache is keyed by, so no old summary is kept beside new blobs;
raising the summary version would break a host that checks for 1.

**Done when:** a glTF's punctual lights arrive in the level as light sources of the right type, place, direction, colour and strength.
