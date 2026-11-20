# Maintenance: engine C API for streamed (huge) worlds, and for editing inside a host's 3D view

Not a numbered phase. Request from the host project (2026-11-19). Nothing is decided on the host side for part B; part A is what any streamed world needs.
Read from `include/engine/engine_c.h` on master (`73b5ed59`): what exists is listed first, so the gaps are only what is missing.

## Why

The host wants rooms to carry huge worlds: a tree of content-addressed manifests in Matrix media, a tile pack per leaf (a `.glb` or an engine-native level plus its
textures, 1 to 20 MB), fetched on demand around the camera over HTTP. It may later also want editing tools inside its 3D view. The host owns the network and the
cache; the engine owns loading, residency and the scene.

## What exists

- A memory resource backend, filled by the host: `resource_add`, `resource_add_many` (+ `_async`), `resource_replace`, `resource_remove`, `resource_mounted`,
  `resource_read`; `BORROW` reads the host's bytes in place. `resource_load` / `resource_load_async` load by id or path with the dependency closure.
- `import_gltf` (+ async), `level_spawn`, and entity and component calls (`entity_create/destroy`, `component_set/get/remove/has`, `query`, `entity_name_*`,
  `entity_find`, `entity_set_parent`).
- Picking pieces: `surface_pixel_ray`, `physics_raycast`.

## A. Streaming (wanted first)

1. **A pull provider.** Today only the host pushes. Wanted: the host registers a callback (or a request queue the host drains) so that a load of an id or path that no
   backend holds becomes "pending on the host" instead of NOT_FOUND. The host answers later, from any thread, with bytes (or an error), using the same
   `engine_resource_add_info` contract. `resource_load_async` reports the pending state through its ticket. The callback must not be called with the world held, and
   the same id must not be requested twice at once.
2. **Request control.** A request carries a priority the host can read and the engine can change (the camera moved), and can be cancelled when nothing needs it any
   more. Say what happens to a load that depended on a cancelled request.
3. **Residency.** Report, per loaded resource, whether anything holds it and when it was last used (frame number), and let the host set a memory budget that the
   engine reports against (bytes loaded, bytes over budget). The host decides what to `resource_remove`; today it has no way to know what is idle.
4. **Level lifecycle.** `level_spawn` has no counterpart that removes the entities of one spawned instance. Wanted: a handle (or a root entity) for an instance and
   `level_despawn`; an async spawn (the level and its closure are loaded off the frame thread); and a spawn transform so tiles of one world can be placed without the
   host rewriting the blob. Check what `level_spawn` returns first; this may already be partly true.
5. **Cheap level of detail swap.** Replacing the coarse tile with the fine one should not make a visible gap: say whether spawn-then-despawn in one frame is
   atomic, or give a way to make it so.

## B. Editing inside the host's view (only if the host builds an editor; not urgent, but stable ids are cheapest to add early)

6. **Stable ids.** Entity keys are runtime values and names need not be unique. Wanted: an id component that survives save and load and is unique within a world, or a
   documented uniqueness rule for `entity_name_*`, so two clients can name the same object in an edit.
7. **Scene export.** Serialize a set of entities (a root and its children, or everything with a tag) with their components into a blob that `level_spawn` can load
   again, deterministically (the same scene gives the same bytes, so an unchanged tile keeps its content hash). Without this the host would walk every component kind and
   write a format of its own.
8. **Change tracking.** "Which entities or components changed since tick N", so a host can send live edit operations and know which tiles are dirty.
9. **Editor drawing.** A way to draw a selection outline, gizmo handles and debug lines on top of a scene from the host's side. Say what exists in the debug facilities
   already, if anything usable.

## Not wanted

- The engine fetching anything over the network itself. The host does it; the provider is only the hook.
- Any Matrix concept in the engine.
- A save format that depends on runtime keys.

## Tests this needs

- A provider that answers late: a load pending on it completes after the answer, and a load of an unknown id fails when the host answers with an error.
- Cancelling a request that has dependents; changing a priority.
- A level spawned twice at different transforms, one despawned: the other is untouched.
- Export, spawn, export again: the bytes are identical.
