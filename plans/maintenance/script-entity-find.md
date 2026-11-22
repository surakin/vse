# Maintenance: `entity.find` and `entity.name` find nothing in a C API world

Not a numbered phase. Bug report from the a host project (2026-11-22), found while using `script-sky-and-lights.md` (thank you; `environment.set`
and `light.set` work, the lookup of the entities to drive does not).

## What happens

In a C API world (engine `51b80dc1`, Linux), a behavior calls `entity.find('Spawn')`, `entity.find('Ground-col')`, `entity.find('Sun')`,
`entity.find('SunLight')` (and with the level's path prefix, `entity.find('field/Sun')`) on every tick, for minutes: every call returns nil. The host's
own `world_find` / `entity_find` finds `Spawn` in the same world at the same time. The level was imported with `import_gltf` and spawned with
`level_spawn` (no `name_prefix`), and its nodes are named in the file (`Sun`, `Moon`, `SunLight`, `MoonLight` are a mesh and a light node).

## Why (read from the source)

`script::ScriptRuntime`'s services struct has `const SceneServices* scene = nullptr` (`engine/script/script_runtime.h:172`), the table that
`entity.name` and `entity.find` read (`engine/script/entity_bindings.h`: `name_of`, `find`, `has_prefab`). Nothing in `engine/engine_c` assigns it
(`grep SceneServices engine/engine_c`: only the unrelated `SceneScriptServices` of `script_scene.cpp`), so both calls return nil in every C API world,
whatever the host named. `host_script_scene.c` only calls `entity.find('nothing') or self.me`, which passes either way.

The guide's "Finding the lights" bullet (`docs/guide/scripting.md`) says `entity.find` finds a host-named entity and an imported level's node; that
cannot hold until the table is set.

## Wanted

1. `engine_c` fills `SceneServices` for its worlds: `name_of` and `find` over the world's entity names (the ones `entity_name_set` writes and `level_spawn` gives
   its nodes, with the `name_prefix` + `/` rule), `has_prefab` as you see fit. Names of entities spawned in the same merge need not be visible before it.
2. A test that would have caught this: a script finds a level node by name and by a host-set name, and moves it (`transform.set_position`), and the host
   reads the moved position back through the C API; and `entity.name` of that entity returns the name.

## What The host does with it

`daylight.lua` finds `SunLight`, `MoonLight`, `Sun` and `Moon` once the level is spawned and moves them every tick (its own tick-by-tick `find` until all
four exist). Nothing else changes in The host when this lands.

## Fixed (2026-11-22, Linux; no day files)

The report was right: `engine_c` never set the runtime's `SceneServices`, so `entity.find` and `entity.name` returned nil in every C API world, and the
claim in the sky-and-lights guide bullet was never tested. Now `engine_c` fills it (`SceneNames` in `engine/engine_c/script_scene.*`, set as
`ScriptRuntimeConfig::scene`): `name_of` reads the world's `EntityName` component (moved to `runtime.h`), `find` queries it under a mutex (a query is not made by
two partitions at once). Names are the ones `entity_name_set` and `level_spawn` write (`name_prefix` + `/` + node name). `has_prefab` stays unset (no prefabs
in a C API world). Test: `host_script_scene` now has a script find a level node and a host-named entity, move both with `transform.set_position`, read
`entity.name`, and the host reads the positions back. The test was written after the fix; the failing state is the report's own, not re-run.
