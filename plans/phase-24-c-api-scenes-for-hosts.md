# Phase 24 — Scenes for Hosts Through the C API

> Drafted 2026-11-15 from a the host session at the user's request; placed as Phase 24 the same day (every later phase moved up one) and broken into day files (Days 358-372) at the user's request.

**Status: started (Day 358, 2026-11-15; `CURRENT_PHASE` 24); the spec and the five spikes are done (`docs/superpowers/specs/2026-11-16-c-api-scenes-design.md`). Before that: planned (2026-11-15): the day files are written (Days 358-372, `plans/phase-24-c-api-scenes-for-hosts/`), so the phase can start once Phase 23 closes (Day 357); Phase 28 (3D text) moved to Days 373-388 to make room (+15).** Placed right after Phase 23 because it builds on its C ABI, surfaces and host-provided content, and because its first user (the host's 3D call rooms) needs nothing from physics, audio or scripting.

**Depends on:** Phase 23 (the C ABI, the function table and its version rules, surfaces in host-owned windows on Linux, Windows and macOS,
host-owned input and the host-driven frame, host-provided content, glTF import at run time), Phase 22 (resources, the level format and
`scene::SpawnLevel`, skeletons, clips and GPU skinning, the glTF importer), Phase 17 (ECS) and the `scene` and `animation` modules
(`MeshRenderer`, `LightSource`, `Camera`, `ActiveCamera`, `CameraOutput`, `AnimationPlayer`, `SkeletonInstance`, `AnimationSystem`), Phase 14
(text), Phase 8 (device reset: everything a host creates must survive it).

**Goal:** a host program that loads the engine as a library can put a lit, animated 3D scene in its own window: import or load a scene and
its models, place entities, play their animations, look through a camera it moves, feed textures it updates every frame (live video), and
draw text labels in the world, all through the versioned C API and without any engine C++ type crossing the boundary. Phase 23 can embed the
engine and load resources but its surface only clears to a color and its components stop at transform, name and mesh reference; this phase
makes the embedded engine draw.

**Driving case:** the host's 3D call rooms (the host repository: `docs/3D-CALL-ROOMS.md`, `docs/3D-DATA-STREAMS.md`, the sample assets in
`assets/worlds/`): a glTF meadow with instanced trees, robot avatars with an 8-joint skeleton and looping `Walk`/`Idle` clips, a `Screen`
material on each robot showing that participant's video, a first-person camera, name tags. Everything here is general (any host gets it),
but that scene is the acceptance test.

**Definition of done:** through the C API only, a C host (and the the host sample assets, imported at run time) can: bind a world to a surface
and see it rendered through a camera entity it moves; light it with a directional light and an ambient term; spawn the entities of a glTF
scene (its node tree as a level) and place more instances itself; give an entity a skinned mesh, play, blend and change the speed of its
clips; create a texture, update its pixels every frame from the host (RGBA8 at the source's size, resized when the source changes) and
bind it in place of one material slot's base color on one entity without touching the others that share the material; draw a text label
facing the camera at a point in the world; and do all of it across a device reset and a surface rebind without the host re-creating
anything. The C host test renders the meadow with three robots (one walking, two idle, each screen fed a different generated pattern) and
compares pixels with a golden image; the ABI gate records the version bump; the guide and a tutorial are written; Linux, Windows and macOS
pass the phase's tests and the full suite in every flavor.

## Days

358. [Design Spec, Spikes & Phase Setup](phase-24-c-api-scenes-for-hosts/day-358-design-spec-spikes-and-setup.md)
359. [A World on a Surface](phase-24-c-api-scenes-for-hosts/day-359-a-world-on-a-surface.md)
360. [Cameras](phase-24-c-api-scenes-for-hosts/day-360-cameras.md)
361. [Lights & the Environment](phase-24-c-api-scenes-for-hosts/day-361-lights-and-environment.md)
362. [Meshes, Material Slots, Visibility & Hierarchy](phase-24-c-api-scenes-for-hosts/day-362-meshes-materials-and-hierarchy.md)
363. [Skinned Meshes & Animation](phase-24-c-api-scenes-for-hosts/day-363-skinned-meshes-and-animation.md)
364. [A glTF Scene as a Level](phase-24-c-api-scenes-for-hosts/day-364-a-gltf-scene-as-a-level.md)
365. [Host Textures I: Create, Update, Resize](phase-24-c-api-scenes-for-hosts/day-365-host-textures-i-create-and-update.md)
366. [Host Textures II: Binding, Several Surfaces & Reset](phase-24-c-api-scenes-for-hosts/day-366-host-textures-ii-binding-and-reset.md)
367. [Labels: Text in the World](phase-24-c-api-scenes-for-hosts/day-367-labels-text-in-the-world.md)
368. [Device Reset, Surface Rebind & Many Surfaces](phase-24-c-api-scenes-for-hosts/day-368-reset-rebind-and-many-surfaces.md)
369. [Windows: Scenes on an `HWND` (Vulkan & Direct3D 12)](phase-24-c-api-scenes-for-hosts/day-369-windows-scenes-on-an-hwnd.md)
370. [macOS: Scenes on an `NSView` (Metal)](phase-24-c-api-scenes-for-hosts/day-370-macos-scenes-on-an-nsview.md)
371. [The Walk-Around Sample & the Acceptance Scene](phase-24-c-api-scenes-for-hosts/day-371-walk-around-sample-and-acceptance-scene.md)
372. [Docs, Closing Runs on Three Machines & the Phase Review](phase-24-c-api-scenes-for-hosts/day-372-docs-closing-runs-and-phase-review.md)

The Linux machine does Days 358-368, 371 and 372's Linux half; **Day 369 is for the Windows PC and Day 370 for the Mac** (CLAUDE.md, "Three machines"): Linux days write the shared code and the C hosts so the other two machines run and fix. The phase starts only after Phase 23's Day 357 (`CURRENT_PHASE` moves to 24 on Day 358).

## Key deliverables (broken into the days above)

- **A world on a surface.** `surface_set_world(surface, world, camera_entity)` (names decided in the spec): the surface renders that world
  through that camera on every `engine_frame` instead of clearing; several surfaces may show one world through different cameras; unbinding
  returns the surface to its background color. The render pipeline is the existing renderer and `scene::RenderSync`; the C API only
  connects them. Viewport size and scale follow `surface_resized`; the camera's aspect ratio follows the surface unless the host fixes it.
  Errors: a destroyed world or camera entity unbinds with a status in `surface_state`, never a crash.

- **Cameras.** `ENGINE_COMPONENT_CAMERA` (perspective: vertical field of view, near and far planes; orthographic later if cheap) on any
  entity; the camera looks along the entity's transform (the convention stated in the header: glTF's, `-Z` forward for cameras, or the
  engine's, decided once). The host moves it by setting the entity's transform each frame; no camera controller is imposed (the engine's
  demo controllers stay engine-side). A query to turn a surface pixel into a world ray (for the host's picking) is in scope if cheap.

- **Lights.** `ENGINE_COMPONENT_LIGHT` for the kinds `scene::LightSource` already supports (directional at least, point if present), plus a
  world-level ambient color and intensity, and the sky or background color behind the scene. Shadows only if the renderer already has them;
  otherwise recorded as a later item, not built here.

- **Meshes and materials, completed.** `ENGINE_COMPONENT_MESH_REFERENCE` (Phase 23) covers rigid meshes; this phase adds per-slot material
  overrides on an entity (so one robot's `Screen` slot can use another texture without a new material for every robot), visibility (the
  `Hidden` tag) and the skinned case (below).

- **Skinned meshes and animation.** `ENGINE_COMPONENT_SKELETON_INSTANCE` and `ENGINE_COMPONENT_ANIMATION_PLAYER` mapping to
  `animation::SkeletonInstance` and `AnimationPlayer`: an entity with a skinned mesh gets its skeleton instance; the host plays a clip by
  resource id or name, loops it, sets the playback speed (to match walking speed), cross-fades to another clip over a duration, and reads the
  current time. The `AnimationSystem` runs inside `engine_frame` before the transform and render sync. Root motion is not used by the driving
  case and stays out unless free. The animated demo moved out of Phase 22 Day 339 (blending, a palette HUD) is a natural test bed here.

- **A glTF scene as a level.** The importer's scene output moved out of Phase 22 Day 338 (`KNOWN-ISSUES.md`, Day 338 item 3): the default
  scene's node tree becomes a level (one entity per node with its transform, mesh and skeleton-instance references; glTF instancing kept as
  several entities sharing one mesh resource), available from the C API's `import_gltf` result and spawnable into a world
  (`level_spawn(world, level, parent_transform)` over `scene::SpawnLevel`), and through `asset_mint import-gltf --emit-level` for the tool. Named
  nodes are findable after spawning (the driving case looks up a `Spawn` node). The level minter is tool code over the ECS today; the spec
  decides whether the runtime builds the level in memory without it.

- **Textures the host updates.** `texture_create(width, height, format)` (RGBA8 sRGB at least), `texture_update(texture, pixels, stride)`
  from any host thread (the engine copies and uploads on the next frame; a newer update replaces a pending one, so a slow frame never queues
  video), `texture_resize` (a camera changing resolution), `texture_destroy`; the texture is usable wherever a material slot's base color
  is (the per-slot override above). Uploads are budgeted per frame and counted in the stats; a texture that is updated faster than frames
  are rendered costs one upload per frame, not one per update. Survives a device reset (the engine keeps the last pixels and re-uploads).
  Out of scope: zero-copy import of the host's GPU textures (a later phase if a host needs it; Phase 23 Day 356's shared textures go the
  other way, engine to host).

- **Text labels in the world.** `ENGINE_COMPONENT_LABEL`: a UTF-8 string drawn at the entity's position (with an offset), facing the camera,
  at a constant size on screen or a size in metres, with a color and an optional background plate, through Phase 14's text rendering (and
  Phase 27's when it exists). Enough for name tags; rich text and layout stay engine-side.

- **The ABI.** Every addition follows Phase 23's rules: size-prefixed structs, `engine_caps` bits for each capability (a world on a surface,
  skinning, host textures, labels, glTF levels), the function table extended at its end, the ABI version bumped once for the phase,
  `abi_check` updated, the C header compiled by a C compiler in the tests, no C++ type across.

- **Tests and tools.** Per deliverable: C API tests through the function table (headless where possible, with `surface_read_pixels`
  for images); the device reset and surface rebind with every new object alive; host texture updates from a second thread under tsan;
  the golden-image scene of the definition of done; the existing Phase 23 C host extended into a small "walk around" sample (keys move the
  camera, one animated model, one live texture) that doubles as the tutorial's code.

- **Docs.** `docs/guide/engine-library.md` gains "Scenes, cameras and lights", "Animated models", "Live textures" and "Labels"; a tutorial
  `docs/tutorials/a-3d-scene-in-your-window.md` (import a `.glb`, spawn it, add a camera and a light, play an animation, feed a texture);
  the spec `docs/superpowers/specs/2026-11-NN-c-api-scenes-design.md` written on the first day.

## Decisions of Day 358 (spec `docs/superpowers/specs/2026-11-16-c-api-scenes-design.md`, and the day that builds each)

- Coordinates are the engine's (right-handed, +Y up, camera -Z), no conversion at the boundary (Day 359 header text).
- `surface_set_world(surface, world)` on Day 359, `surface_set_camera(surface, entity)` on Day 360: the surface names the camera.
- Per-slot material overrides, no material creation (Days 362, 366).
- Fixed order inside `engine_frame`: animation, transform, render sync, render of every bound surface; poses are read after `engine_frame` (Day 359, 363).
- Joint attachment is later; labels follow an entity's origin plus an offset (Day 367).
- Labels: the text renderer's 2D path projected from the world point (Day 367).
- ABI version unchanged (unreleased); additions at the end of the table, snapshot and `abi_check` record them (every day).
- Goldens: no channel off by more than 4, at most 0.5 % of values off by more than 1 (Days 359, 371); cross-GPU figure not measured (`KNOWN-ISSUES.md`).
- Per surface: a frame object, a `RenderScene`, a `SceneLink`, and a device-resident content cache keyed by resource id and generation (Days 359, 362, 368). The frame object moves from `game/renderer_demo` into the engine and the renderer gets a before-present hook (Day 359).
- Live textures: latest-wins slot, a ring of three staging buffers, one upload per frame, an 8 MB per-frame budget (Day 365).

## Notes / open questions (settled in the spec on Day 358)

- **Coordinate conventions at the boundary.** glTF is right-handed, `+Y` up, metres; the importer converts to the engine's convention
  (Phase 22 Day 336, "no flip"). Decide whether C API transforms are in the engine's convention or glTF's, once, and state it in the header:
  hosts will mix imported content with entities they place by hand.
- **One camera per surface, or a camera component that names its surface.** The first is simpler; the second suits several views of one
  world. Recommendation: the surface names the camera.
- **Material overrides vs. per-entity materials.** A per-slot override (slot index or name → texture) is enough for the driving case and
  avoids exposing material creation; full material creation through the C API can come later.
- **Where the frame's systems run.** Animation, transform and render sync inside `engine_frame`, in a fixed order the header documents; a
  host that needs a pose before rendering (attaching a label to a head joint) reads it after `engine_frame` returns.
- **Joint attachment.** Whether an entity can follow a joint of another entity's skeleton (a label on a robot's head). Useful, small if the
  animation system already writes joint world transforms; otherwise later.
- **Not in this phase:** physics and collisions (Phase 25), positional audio (Phase 26; the driving case pans voices itself),
  scripting, networking or replication of any kind (the host moves entities itself), shadows if the renderer lacks them, zero-copy host
  GPU textures, mobile and console platforms.
