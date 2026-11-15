# Tutorial: A 3D Scene in Your Window

**Goal:** Fill a window your program owns with a 3D scene through the C API: a camera you move, a light, meshes, an animated skinned character, a texture your own thread paints, and a name tag in the world.

**You need:** the program of [Load the Engine and Clear a Window](loading-the-engine.md) working (a built tree, X11 or Xwayland, a Vulkan GPU). The program of this page is `tests/engine_library/walk_around.c`; `cmake --build build --target walk_around` builds it and `./build/tests/engine_library/walk_around` runs it (W A S D move, the mouse looks, Escape quits); `--frames 40` makes it draw 40 frames, rebind to a new window and check the picture is the same. The guide: [The Engine as a Loadable Library](../guide/engine-library.md).

## Steps

1. **Add the content once.** `resource_add` hands the engine the bytes of a mesh, a material, a skeleton and a clip (blobs `asset_mint` made, or a glTF imported at run time with `import_gltf`); each has an id of yours. The engine copies them and every surface the world is drawn on makes them resident on its own device.
2. **Make a world and fill it with entities.** `world_create`, then `entity_create` and `component_set` for each thing: `ENGINE_COMPONENT_TRANSFORM` places it; `ENGINE_COMPONENT_MESH_REFERENCE` names the mesh and the material to draw; `ENGINE_COMPONENT_SKELETON_INSTANCE` gives a skinned mesh its skeleton and `animation_play` starts a clip (a speed of 0 holds it still).
3. **Light it.** A directional light is an entity with `ENGINE_COMPONENT_LIGHT` whose -Z axis is the way it shines; `world_set_environment` sets the ambient term, the sky colour and the exposure.
4. **Look through a camera.** A camera is an entity with `ENGINE_COMPONENT_CAMERA`; `surface_set_camera` makes a surface look through it, and moving the entity moves the view. A surface made no camera shows the default view.
5. **Show the world.** `surface_set_world` binds the world to the surface; every `frame` call then draws it. One world can be bound to several surfaces, each with its own camera.
6. **Feed a texture from another thread.** `texture_create` makes a picture of your own; `entity_set_slot_override(world, entity, "albedo_tex", ...)` with `ENGINE_SLOT_HOST_TEXTURE` makes one entity draw it in place of its material's texture (the entities that share the material keep theirs); any thread calls `texture_update` and the next frame shows the newest pixels. Destroying the texture puts the slot back.
7. **Put a name tag over a head.** `label_font_set` gives the engine a font file once; an `ENGINE_COMPONENT_LABEL` on an entity draws its text at the entity's position plus an offset, in pixels or in metres, hidden with the entity.
8. **Survive what hosts do.** `surface_rebind` after the window or the device is lost remakes everything the surface owned from what the engine keeps; the world, the camera, the texture and the label are yours to leave alone.
9. **Clean up in order.** Stop your painting threads, destroy the textures, the surface and the world, then the engine.

## Where to go next

- [Load the Engine and Clear a Window](loading-the-engine.md) for the first half of the story.
- The guide's sections on cameras, lights, hierarchy, skinned meshes, glTF levels, host textures and labels.
