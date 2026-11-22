# Maintenance: a script sets the sky and the lights of its world

Not a numbered phase, no day files (the user, 2026-11-22). Request from a host project (2026-11-22): a daylight cycle written as a Lua behavior needs to change the
environment and the lights of its world, which only the C API could. Done on Linux 2026-11-22.

## Decisions

1. `environment.set(fields)`: any subset of `ambient_color`, `ambient_intensity`, `sky_color`, `exposure`; the rest is kept; errors name the field; a global
   merge command applied at the sync point (every surface from its next frame). Like `world_set_environment`, it removes the default sun.
2. `environment.get()` (world-read, as of the tick's start) is added so a script can read what it changes.
3. `light.set(entity, fields)`: `color`, `intensity`, `radius`, `inner_cone`, `outer_cone` on an entity that has a light (a script error otherwise);
   the C API's limits are checked at the call and again at the merge.
4. `light.create(entity, kind, fields)` is included (`directional`, `point`, `spot`); it accepts an entity spawned in the same callback.
5. Ordering: the frame holds the world's mutex while scripts and the merge run, and the host's `world_set_environment` takes it between frames: the later
   write in time wins, and the script's call keeps the host's other fields.
6. `entity.find` finds host-named entities (`entity_name_set`) and imported levels' entities by name (prefix + `/` + node name when a `name_prefix` was
   given); a `KHR_lights_punctual` light is a light component on its node's entity.
7. `time.real` stays; no `time.utc`.
8. Bindings live in `engine/engine_c/script_scene.*` (the world's data is `engine_c`'s), reaching the world through a new service slot `kServiceScene`.

## Done

- [x] bindings and command kinds; wiring in `script_api.cpp`
- [x] C API test `tests/engine_library/host_script_scene.c` (state, subset, errors, ordering with the host's call, picture of an off-screen surface)
- [x] guide section in `docs/guide/scripting.md`
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
