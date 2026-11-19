# Maintenance: a glTF/GLB model viewer

Not a numbered phase. The user's request (2026-11-19): "a gltf/glb model viewer. The model is specified in the command line, the tool opens
a window with the model on it, the user can rotate and zoom and strafe around." One day.

**Decisions (the user, 2026-11-19):**
- **Animated models:** a skinned model plays its first clip, looping; Space pauses, N goes to the next clip.
- **Controls:** an orbit camera. Left-drag orbits around a target, the wheel zooms, right- or middle-drag strafes (pans) the target,
  W/A/S/D and Q/E strafe with the keys, F frames the model again.

**What it is built from** (nothing new in the engine):
- `asset_import::ImportGltf` with `emit_level`, then `ImportIntoBackend` into a `HostMemoryBackend`;
- a resource manager with the CPU and GPU loaders, then `scene::SpawnLevel` (entities with `MeshRenderer`, and `SkeletonInstance` on
  skinned meshes);
- `animation::AnimationSystem` with a `ManagerResolver`;
- the transform system and the render sync into a `RenderScene`, drawn by `SceneFrame` in a window, as `physics_demo` draws its scene.

## Day 1: the viewer

- [x] `game/model_viewer` (a windowed program, `ENGINE_WINDOWED_PROGRAM`; it links the importer, so it gets a line in
  `tools/asset_boundary_check/allowlist.txt` like `asset_demo`):
  - `model_viewer FILE.glb|FILE.gltf [--backend B] [--frames N] [--screenshot out.ppm] [--clip N] [--no-animation]`, read through
    `os::CommandLine`; a `.gltf` with external buffers and images reads them beside the file (the importer's resolver over `io::`);
  - errors (no file, a file the importer refuses) are printed with the importer's message and exit 1, never a crash.
- [x] **The scene** (`viewer_scene.h/.cpp`):
  - import, back-end, manager (CPU and GPU loaders over the device), and spawn the default scene's level;
  - a sun and an ambient light;
  - the bounds of what was spawned (the render objects' bounds), to frame the camera.
- [x] **The camera** (`orbit_camera.h/.cpp`, plain math with no window, so it is tested on its own):
  - target, yaw, pitch (clamped short of the poles), distance (clamped to the model's size);
  - orbit, zoom (multiplicative per wheel notch), pan in the view plane scaled by the distance, and key strafing scaled by the model's
    size and the frame time;
  - `Frame(bounds)` fits the bounding sphere in the field of view; near and far planes follow the distance.
- [x] **Animation:**
  - every entity with a `SkeletonInstance` gets an `AnimationPlayer` with the clip chosen (the first of the import's clips, `--clip N`);
  - the animation system runs each frame; Space pauses and N steps to the next clip;
  - a model without clips is static.
- [x] **The window:** the `physics_demo` loop (modal-loop callback, device-lost handling, reset callbacks, overlay scale). The overlay shows the
  file name, the mesh, material, triangle and clip counts, the playing clip, and the key help; F1 hides it.
- [x] **Tests:**
  - `tests/model_viewer/orbit_camera_test.cpp` (phase 22, the importer's phase): framing puts the bounds inside the view, pitch clamps,
    zoom clamps, a pan moves the target in the view plane, orbit keeps the distance;
  - ctests (`gpu;demo`, skip 77 without a GPU): `model_viewer_screenshot_robot` (`tests/engine_library/fixtures/robot.glb`, animated, 120
    frames, offscreen) and `model_viewer_screenshot_field` (`green_field.glb`, static); each checks the "wrote ..." line with the counts;
  - `model_viewer_bad_file` (a missing file and a file that is not glTF exit 1 with a message);
  - `model_viewer_window_wayland` and `_x11` (`gpu;demo;display`, 300 frames with validation on).
- [x] **Docs:** `game/model_viewer/README.md`, and a section in `docs/guide/assets.md` (importing at run time: the viewer as the example).
  No public header changes.
- [x] **Standing rows:**
  - `scripts/test-phase.sh --affected` plain, then asan and tsan;
  - Clang unity, GCC non-unity;
  - the gates (`asset_boundary_check`, `module_deps_check`, `platform_neutral_check`, `cmdline_access_check`);
  - a line in `tests/phases.txt` and `tests/module_map.txt` for the new directories;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md` (Windows and the Mac have not built it); commit and push.

**Done when:** `model_viewer robot.glb` opens a window with the walking robot framed; the mouse and keys orbit, zoom and strafe; the
screenshot and window ctests pass on Linux; the guide and README say how to use it.

## Notes (2026-11-19)

**What was built:**
- `game/model_viewer`: `main.cpp` (the command line, the window loop, `--screenshot`), `viewer_scene.h/.cpp` (import, spawn, animation,
  render sync) and `orbit_camera.h/.cpp`; `model_viewer_lib` holds the last two, for the test.
- **Found while building:** meshes the GPU loaders make are drawn only after their upload barriers are recorded. The G-buffer refused them ("the
  mesh ... is not prepared"), and the first pictures were black. The scene now records `GpuMesh::RecordPrepare` for every mesh that is not
  ready, in one submission, after the spawn and on each update; the second covers meshes a device reset made again. The guide says so.
- **Two departures from the plan:**
  - the `.gltf` ctest uses the asset tests' small `model.gltf` (a skinned quad with a clip), since there is no larger `.gltf` in the tree;
  - the missing-file test needs no GPU; the not-glTF test does (the import runs after the device is made) and skips without one.

**Seen:**
- `robot.glb`: 5 objects, 1,576 triangles, 2 clips (`Idle_RobotRig`, `Walk_RobotRig`), 1 skinned entity, posed by the clip.
- `green_field.glb`: 633 objects, 22,118 triangles, framed whole.
- An animated frame differs from the rest-pose frame at the same camera (9% of the bytes), so the clip plays.

**Runs:**
- `scripts/test-phase.sh --affected --flavor all`: `ran 10923 of 11450 tests (phases: all; flavors: all; display: included; crash tests:
  excluded) in 588 s`. Failures, all known: the Job-destroyed test (three flavors), `engine_c_surface`, the voice-cost snapshot,
  `script_bench_check` (timing under load) and the tsan LuaJIT sandbox flake.
- After the review's fixes: `model_viewer_tests` 6 of 6 and the 7 `model_viewer_*` ctests pass.
- Builds: Clang unity, GCC non-unity, both clean.
- **Not done here:** the mouse and keys were not driven by hand. The camera's math is tested and the window runs 300 frames, but the
  user has not tried it yet.

**Review** (`/code-review high`): 2 findings, fixed.
- The wheel zoomed in when scrolled down (`scroll_dy` is positive scrolling down); it now zooms out.
- The component registry was a member that a second `Create` would have registered into twice; it is now made by each `Create`.
- Declined: the first frame after a press can see a large mouse delta (the pointer's position before any event is 0, 0); it only
  matters when a button is already held at start.

**Also fixed:** `ARCHITECTURE.md` had not been updated for the edges Day 475's finish gave `game/script_demo` (renderer, audio, script_media,
platform); it is now, with the viewer's line.
