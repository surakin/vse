# Maintenance: a UV transform on a material slot, so a host can fit video to a screen without copying pixels

Not a numbered phase. Request from the host project (2026-11-18), written against the engine at `ed6d8327`. Nothing here is started.

## Why

The host draws each participant's live video on the `Screen` material slot of their avatar, as a host texture (`ENGINE_SLOT_HOST_TEXTURE` on `albedo_tex`
through `entity_set_mesh_slot_override`). The screen is a 16:9 quad with 0..1 UVs. Video comes in many shapes: a portrait phone camera, a 4:3 webcam, a
screen share that changes size mid-call. Today the frame is uploaded at its own size and fills the whole quad, so every non-16:9 picture is stretched.

The fix the user wants is texture mapping: leave the pixels alone and change how the quad samples them (the picture is scaled to fit inside the screen,
centred, never cropped or stretched, with the rest of the screen in a dark colour). That avoids a CPU copy and re-pad of every frame, lets the texture keep the
video's own size, and turns a size change into a few floats instead of a texture rebuild.

## What the engine has now (checked in `engine_c.h`, `renderer/gbuffer.h`)

- `entity_set_mesh_slot_override(world, key, mesh_slot, slot, value)` sets, for one mesh slot of one entity: `base_color` (`ENGINE_SLOT_COLOR`), `roughness`, `metallic`,
  `emissive_scale`, `alpha_cutoff`, `occlusion` (`ENGINE_SLOT_FLOAT`), and the textures `albedo_tex` and `normal_tex`.
- No slot carries a texture-coordinate transform. The only `UvOffset`/`UvScale` in the engine (`engine/asset/mesh.h`) decodes packed vertex coordinates and is not
  settable by a host.
- The `Screen` is a mesh slot of the avatar's mesh, not an entity, so the host cannot scale the screen on its own by transforming something.

## Missing

1. **A UV transform slot (required).** A material slot `albedo_uv` (4 floats: scale u, scale v, offset u, offset v; uv' = uv * scale + offset), settable per mesh slot
   through `entity_set_mesh_slot_override` and `entity_set_slot_override`, default `(1, 1, 0, 0)` so existing materials and glTF imports draw as before. A new
   `ENGINE_SLOT_FLOAT4`-style kind or an existing kind that already carries `value[4]` (`ENGINE_SLOT_COLOR` is linear RGBA; do not reuse it: its value is colour-managed).
   Ask: which fits the override struct best. Optional second transform for `normal_tex` is not needed.
   Also in the G-buffer shaders, the albedo sample uses the transformed coordinates.

2. **Outside the picture shows a colour, not a smear (required).** With a scale above 1 the coordinates leave 0..1. The sampler's address mode today (to be checked
   in the renderer: clamp or repeat) would repeat the picture or stretch its edge row across the bars. Wanted: a slot flag (or a second slot, `albedo_uv_outside`, a
   linear RGBA) that makes samples outside 0..1 return that colour. Either a shader mask on the transformed UV or a border address mode (not available on every
   backend: Metal has a border colour only for a few fixed values, so the mask is the portable choice). Note that the picture's edge pixels must not bleed into
   the bars through bilinear filtering: the mask should be applied to the coordinate before filtering, or the texture sampled with the 1-texel inset.

3. **A rule for the preview and the golden images (small).** The off-screen golden scene (`engine_c_acceptance_scene`) should keep its current picture with the default
   transform, and a new case should show a 4:3 and a 9:16 test texture fitted into the 16:9 screen with the colour bars, equal on Vulkan, Direct3D 12 and Metal
   within the existing tolerance.

## Not needed

- Cropping ("fill" mode): the user wants fit, never crop. A host can compute a crop with the same transform (scale below 1) if it ever wants one, so nothing extra is asked.
- Reading the screen's real aspect back from the mesh: the host assumes 16:9 (the avatar format fixes it); a later request can add it if artists want other shapes.
- Rotation of the UVs: not needed for fitting.

## What the host does with it

For a frame of aspect `a = w / h` on a screen of aspect `s` (16:9): if `a > s` the picture is wider than the screen, so scale v by `a / s` and offset v by
`(1 - a / s) / 2`; if `a < s` scale u by `s / a` and offset u by `(1 - s / a) / 2`; equal aspect gives the default transform. Set the transform when the
frame size changes, not every frame.

## Tests this needs

- The default transform draws every existing scene unchanged (all current goldens).
- A 4:3 and a 9:16 texture on a 16:9 screen: the centred picture, the exact bar colour, no bleed of edge pixels into the bars (pixel checks on the seams), same on the
  three graphics APIs.
- A transform set on one slot leaves the entity's other slots and other entities that share the material untouched.
- Clearing the overrides (`entity_clear_slot_override`) restores the default transform; a host texture destroyed or without pixels still reverts to the material's own
  value as today (`slots_reverted`).

## Result

Done on Linux, 2026-11-18.

- Slots `albedo_uv` (`ENGINE_SLOT_VEC4`, a new kind: four plain floats, not colour-managed) and `albedo_uv_outside` (`ENGINE_SLOT_COLOR`, linear RGBA) on the G-buffer material (parameter block 48 -> 80 bytes, defaults (1,1,0,0) and (0,0,0,0)), set per mesh slot or per entity through the existing override calls.
- `gbuffer.frag`: uv' = uv * scale + offset for the albedo only. When the outside colour's alpha is above 0, the shader masks coordinates outside 0..1 (the portable choice, no border address mode) and keeps the sampled coordinate half a texel inside the edge, so no bleed under repeat or clamp. Alpha 0 draws as before.
- Tests: `host_acceptance_scene` fits 4:3 (picture 0.76 of the screen width, bars 13 + 13 of 108 px) and 9:16 (0.315) pictures into the 16:9 screen and checks centring, the bar-picture-bar order (no bleed), that the other robots' screens and the body are untouched, that clearing the transform stretches the picture with no bars, that a destroyed host texture counts in `slots_reverted`, and that the robot's own screen comes back. `host_scene` checks the argument validation. The default transform leaves the existing checks of the scene unchanged. The material test expects the 80-byte block.
- Runs: plain `ran 3955 of 10622 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan 3250, tsan 2905; failing: the known three plus the DXIL and Metal minted-hash tests (the PC and Mac re-mint `gbuffer.frag`). `/code-review high`: 4 findings; the validation test was added; the rest are recorded in `KNOWN-ISSUES.md` (hard mask edge; blobs minted earlier; no golden image or cross-API comparison yet, only pixel checks on Vulkan).

