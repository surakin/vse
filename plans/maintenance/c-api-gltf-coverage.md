# The C API catches up with glTF coverage (maintenance, 2026-11-20, Linux)

Not a numbered phase. The user asked to check the C and C++ API and bring it up to date. Every function of the C table has a C++
wrapper (`engine_h_covers_table`). What the C API lacked are three things the glTF coverage series built inside the engine (Days 8-12):
spot lights, morph weights, and the lights of a spawned level. The user chose to add all three (2026-11-20).

## Design

- **Spot lights.**
  - `ENGINE_LIGHT_SPOT` (3) shines from the entity's position along its -Z, like the directional light's axis.
  - `engine_light` gains `inner_cone` and `outer_cone` (half-angles in radians, 0 <= inner < outer <= pi/2) at its end.
  - A struct that ends before `inner_cone` is the first version: it may set and read directional and point lights, but a spot light
    needs the cone fields (`STRUCT_SIZE` otherwise). `component_get` writes only what the host's struct holds.
- **Morph weights.**
  - `ENGINE_COMPONENT_MORPH_WEIGHTS` (18), `engine_morph_weights { struct_size, count, weights[ENGINE_MORPH_WEIGHTS_MAX = 64] }`.
    It maps to `animation::MorphWeights`. `ENGINE_COMPONENT_MASK_ALL` becomes `0x3FFFF`.
  - A surface draws a morphed mesh with the entity's weights (borrowed each frame, as the joint palette is).
  - `animation_play` accepts a clip of weights only on an entity without a skeleton instance (a skinned entity takes it as before). An
    entity gets the component when a clip with weight tracks starts on it.
- **Spawned levels bring their lights and weights.** The spawn checks them first, as `component_set` checks a host's (a level blob
  is the host's input, and the render scene asserts on a light that is not finite), and refuses the level whole otherwise.
  - `level_spawn` copies `scene::LightSource` into the C world's light component. The level's own direction is kept as an internal
    local axis, so a light pointing anywhere in its entity's space draws as it should; the importer always writes +Z. An ambient light
    has no C kind and is left out.
  - `level_spawn` copies `animation::MorphWeights` too.

The ABI gains no table function, so the minor version stays (the practice since Day 347: the snapshot is regenerated, and only an
added symbol raises the minor).

## Tasks

- [x] `engine_c.h`: the light kind, the cone fields, the component kind, the struct and the mask; the docs.
- [x] `runtime.cpp`: light set, get and checks; the morph component's set, get, remove, the kind tables and the query.
- [x] `resources.cpp` `PinClip`: a clip of weights only needs no skeleton; `animation_play` without a skeleton instance.
- [x] `surface_scene.cpp`: spot lights; morph weights on objects.
- [x] `level_spawn`: lights and weights.
- [x] Tests (`tests/engine_library`):
  - `host_runtime.c`: the components' round trips and refusals, the first-version light struct (set, read without an overrun, no
    spot), the query mask;
  - `host_import.c`: a glTF with a morphed mesh, a weights animation and three lights, spawned and played, then drawn on an
    off-screen surface while the face moves between archetypes (failed when the spawn copied nothing);
  - `host_offscreen.c`: a spot light lights the mesh when it points at it and not when turned away (red sums about 252,600 toward,
    149,900 away, 253,700 as a point light);
  - `level_spawn_test.cpp` (new, `engine_c_tests`): a level whose spot radius is NaN (the record rewritten, the blob rebuilt) is
    refused and the world is unchanged (failed without the check).
- [x] The ABI snapshot regenerated; the C++ header (`engine.h`) needs nothing new (no table function); docs regenerated;
  `docs/guide/engine-library.md` (lights, morph weights, playing, spawning).
- [x] Scoped runs, `/code-review high`, `STATUS.md`, `KNOWN-ISSUES.md` (the Day 12 entry about C API lights is removed), commit and
  push.

Also fixed (asked by the user during the day): the comments that said scripting through the C API was Linux only
(`ENGINE_CAP_SCRIPT` in `engine_c.h`, `engine/engine_c/CMakeLists.txt`, `docs/guide/engine-library.md`, `docs/guide/scripting.md`):
Windows and macOS x86-64 have had it since Days 473 and 474.

**Done when** a host can make spot lights and set morph weights through the C API, a spawned level brings its lights and weights, an
imported weights animation plays on a morphed mesh through `animation_play`, and the tests show it.

## Runs

- Plain: `ran 4289 of 11538 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 128 s`
  (every phase: a public header changed). Two failed:
  - `std_usage_check`: `host_runtime.c` now includes `<stddef.h>`; its allow-list entry names it now and the gate passes;
  - `Snapshot: the cost of a voice at each quality, with and without effects` (phase 26, a timing bound): it failed alone too, in
    this `build` tree, which is now Debug. It is unrelated, and recorded in `KNOWN-ISSUES.md`.
- asan: `ran 3530 of 11538 tests (phases: all; flavors: asan; display: included; crash tests: excluded; soak: excluded) in 210 s`,
  no failures (before the spawn check and its test; those ran in asan afterwards, below).
- tsan: `ran 3181 of 11540 tests (phases: all; flavors: tsan; display: included; crash tests: excluded; soak: excluded) in 273 s`. Two
  failed:
  - `LuaJIT sandbox: a trusted state refuses instruction and time budgets, a budgeted one honors them [tsan]`, the known flake of
    Day 470 (`KNOWN-ISSUES.md`);
  - `engine_c_import_tsan`: a SEGV in a thread under ThreadSanitizer, from the new offscreen drawing check. ThreadSanitizer cannot run
    the GPU driver's threads, which is why the GPU tests have no tsan form. Fixed: the check is skipped under `HOST_IMPORT_TSAN`.
    `engine_c_import`, `_asan` and `_tsan` then pass, 3 of 3.
- After the spawn check: `engine_c_import_asan`, `engine_c_runtime_asan`, `engine_c_offscreen_asan` and `engine_c_tests_asan` pass,
  4 of 4.
- `ctest -L docs` 9 of 9; the ABI snapshot, `engine_h_covers_table` and the gates pass; `build-gcc` (GCC, unity off) builds
  `engine_c` and the three C hosts clean.

## Post-implementation code review

`/code-review high` on the diff: 5 findings; 4 fixed, 1 kept and documented. The tsan run then found a sixth problem (the GPU
drawing check under ThreadSanitizer, above), fixed.
1. **A hostile level could stop the host.** The spawn copied a level's lights and weights unchecked, and `RenderScene` asserts on a
   light that is not finite or a cone that is not ordered. Fixed: the spawn checks them before the world is touched. Test:
   `level_spawn_test.cpp` (failed without the check).
2. **Two tests named the first bit past the mask by number** (`0x20000`, now the morph-weights bit). Fixed: `ENGINE_COMPONENT_MASK_ALL + 1`
   in `host_runtime.c` and `host_physics.c`.
3. **The morph drawing check did not move the weights.** Its comment said creating entities moved them, and it did not. Fixed: the
   face gains and loses a component between frames.
4. **The no-light baseline of the spot picture was wrong.** A world with no light is lit by the surface's default sun. Fixed: the
   check compares the spot pointed at the mesh with the spot turned away.
5. **Kept: a spawned light's axis is not in `engine_light`.** A level light pointing along another axis than its entity's -Z keeps
   that axis until the host sets the component, which gives it -Z. A glTF light's axis is -Z, so imported levels are not affected.
   Documented in `engine_c.h`.

## Not done

- Not built or run on the PC or the Mac (`KNOWN-ISSUES.md`); Direct3D 12 and Metal draw the spot lights and the weights through the
  same renderer paths, untested from the C API there.
