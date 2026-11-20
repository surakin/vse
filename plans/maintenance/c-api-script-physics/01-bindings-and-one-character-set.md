# C API script physics / Day 1: the bindings and one character set

**Series:** [C API script physics](../c-api-script-physics.md) (the whole design)
**Depends on:** the glTF colliders series (the imported ground of the tests).

## Tasks

- [ ] **One character set per world** (`engine/engine_c/runtime.{h,cpp}`):
  - `WorldObject` owns a `script_physics::CharacterSet` made with the world (after its `PhysicsWorld` resource, destroyed before it);
  - `ENGINE_COMPONENT_CHARACTER` set: remove the entity's character from the set if it has one, then `Add` it at the entity's transform with
    the component's config; component remove and entity destroy: `Remove` (and `Prune` once per frame);
  - `character_move(velocity, dt)`: check as today, then `SetMove` (the held wish; `dt` unused); `character_jump(speed)`: `Jump` when the
    character stands; both fill `engine_character_state` from the set's controller;
  - remove `WorldObject::characters` and `FollowCharacters`; after the schedule, `CharacterSet::WriteBack` (interpolated, below).
- [ ] **`CharacterSet`** (`engine/script_physics/character_set.{h,cpp}`):
  - `SetPosition(entity, position)`: the capsule's centre moves there (`SetTransform`), the kept vertical speed and the body's speed become 0,
    the ground state is cleared until the next step, the wish stays;
  - `WriteBack(ecs, alpha)` (or a second function): with interpolation on, write `PhysicsWorld::Interpolated`'s position; the script demo
    keeps its current behaviour.
- [ ] **The binding** `character.set_position(entity, x, y, z)` (`world-write`; a new command kind 115, `kCommandCharacterPosition`, applied
  at the merge) in `engine/script_physics/physics_bindings.cpp`, in the generated `docs/reference/script-api.md`.
- [ ] **The bindings in a C API world** (`engine/engine_c/script_api.cpp`, both places that register the standard bindings):
  `RegisterPhysicsComponents`, `RegisterPhysicsValidators`, `RegisterPhysicsBindings`, `RegisterPhysicsCommandKinds`, the services (the world's
  `PhysicsWorld`, its `CharacterSet`, the layers from the settings or the defaults) in `extensions[kServicePhysics]`; after the frame's step,
  `EmitPhysicsEvents` for the scripts. CMake: `engine_c` links `script_physics` where it links `script`.
- [ ] **Same tick:** confirm from the merge order that `entity.spawn` then `character.create` on the new entity works in one tick; state the
  rule (same tick, or the next) in the docs.
- [ ] **Module edge:** `engine_c -> script_physics` in `ARCHITECTURE.md` and the module map (and `ARCHITECTURE.md`'s stale "Linux x86-64 so
  far" for `engine_c`'s script module).
- [ ] **Tests:**
  - `tests/engine_library/host_script_character.c` (new; its `add_test`, `tests/phases.txt` line and `std_usage_check` entry): an imported
    `-col` ground spawned; a behavior spawns an entity and makes a character in the same tick, walks it, jumps at 5 m/s: it lands,
    `is_grounded` goes false then true, the apex is `v^2 / (2 g)` within 2 cm; `character_jump(world, key, 0, &state)` agrees with the
    script's `is_grounded` and `vertical_speed`; `character.set_position` puts it at the new place with vertical speed 0;
  - frames of 1/30 s and of 1/120 s walk the same distance in a second as 1/60 s, within 1 cm;
  - the existing character tests (`host_physics.c`, `physics_sample.c`, `host_character_world.c`, the walk-around sample) updated for the
    held move where they relied on one move per call;
  - a world without the script capability has no such bindings (the library built without the script module).
- [ ] **Docs:** `engine_c.h` (`character_move` holds the wish; `dt` unused; the write-back is interpolated), `docs/guide/engine-library.md`
  (scripts with physics: the timing, the teleport, one character set), `docs/guide/scripting.md` (the C API world has the physics bindings).
- [ ] **Runs and review:** `scripts/test-phase.sh --affected` plain, asan and tsan; `ctest -L docs`; GCC unity off; `/code-review high`.
- [ ] **Records:** `STATUS.md`, `KNOWN-ISSUES.md` (the PC and the Mac), a note at the end of the host's request file; commit and push.

**Done:** the tests above pass on Linux in the three flavors, the docs say the timing and the behaviour change, and the review is recorded
below.
