# Maintenance: scripts set a mesh's material slots, and bind host textures by name

Not a numbered phase, no day files. Request from a host project (2026-11-24). The host's robots have a screen: a material slot named `Screen` on the robot's mesh. The host
binds a video texture to it, makes it glow (`emissive_from_albedo`), and, when the picture is not 16:9, sets `albedo_uv` and `albedo_uv_outside` so the picture
is centred with black bars. That is all C++ (`SetMeshSlotOverride`) because a script cannot touch a material slot. The host wants this in a Lua behavior, with
its C++ only creating the texture and uploading its pixels.

## Wanted

1. **`mesh.set_slot(entity, slot, field, value)`**: the script form of `mesh_slot_override`, with the same checks: `slot` is the material slot's name (`"Screen"`),
   `field` the name of the slot's field (`"albedo_tex"`, `"emissive_from_albedo"`, `"albedo_uv"`, `"albedo_uv_outside"`, any the material declares), and `value`:
   a number (a float field), a `vec4` or a `color` table, or a string naming a host texture (below). A world-write command applied at the merge. Errors as for the
   C call (`NOT_FOUND` when the entity's mesh has no such slot or field, `TYPE_MISMATCH` when the value is not of the field's kind), as script errors naming the
   slot and field. `mesh.clear_slot(entity, slot, field)` removes an override.
2. **Host textures by name.** The host gives a texture a name when it makes it (`engine_texture_info.name`, today only for diagnostics, or a new
   `texture_set_name`), unique in the world; a script passes that name as the string `value` of a texture field and the engine binds the texture it names. The same
   name made again by the host (a new texture after the old one is destroyed) is found again; a name that matches nothing is a script error. The script never sees
   the texture object, only its name, and cannot change its pixels (those stay the host's, `texture_update`).
3. **Reading a slot back** is not needed.
4. **Which entity.** The slot override applies to the entity that carries the mesh, as in the C call (the host finds it by trying the entities of a spawned level);
   so a script needs `entity.find` of the mesh node by name, which exists. Say in the guide that a slot override on an entity without that slot is an error,
   not a no-op, and that `mesh.has_slot(entity, slot)` (a world-read boolean) lets a script try the entities of a spawn.

## Tests

A C API test next to `host_script_scene.c`: a script sets a float, a vec4 and a color field of a slot and the host draws the world and checks the picture (or reads
the override back with the C API); a texture named by the host is bound by name, the host updates its pixels and the picture changes; the errors of 1 and 2
(no such slot, no such field, wrong kind, unknown texture name); `mesh.has_slot`; `clear_slot` brings the material's own value back.

## What the host does with it

`assets/worlds/robot.lua` finds the mesh entity with the `Screen` slot (`mesh.has_slot`), binds `"screen:<participant id>"`, sets the glow, and fits the
picture to the 16:9 screen from a `screen_size` event (width, height) that the host sends whenever the video's size changes. The host's C++ only makes the
named texture and uploads the pixels.

## Done

- [x] the bindings and the texture name
- [x] C API test
- [x] guide text (scripting.md)
- [ ] checked on Windows and macOS (see `KNOWN-ISSUES.md`)
