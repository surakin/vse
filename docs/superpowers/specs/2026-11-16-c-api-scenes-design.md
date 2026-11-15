# Scenes for hosts through the C API: decisions and spike results

Phase 24, Day 358 (2026-11-15, Linux x86-64, Clang, NVIDIA Vulkan, plain debug build). The spikes are the tests in
`tests/scene/scene_spikes_test.cpp` (`[spike]`), which print the numbers below; they stay in the suite as reproducible
measurements and as the first test of each mechanism the later days build.

## Decisions

1. **Coordinates at the boundary are the engine's**, the same the importer produces (Phase 22 Day 336, no flip): right-handed,
   +Y up, metres; a camera looks along its entity's -Z (`scene::Camera`'s documented rule), +X right. The header states this
   once, at the transform component, and nothing in the C API converts.
2. **The surface names the camera.** Day 359: `surface_set_world(surface, world)` shows the world through a fixed default
   camera; Day 360 adds `surface_set_camera(surface, camera_entity)`. Several surfaces may show one world through different
   cameras (each surface has its own `Renderer`, `RenderScene` and `SceneLink`, below), and an unbound camera falls back to the default.
3. **Material overrides per slot**, not material creation: an entity's mesh renderer carries per-slot overrides (a texture for the
   base color), so one robot's `Screen` slot differs from another's without a material each.
4. **The frame's order is fixed**: host's input and calls, then `engine_frame` runs animation, transform, render sync, and
   renders every bound surface (Day 359). A host reads poses (joint matrices, world transforms) after `engine_frame` returns.
5. **Joint attachment: later.** Labels follow an entity's origin plus an offset; an entity following another's joint is a later
   item (a read-only query of a joint's world matrix is cheap once Day 363 shows what `AnimationSystem` leaves in place, and is
   listed in `KNOWN-ISSUES.md` if it does not make Day 363).
6. **Labels are the text renderer's 2D path, projected** (Spike 4): constant size in pixels, drawn over the frame with no depth
   test (a label is never hidden by geometry); a size in metres is the pixel size computed from the distance and the field of
   view, rounded to the atlas's sizes. Billboard quads with a depth test and plates are a later item.
7. **ABI version: unchanged.** Phase 23 has not been released, so every addition goes at the end of the function table and of
   the component and caps lists within the same major and minor; the snapshot (`abi/engine_api.snapshot`) and `abi_check` still
   record every change, and the guide says the first release is at the phase's end. (The phase file said "bumped once": there
   is no released version to bump from.)
8. **Golden images**: a comparison passes when no channel differs by more than 4 (of 255) and at most 0.5 % of the channel values
   differ by more than 1. On one GPU the floor is exact equality (Spike 5); the cross-GPU figure could not be taken (below).
9. **What a surface owns**: a frame object (the G-buffer, lighting and tonemap passes, materials, the `renderer::Renderer`; today
   `renderer_demo::DemoFrame`), a `RenderScene` and a `scene::SceneLink`, **per surface, on that surface's device**. A world's
   content (meshes, textures, materials) becomes resident on a surface's device through a per-surface cache keyed by resource id
   and generation, filled the first frame an entity needs it, registered with that surface's `DeviceResources` so a reset
   rebuilds it from the CPU source (every `Gpu*` type already keeps one).

## Spike results

### Spike 1: a renderer on a surface's device

An ECS world (a mesh entity, a directional and an ambient light) goes through `scene::RenderSync` into a `RenderScene`, drawn by a
`DemoFrame` on the device and swapchain of an X11 window opened as a host would (`PlatformWindowHost`, the same device and swapchain
creation as `surface.cpp`'s `Build`): 60 of 60 frames presented, **3.9 ms per frame** (sync, record, submit, present; the debug build with
validation and synchronization validation on), and the same scene on the same device rendered offscreen is not uniform (the cube is there).

Findings the later days act on:

- **`DemoFrame` is in `game/renderer_demo`, not in the engine.** The engine library cannot depend on a game directory: Day 359 moves
  it (the frame object of decision 9, without the demo's overlay and debug view) into the engine, next to `renderer_passes`, and the
  demo and tests use it from there. `engine_c` gains the edges `renderer`, `renderer_passes`, `scene`, `animation` (and `text` for
  labels on Day 367); the layering stays acyclic (`ARCHITECTURE.md` and `modules.txt` change in that commit).
- **A renderer with a swapchain ignores an offscreen target and presents** (`Renderer::RenderFrameImpl`), so the read-back path of
  Day 352 (`ServeReadbacks`, which copies the swapchain image after `EndFrame` and before `Present`) has no place to run. The fix is
  a small hook, `RendererDesc::before_present(texture, user)`, called after the graph executes and the device's `EndFrame`, before
  `Present` (the alternative, rendering offscreen and blitting to the swapchain every frame, adds a full-frame copy for a read-back that
  is rare). Day 359 builds it.
- What the renderer needs from its host: a device, optionally a swapchain, a `jobs::Scheduler` (the engine's), a `DeviceResources`
  per device and the settings (`RenderSettings`; the defaults serve). Nothing else.

### Spike 2: content on a surface's device

Two Vulkan devices coexist in one process (the window's and a headless one). The same 33k-vertex mesh and 1024x1024 RGBA texture
become resident on the first device in 4.2 ms and 4.1 ms and on the second in 3.8 ms and 3.6 ms: **a second device adds about
7 ms for a character-sized mesh plus a large texture, once**, not per frame. So the per-surface cache of decision 9 is cheap, and
sharing one device between surfaces is not worth the cross-device work it would need (a surface's device is chosen by the window it
presents to, Day 352). A device reset rebuilds a cache entry from its CPU source through the existing `Recreatable` path (Phase 8).

### Spike 3: a live texture

A host thread writes a full frame into a latest-wins slot (a lock, a copy, a version) every 2 ms; the render thread, at 60 Hz, takes the
slot if its version is new, copies it into one of three persistently mapped staging buffers (a ring, so a copy never waits on the
GPU) and records one `CopyBufferToTexture` with its barriers. Over 120 frames, with the Vulkan validation layers and synchronization validation on (every spike device is made with both) and no message printed:

| Size | Source updates | Uploads | CPU per upload (copy + record + submit) | Worst |
|---|---|---|---|---|
| 1280x720 (3.7 MB) | 255 | 119 | 0.22 ms | 0.46 ms |
| 320x180 (0.23 MB) | 790 | 119 | 0.09 ms | 0.20 ms |

So updates faster than frames cost **one upload per frame** (and a frame with no new update costs nothing). Row pitch must meet the
device's `buffer_texture_copy_row_pitch_alignment` (256): both sizes do (width a multiple of 64); Day 365 pads other widths into the
staging buffer. GPU time was not measured (the numbers are CPU); the per-frame upload budget (Day 365) defaults to 8 MB, which is two
720p updates. Memory: a live texture holds the texture plus three staging buffers.

### Spike 4: text in the world

`DemoFrame`'s overlay draws through the text renderer's 2D path over a 1280x720 frame; the frame's cost with glyphs on top (the
projection from a world point is arithmetic and is not what costs):

| Glyphs | ms per frame | against none |
|---|---|---|
| 0 | 0.200 | 0 |
| 40 (one name tag) | 0.337 | +0.14 |
| 400 (ten) | 0.443 | +0.24 |
| 4000 (a hundred) | 1.599 | +1.40 |

A name tag costs a fraction of a millisecond and a hundred labels about 1.4 ms: enough for the driving case. Limits recorded: no depth
test (always on top), no plate (Day 367 draws a rectangle behind the text with the UI renderer's quad path or leaves it out), one
glyph size per atlas entry (a size in metres changes the pixel size with distance, so sizes are rounded to a set), DPI follows the
surface's scale.

### Spike 5: golden images

The machine has one GPU and no software rasteriser (`/usr/share/vulkan/icd.d` holds the NVIDIA driver only, no lavapipe), so the
cross-GPU difference **was not measured**; it is listed in `KNOWN-ISSUES.md` as a check no one has done. What was measured: a lit
128x128 sphere rendered on two separate device instances differs in **0 of 65,536 channel values** (the floor), so the rule of
decision 8 only has to absorb other GPUs and drivers, and the goldens of this phase are compared with it. Skinned frames use the same
rule (Day 363).

## Consequences for the days

- Day 359: move the frame object into the engine; `RendererDesc::before_present`; `engine_c` edges; `surface_set_world(surface, world)`
  only (the camera is Day 360).
- Day 360: `surface_set_camera`; Day 365: the staging ring, the latest-wins slot and an 8 MB per-frame budget;
  Day 367: the projected 2D label path of decision 6; Day 368: the per-surface caches rebuild through `DeviceResources`.
- Day 371: goldens use decision 8's rule.
