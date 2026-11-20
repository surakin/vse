# glTF colliders / Day 7: the character on an imported world, hostile files, the guides and the closing runs (Linux)

**Series:** [glTF colliders](../gltf-colliders.md) (design section 6 and the tests)
**Depends on:** Days 1-6.

## Tasks

- [ ] **The character acceptance test** (`tests/engine_library/host_character_world.c`, new, plain C; its `add_test` and
  `tests/phases.txt` line in the same commit, for every platform branch it is registered in): one `.glb` built inline with a ground
  (`ground-col`), a box wall (`wall-colonly`), a step of exactly `step_height` and one of `step_height + 0.05`, a ramp steeper than
  `min_ground_y` and a ledge. Import, spawn, add an `ENGINE_COMPONENT_CHARACTER` entity (default parameters):
  - dropped from 2 m: after at most 120 frames of 1/60 s it is grounded, resting at the expected height within 1 mm;
  - walking into the wall: its x stops at the wall face minus the radius (within the skin width);
  - the `step_height` step is climbed; the higher one stops it; the steep ramp stops it;
  - walking off the ledge: not grounded within a frame, then lands below;
  - the same scene driven from Lua (`character.create`, `move`, `is_grounded`) in `tests/script_physics` gives the same final positions.
- [ ] **Hostile collision through the whole path** (`tests/asset/gltf_collider_test.cpp`, asan): 2,000 mutated copies of the test file
  (the existing mutation helper of `gltf_json_test.cpp`): every import either succeeds or fails with a message; every success spawns
  through `level_spawn` without a failure other than a documented refusal.
- [ ] **Determinism**: a frame-by-frame position hash of the character scene, committed as a constant, the same at 1 and 4 threads
  (Windows and macOS compare it on Day 8).
- [ ] **The guides** (the series' narrative, now that it is done): `docs/guide/assets.md` ("Collision from glTF": the two conventions with
  a Blender walk-through by object names, what is drawn, the warnings, the limit); `docs/guide/engine-library.md` (a solid level and a
  character, a compiling C sample in the guide's tagged-sample form); `docs/guide/physics.md` (parented static bodies, following, scale at
  creation, the ground thickness rule: a triangle mesh has no inside, characters do not tunnel, a fast dynamic body can; snapshots in
  the same world only).
- [ ] **Closing runs** on Linux: `scripts/test-phase.sh --affected --flavor all` over the series' range and `ctest -L docs`; record the
  `ran X of Y` lines; the series' uncapped review: `/code-review high --max-findings all` over the series' diff.
- [ ] **Records**: `STATUS.md`; `KNOWN-ISSUES.md` (what the PC and the Mac must run, Day 8; KHR_physics and hull assets as later work);
  the host's request file answered (a short note at its end saying where each wanted item landed, without changing its text).
- [ ] **Standing rows** (series rules).

**Done:** a character lands, climbs, is stopped and falls on an imported, spawned world through the C API and Lua; hostile files never
crash; the guides explain it; the Linux closing runs and the uncapped review are recorded.
