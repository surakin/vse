# Off-screen surfaces and multi-material meshes: design (Phase 24 addendum, Day 373)

Why: the host draws its whole UI through its own canvas, so the engine view must be an image the host draws, and its
`robot.glb` and `green_field.glb` need every primitive drawn with its own material (the user, 2026-11-17).

## Checked against the code (2026-11-17)

- Read-back tickets: at most 256 (`engine/engine_c/surface.cpp`, `LIMIT_EXCEEDED`); import tickets also 256 (`import.cpp`).
- `scene::SpawnLevel` (`engine/scene/level_spawner.cpp`) gives each entity one material, the level's `MeshReference.material`.
- The C API's surface scene (`engine_c/surface_scene.cpp`) has its own sync (it reads `engine_c` `MeshReference` with
  `material_resource`), separate from `scene::MakeRenderSyncSystem`; both emit one `RenderObject` per entity today.
- Slot overrides (`SlotOverrides`, at most 8 per entity) replace *material parameter slots by name* (`base_color`, ...), a
  different thing from a mesh's material slots (`asset::MeshSlot`, up to 64 per mesh). The text below says "mesh slot" for
  the second.
- `asset::MeshSlot {name, first_index, index_count}` exists in the blob; `LoadGpuMesh` ignores it.

## 1. Several materials on one mesh (Spike 1: decision (a))

- `renderer::MeshGeometry` gains `ranges` (up to `kMaxMeshSlots` index ranges, from the blob's slots, empty for a mesh
  without slots), `GpuMesh::RangeCount()/Range(i)`. `GpuMesh` stays one vertex and index buffer.
- `RenderObjectDesc` gains `first_index` and `index_count` (0 and 0 = the whole mesh, today's behaviour). One object per
  (entity, slot); the sync emits them. The object's bounds are the mesh's bounds for every range (conservative: a range
  is never culled wrongly; tighter per-range boxes cost vertex scans and buy only fewer draws of ranges off screen).
- The G-buffer's sort key stays (pipeline, material, mesh); the range is the tie-break after it, and instancing merges only
  objects with the same mesh, material, pipeline *and range*.
- A skinned entity's objects all point at one palette. The frame writes each distinct `joint_matrices` pointer once and
  shares its offset (Day 374), so five slots do not upload five palettes.
- Why not a multi-draw object: it would give every consumer of `RenderObject` (shadows, picking, culling, instancing) a
  list to loop, where today each sees one draw. A range per object leaves them unchanged except for the draw call.

Numbers (Spike 1) are taken by the Day 374 benchmark (five-slot skinned robot, 1,000 two-slot trees) and recorded in that
day file; the design does not depend on them (the alternative is not built).

## 2. Material per slot (Spike 5 and 6)

- A mesh slot is named by index or by its imported name (`asset::MeshSlot::name`, up to 31 bytes). The scene component
  `scene::MeshRenderer` gets `slot_materials` (a small fixed array of material indices, one per slot, 0 = the base
  `material`), so an entity with no per-slot materials is unchanged. An entity whose mesh has no slots draws the whole mesh.
- A slot index at or above the mesh's slot count is refused at set time (`INVALID_ARGUMENT`); a mesh swapped for one with
  fewer slots draws the slots it still has and counts the dangling ones (`slot_materials_dangling` in the frame report).
  Names resolve to an index when set; a renamed slot is a new mesh (a minted asset is immutable).
- C API: `engine_mesh_reference` does not grow. A new table function `entity_set_slot_material(world, entity, slot,
  material_resource)` (slot as index; `entity_set_slot_material_by_name` takes the imported name) and
  `entity_clear_slot_materials`. Table functions are appended (ABI rule). `slot_override` keeps working per entity: it
  clones the material of each slot it touches (a private copy per (entity, slot), counted against the same 8 overrides,
  recorded in KNOWN-ISSUES if an entity then needs more).
- Levels (Spike 6): `asset::MeshReference` gains a `slots` list (a material resource id per mesh slot, 0 = the
  material of the entity). The level format version is bumped, the fixtures are re-minted offline, no load-time
  conversion (Phase 22 current-version-only rule). The glTF importer emits one slot per primitive with that
  primitive's material, and a primitive without material references the importer's default material.

## 3. Off-screen surfaces (Spikes 2 to 4)

- `engine_surface_info.kind = ENGINE_SURFACE_OFFSCREEN` (5) uses `width`, `height` and `scale` only; `display`, `surface`,
  `window_id` and `callbacks` are ignored (NULL is fine). Capability bit `ENGINE_CAP_SURFACE_OFFSCREEN`.
- The surface owns a colour render target (RGBA8, sRGB-encoded as a window shows it, straight alpha: the background
  colour's alpha is kept) and a ring of 3 mapped read-back buffers with a fence each. Rows are tightly packed in the
  frame the host sees (the engine copies out of the aligned staging row stride), `stride = width * 4`.
- `surface_acquire_frame(surface, &frame)` returns the newest finished frame (pixels pointer, width, height, stride,
  sequence number, frame id), skipping older finished ones (counted `frames_dropped`); it never waits on the GPU: with
  nothing finished it returns `NOT_READY`. `surface_release_frame` returns the slot to the ring. The renderer never writes
  a slot the host holds; when all slots are held or in flight the engine skips the copy for that frame (counted
  `frames_skipped_slow_host`), so a slow host costs dropped frames, never a stall. Two outstanding acquires are allowed
  (a ring of 3 = one being written, two readable).
- An acquired frame stays valid until released or until `surface_destroy`; `surface_resized` makes new frames the new size
  and leaves held ones valid at their own size. Any one thread at a time reads a frame; the engine's `frame` is the writer.
- Device (Spike 4): the default adapter, as a window surface; the engine's several surfaces share a device as Day 369's
  rule says (one device per adapter on Direct3D 12). A window surface and an off-screen surface of one engine coexist.
- Input stays what `surface_input` does for any surface; the host forwards input of its own canvas.
- Not built: GPU texture sharing with the host (the pixels are copied out), a transparent window.

## 4. Reset and loss

Render target and ring are device objects rebuilt through `renderer::DeviceResources` from their size (they own no CPU
pixels worth keeping); a held frame's mapped memory is a copy owned by the surface and stays valid across a reset. A lost
device or reset rebuilds the target and drops in-flight copies (counted), never the acquired frame.

## Measurements

The spikes are in `2026-11-17-offscreen-and-multimaterial-spikes/` (analysis files); the timings they leave open are taken
by the benchmarks of Days 374 and 379 and recorded in those day files.

## Amendments (Days 374-380)

- The level change is an additive stored component (`asset::MeshSlotMaterials`, id 52), not a version bump.
- The spikes are analysis only; their timings were not taken (`KNOWN-ISSUES.md`).
- The acquired frame is a CPU copy the engine keeps (two held at most), not a pointer into the mapped ring buffer.
