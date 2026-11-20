# Maintenance: the physics and character script bindings in a C API world

Not a numbered phase. Request from the host project (2026-11-20), the follow-up of `host-gltf-colliders-and-character.md` (done; thank you).
Read from `include/engine/engine_c.h`, `engine/engine_c/script_api.cpp` and `runtime.cpp`, `engine/script/standard_bindings.cpp`,
`engine/script_physics/` and `docs/reference/script-api.md`.

## Why

The local player of the host's 3D call room is a Lua behavior (`local_player`, attached to the camera entity through `ENGINE_COMPONENT_SCRIPT`) that owns walking,
turning and the camera. It now needs gravity, ground contact and a jump. `docs/reference/script-api.md` has exactly that, as `character.create`, `character.move`,
`character.jump`, `character.is_grounded`, `character.vertical_speed` and `physics.*`. A script run through the C API cannot call them.

## What is missing

`engine/engine_c` registers `script::RegisterStandardBindings` only (`script_api.cpp:584` and `:923`); nothing in `engine_c` links or names `script_physics`
(`CharacterSet`, `PhysicsScriptServices`, `RegisterPhysicsBindings` and the rest of `physics_bindings`). The standard set has no `character.*` and no `physics.*`, so
a C API world's script gets no such function, although the same world has a physics world, the character component (`ENGINE_COMPONENT_CHARACTER`,
`character_move`, `character_jump`) and gravity. The documented Lua API and the C API's world do not meet. (Please check this reading: I only searched for the names.)

## Wanted

1. **A C API world with ENGINE_CAP_SCRIPT also gets the physics script bindings** (`physics.*`, `character.*`), wired to that world's own physics world and
   character set, so a behavior can create a character, move it, jump it and read whether it is grounded. If the engine would rather make it opt-in
   (a world flag or a `script_set_*` call), say which; the host can call it.
2. **One character, two doors.** A character made from Lua and one made from the C API component (`ENGINE_COMPONENT_CHARACTER`) must not fight: say whether they
   are the same object (one `CharacterSet`, or the `FollowCharacters` list in `runtime.cpp`), and that the entity's transform follows the capsule's centre in both.
   A script-made character should be written back to the entity's `LocalTransform` after each step like the C API's.
3. **Timing.** A script runs once per frame, physics in fixed steps (`physics.step_hz`, `max_substeps`). Say how `character.move` (a velocity that holds until set again)
   and `character.jump` (applied at the start of the next step) behave when a frame makes zero, one or several steps, and whether the camera sees an interpolated position
   (`physics.interpolate`) or the last step's. First-person camera smoothness depends on it.
4. **Teleport.** A way to put a character somewhere else without destroying and making it again (for example `character.set_position`, or a documented rule that
   `transform.set_position` on a character's entity does it), because a player is teleported to a spawn and its vertical speed must reset.
5. **A script-spawned entity** (`entity.spawn(nil)` in `on_create`) can get a character in the same tick or the next; say which.

## What the host does with it

The behavior spawns a body entity (a capsule about 1.2 m tall and 0.25 m in radius: the world is robot-sized, eyes at 1.06 m), calls `character.move` each tick with the
walking velocity, `character.jump` when Space is held and `character.is_grounded`, and puts the camera at the body's position plus the eye offset. Walls at the edge of the
world are `character.move` velocity components the script zeroes; a fall below the world resets the character. Nothing else changes for the host.

## Not wanted

- Any Matrix or client concept in the engine.
- New physics features: the existing `character` is enough (gravity, steps, slopes, a jump speed).
- A second character API: the Lua calls already exist.

## Tests this needs

- A C API world with a script that makes a character on a spawned entity over an imported ground collider (the glTF colliders work), moves it, jumps it: it lands, `is_grounded` flips false then true,
  and the apex height matches `v^2 / (2 g)` for the jump speed.
- The same character read through the C API (`character_jump` with speed 0 returns the state) agrees with what the script saw.
- A frame that makes two fixed steps and one that makes none: the walked distance over a second is the same.
- A teleport (item 4) resets the vertical speed and puts the capsule at the new place.
- Asan and tsan like the other script tests; the bindings only appear in a world that has the script capability.

## Answer from the engine (2026-11-20, built on Linux; the PC and the Mac follow)

Your reading was right. It is built (`plans/maintenance/c-api-script-physics.md`); the answers to each item:

1. **Bindings:** every world with `ENGINE_CAP_SCRIPT` has `physics.*` and `character.*`, with no flag, on that world's physics world. Layer names come from the
   `script.physics_layers` setting (`default` if unset). The step's contacts and triggers reach `on_collision_*` / `on_trigger_*` at the next tick.
2. **One character, two doors:** one `CharacterSet` per world. A character made by `character.create` and one made by `ENGINE_COMPONENT_CHARACTER` are the same
   object: `character_jump(world, key, 0, &state)` reads a script-made one, and `component_has` / queries see it. An entity has at most one. After the frame's
   steps the capsule's centre is written to the entity's `LocalTransform`, whichever door made it.
3. **Timing:** a frame runs the script tick (its commands merge), then 0, 1 or n fixed steps, then the write-back. `character.move` holds and is carried out
   once per step, so distance per second does not depend on the frame rate (tested: 1.4750 m at 30, 60 and 120 fps). A frame without a step moves nothing.
   `character.jump` starts at the next step and waits for one. The camera sees the **interpolated** position when `physics.interpolate` is on (the default);
   `is_grounded` and `vertical_speed` are the last step's. **Behaviour change for C hosts:** `character_move` is now a held move too (`dt` is unused).
4. **Teleport:** `character.set_position(e, vec3)` resets the vertical speed and the ground and keeps the held move. `transform.set_position` on a character's
   entity does not move it (the write-back replaces it).
5. **Spawned entity:** `entity.spawn(nil)` then `character.create(body, ...)` works in the **same** callback, and `character.exists` is true from the next
   tick. Caveat: the handle `entity.spawn` returns names the entity only within that tick's merge. A stored `self.body` used in a later tick does not reach it
   yet (recorded in the engine's `KNOWN-ISSUES.md`), so set the body up in the spawning callback, or make the character on the camera entity itself.

Other things to know: a jump's apex at 60 Hz is the fixed step's 1.2329 m for 5 m/s, against v²/2g = 1.2742 m. A character pressed against dynamic bodies it
pushes can read as not grounded (an existing controller limit, recorded).
