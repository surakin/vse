# The physics and character script bindings in a C API world (design, 2026-11-20)

Status: draft for the user's approval. Request: `plans/maintenance/host-script-physics-bindings.md` (a host's, 2026-11-20), the follow-up
of the glTF colliders series.

## Intent

A host's player is a Lua behavior in a C API world (`ENGINE_COMPONENT_SCRIPT`). It needs gravity, ground contact and a jump, which the
documented script API already has (`character.*`, `physics.*`), but a C API world registers only the standard bindings
(`engine/engine_c/script_api.cpp`, `RegisterStandardBindings` at lines 584 and 923; nothing in `engine_c` links `script_physics`). The host's
reading is confirmed. The goal: the documented Lua calls work in a C API world, on that world's physics, with one character per entity
whichever door made it, and a stated timing a first-person camera can rely on.

## Non-goals (the request's)

No new physics features, no second character API, nothing of the host's own concepts in the engine.

## Decisions

1. **One character set per world (the user's choice, 2026-11-20).** A C API world keeps one `script_physics::CharacterSet`, made with the
   world (it installs the pre-step hook in the world's physics). Both doors use it:
   - **Scripts:** `character.create`, `move`, `jump`, `set_height`, `destroy` and the reads, as documented.
   - **The C API:** `ENGINE_COMPONENT_CHARACTER` adds the entity's character to the set (setting it again puts the capsule at the
     entity's transform with the new config, as today); removing the component, or destroying the entity, removes it.
     `character_move(velocity, dt)` sets the held wish (`CharacterSet::SetMove`), and `dt` is checked but no longer used.
     `character_jump(speed)` asks for a jump when the character stands. Both fill `engine_character_state` from the controller.
   - The C API's own list (`WorldObject::characters`, `FollowCharacters`) goes. **Behaviour change for C hosts, stated in `engine_c.h`
     and the guide:** a move holds until changed (the character keeps walking), and it is carried out in each fixed step, not once per
     call. No ABI change.
   - A character made by one door is the same object seen from the other. `character.create` on an entity that already has one fails
     ("has a character") whichever door made it; the C component set on such an entity re-places it.
2. **The bindings in every world with the script capability.** No flag. A world's script host registers the physics components and
   validators, `RegisterPhysicsBindings`, the command kinds, and the services (the world's `PhysicsWorld`, its `CharacterSet`, and the
   layers from the `physics.*` layer settings). After the frame's step, `EmitPhysicsEvents` hands the step's contact and trigger events to
   the scripts (`on_collision_*`, `on_trigger_*`), as the script demo does. A library built without the script module is unchanged.
3. **Write-back and the camera.** After the step the set writes each character's capsule centre to its entity's `LocalTransform`
   (`CharacterSet::WriteBack`, replacing `FollowCharacters`). When `physics.interpolate` is on (the default), it writes the **interpolated**
   position (`PhysicsWorld::Interpolated` with the world's `Alpha()`), like the dynamic bodies' write-back, so a camera that follows the
   entity moves smoothly when frames and fixed steps do not line up. `is_grounded`, `vertical_speed` and the C state report the last step.
4. **Timing, stated in the docs.** A frame runs the script tick (the merge applies the commands), then 0, 1 or several fixed steps, then
   the write-back. The held move is carried out once per fixed step, so the distance walked in a second does not depend on the frame
   rate (a frame with two steps moves twice, one with none moves nothing). A jump is applied at the start of the next fixed step and waits
   for one if the frame makes none.
5. **Teleport.** A new binding, `character.set_position(entity, x, y, z)` (`world-write`, a command applied at the merge): it moves the
   capsule's centre there, zeroes the kept vertical speed and the body's speed, and clears the ground state until the next step; the held
   move stays. `CharacterSet` gains `SetPosition`. `transform.set_position` on a character's entity does not move the character: the
   write-back overwrites it (documented). C hosts set `ENGINE_COMPONENT_CHARACTER` again, as today.
6. **Same tick.** A behavior's `entity.spawn(...)` followed by `character.create` on the new entity in one tick works: both are commands,
   merged in order, so the character exists after that tick's merge and moves in that frame's steps. The builder confirms it against the
   merge order and states it; if the merge cannot, the documented rule becomes "the next tick".
7. **Module edge.** `engine_c -> script_physics` (both layer 14, no cycle; the precedent is `asset_import -> asset`): `ARCHITECTURE.md` and
   the module map in the same commit. It is built wherever `engine_c` has the script module (`ENGINE_C_WITH_SCRIPT`).

## Approaches considered for the characters

- **One set (chosen).** One object, one timing rule, the C API keeps its table functions; existing C hosts see the held-move change.
- **Two systems, one per entity.** No change for C hosts, but two code paths and two timing rules, and a host using both doors per player
  would have to know which made the character. Rejected by the user.

## Tests

1. A C host program (`host_script_character.c`): an imported ground (a `-col` quad) spawned, a behavior that spawns an entity and makes
   a character on it in the same tick, walks it, jumps it: it lands, `character.is_grounded` goes false then true, and the apex is
   `v^2 / (2 g)` within 2 cm. The C API's `character_jump(world, key, 0, &state)` reports the same `on_ground` and vertical speed the script
   saw.
2. Frames of 1/30 s (two steps each) and of 1/120 s (alternating none and one): the distance walked in a second matches 1/60 s frames
   within 1 cm.
3. `character.set_position` puts the capsule at the new place with vertical speed 0.
4. The C door alone (`ENGINE_COMPONENT_CHARACTER`, `character_move`, `character_jump`): the existing C tests and the glTF colliders
   character test, updated for the held move.
5. The bindings exist only in a world with the script capability; the scripted tests run plain, asan and tsan.

## Left to the builder

- Where the set lives (`WorldObject`), and the order of the frame's new calls around the schedule.
- The layer settings the services read (the existing `physics.*` layer settings, or the defaults when none are declared).
- The interpolation in `WriteBack` (a parameter, or a second function) without changing the script demo's behaviour unless it asks.
- The day split (about three days on Linux, then the PC and the Mac).
