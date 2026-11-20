# C API script physics / Day 1: the bindings and one character set

**Series:** [C API script physics](../c-api-script-physics.md) (the whole design)
**Depends on:** the glTF colliders series (the imported ground of the tests).

## Tasks

- [x] **One character set per world** (`engine/engine_c/runtime.{h,cpp}`):
  - `WorldObject` owns a `script_physics::CharacterSet` made with the world (after its `PhysicsWorld` resource, destroyed before it);
  - `ENGINE_COMPONENT_CHARACTER` set: remove the entity's character from the set if it has one, then `Add` it at the entity's transform with
    the component's config; component remove and entity destroy: `Remove` (and `Prune` once per frame);
  - `character_move(velocity, dt)`: check as today, then `SetMove` (the held wish; `dt` unused); `character_jump(speed)`: `Jump` when the
    character stands; both fill `engine_character_state` from the set's controller;
  - remove `WorldObject::characters` and `FollowCharacters`; after the schedule, `CharacterSet::WriteBack` (interpolated, below). (Built:
    `WorldObject::characters` is now the `UniquePtr<physics::CharacterSet>`; `FollowCharacters` stays as the frame's step that prunes, writes
    back and keeps `ENGINE_COMPONENT_CHARACTER` in step with the set.)
- [x] **`CharacterSet`** (`engine/script_physics/character_set.{h,cpp}`; moved to `engine/physics/character_set.{h,cpp}`, namespace `physics`, so
  `engine_c` and `script_physics` share it without an upward edge):
  - `SetPosition(entity, position)`: the capsule's centre moves there (`SetTransform`), the kept vertical speed and the body's speed become 0,
    the ground state is cleared until the next step, the wish stays;
  - `WriteBack(ecs, alpha)` (or a second function): with interpolation on, write `PhysicsWorld::Interpolated`'s position; the script demo
    keeps its current behaviour.
- [x] **The binding** `character.set_position(entity, x, y, z)` (`world-write`; a new command kind 115, `kCommandCharacterPosition`, applied
  at the merge) in `engine/script_physics/physics_bindings.cpp`, in the generated `docs/reference/script-api.md`.
- [x] **The bindings in a C API world** (`engine/engine_c/script_api.cpp`, both places that register the standard bindings):
  `RegisterPhysicsComponents`, `RegisterPhysicsValidators`, `RegisterPhysicsBindings`, `RegisterPhysicsCommandKinds`, the services (the world's
  `PhysicsWorld`, its `CharacterSet`, the layers from the settings or the defaults) in `extensions[kServicePhysics]`; after the frame's step,
  `EmitPhysicsEvents` for the scripts. CMake: `engine_c` links `script_physics` where it links `script`.
- [x] **Same tick:** confirm from the merge order that `entity.spawn` then `character.create` on the new entity works in one tick; state the
  rule (same tick, or the next) in the docs.
- [x] **Module edge:** `engine_c -> script_physics` in `ARCHITECTURE.md` and the module map (and `ARCHITECTURE.md`'s stale "Linux x86-64 so
  far" for `engine_c`'s script module).
- [x] **Tests:**
  - `tests/engine_library/host_script_character.c` (new; its `add_test`, `tests/phases.txt` line and `std_usage_check` entry): an imported
    `-col` ground spawned; a behavior spawns an entity and makes a character in the same tick, walks it, jumps at 5 m/s: it lands,
    `is_grounded` goes false then true, the apex is `v^2 / (2 g)` within 2 cm; `character_jump(world, key, 0, &state)` agrees with the
    script's `is_grounded` and `vertical_speed`; `character.set_position` puts it at the new place with vertical speed 0;
  - frames of 1/30 s and of 1/120 s walk the same distance in a second as 1/60 s, within 1 cm;
  - the existing character tests (`host_physics.c`, `physics_sample.c`, `host_character_world.c`, the walk-around sample) updated for the
    held move where they relied on one move per call;
  - a world without the script capability has no such bindings (the library built without the script module). (Not run: every Linux build
    has the script module; recorded in `KNOWN-ISSUES.md`.)
- [x] **Docs:** `engine_c.h` (`character_move` holds the wish; `dt` unused; the write-back is interpolated), `docs/guide/engine-library.md`
  (scripts with physics: the timing, the teleport, one character set), `docs/guide/scripting.md` (the C API world has the physics bindings).
- [x] **Runs and review:** `scripts/test-phase.sh --affected` plain, asan and tsan; `ctest -L docs`; GCC unity off; `/code-review high`.
- [x] **Records:** `STATUS.md`, `KNOWN-ISSUES.md` (the PC and the Mac), a note at the end of the host's request file; commit and push.

**Done:** the tests above pass on Linux in the three flavors, the docs say the timing and the behaviour change, and the review is recorded
below.

## Notes (2026-11-20, as built)

- **Same tick:** `entity.spawn` then `character.create` on the returned placeholder works in one callback: the merge makes the entity, then
  the character (`CharacterCreate` skips its transform and existing-character checks for a placeholder; the merge's `Add` checks them).
  `character.exists` is true from the next tick. The placeholder handle itself reaches the entity only within that tick's merge
  (`KNOWN-ISSUES.md`).
- **The apex** of a 5 m/s jump at 60 Hz is the fixed step's discrete one, 1.2329 m (`dt * (n v - g dt n (n + 1) / 2)`, `n = floor(v / (g
  dt))`), 3% under the continuous `v^2 / (2 g)` = 1.2742 m; the test checks the discrete value within 5 mm and prints both.
- **The held move** walks 1.4750 m in one second at 30, 60 and 120 frames a second.
- **Found on the way:**
  - `PhysicsWorld::Restore` dropped the pre-step hook (it swaps in a fresh world), so every character stopped after a restore. Fixed in
    `physics/snapshot.cpp`, with the test `Snapshot: a restore keeps the program's pre-step hook installed`.
  - The C API snapshot did not hold the characters' own state. `CharacterSet::Snapshot`, `ValidSnapshot` and `Restore`, and a footer
    (`[physics][characters][u32 size]["CHRS"]`; a buffer without it is the physics world's alone, so the header tests still patch it in
    place). Test: `Script character: a set's snapshot puts the characters back with the world's`. Both new tests fail without their fix.
  - The character scene's numbers changed with the held move: `physics_sample`'s golden checksum `532bfea0` became `661cbfc4`, and
    `host_character_world`'s printed hash `81970852f4f13a9f` became `3fa72af45a8d482b`.
  - A body spawned at the player's own x/z held the player up on its capsule (characters collide with each other); the test places it
    away, with a comment.
  - A character pressed against dynamic boxes it pushes reports not grounded while its vertical speed grows; the same at `3be0d921`
    (built and traced in a scratch worktree). Recorded in `KNOWN-ISSUES.md`, not changed here.
- **Layers:** `script.physics_layers` is declared in the engine's registry and read when a world's scripts start (the test sets
  `default,world,player` and reads layer 2).

## Runs (2026-11-20, Linux, base `3be0d921`)

- `scripts/test-phase.sh --affected` (after the review's fixes): `ran 4321 of 11634 tests (phases: all; flavors: plain; display: included;
  crash tests: excluded; soak: excluded) in 116 s`. Failed: `docs_check` and `docs_script_api` (the `set_position` text changed after the
  last docs regeneration; they pass after it), `Snapshot: the cost of a voice at each quality, with and without effects` (the known Debug
  bound), and `X11: text set on one connection is read on another, for every kind of text` (it passed alone; recorded in `KNOWN-ISSUES.md`).
- `--affected --flavor asan`: `ran 3562 of 11634 tests (phases: all; flavors: asan; ...) in 220 s`, no failure.
- `--affected --flavor tsan`: `ran 3212 of 11634 tests (phases: all; flavors: tsan; ...) in 281 s`, no failure.
- `ctest -L docs`: 9 of 9. `docs/reference` regenerated (`physics.md`, `script-api.md`, `script_physics.md`, the index).
- Builds clean: Clang unity (`build`) and GCC unity off (`build-gcc`).

## Post-implementation code review (`/code-review high`)

Eight findings; six fixed, two recorded:

1. **Fixed:** `PhysicsWorld::Restore` dropped the pre-step hook, so restored characters stopped. Regression test (fails without the fix):
   `Snapshot: a restore keeps the program's pre-step hook installed`.
2. **Fixed:** the C API snapshot lacked the characters' state. Now there is the set's snapshot behind a footer. Test (fails without the fix):
   `Script character: a set's snapshot puts the characters back with the world's`; `physics_sample`'s rewind matches again.
3. **Fixed:** `component_has` and queries did not see a script-made character (`component_get` did). The frame now keeps
   `ENGINE_COMPONENT_CHARACTER` in step with the set. `host_script_character.c` checks has, get and a query, and fails without the sync.
4. **Fixed:** `CharacterSet::SetPosition` kept the last step's ground state, so a teleport off a moving platform carried its velocity. Now
   the character is in the air until the next step looks, as the design said. No separate test: the scripted teleport check passes.
5. **Fixed:** the scripts' layer names ignored `script.physics_layers`. The setting is declared and read; the test reads layer 2.
6. **Fixed:** `CharacterOf` ran `Recreate` on every C character call (O(n) each). Removed; the restore recreates the bodies.
7. **Recorded, not fixed (`KNOWN-ISSUES.md`):** a character pressed against dynamic boxes it pushes reads as not grounded while its vertical
   speed grows. It was the same before this day (traced at `3be0d921`); a controller change is outside this day.
8. **Recorded, not fixed (`KNOWN-ISSUES.md`, the guides):** the handle `entity.spawn` returns names the entity only within its tick's
   merge. Binding placeholders across ticks is a script-runtime change outside this day's scope.

## Follow-up (2026-11-20): the C++ layer

`include/engine/engine.h` forwarded the changed functions unchanged, without saying so. `World::MoveCharacter` (a held move, `dt` unused),
`JumpCharacter` (at the next step), the physics section (one set shared with the scripts, the interpolated write-back) and `PhysicsSnapshot`
(the characters' state) are documented, and the guide's C++ section points at the held move. No code or ABI change. Builds clean; the C++
layer, ABI and table-coverage tests pass (94 of 94), and `ctest -L docs` passes 9 of 9. Review: the diff is comments only; no findings.

