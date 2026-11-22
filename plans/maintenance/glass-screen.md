# Maintenance: a display that glows and reflects (a host's avatar screen)

Not a numbered phase. Request from a host project (2026-11-22), after `script-sky-and-lights.md` (done; thank you).

## Why

The robot avatars have a flat screen for a face that shows the participant's video. Today it is an ordinary lit material with the picture in `albedo_tex`
(fitted to the quad with `albedo_uv`, black bars with `albedo_uv_outside`). The meadow now has a day and night cycle (a script sets the sky, the ambient
light and two directional lights), and that exposes two problems with a screen that should look like a real computer display:

1. **It does not glow.** At night the only light is a weak moon and an ambient of about 0.08, so every face is nearly black. A display emits its own light.
2. **It does not reflect.** A real screen is glass: it shows a sharp highlight of the sun, and at a glancing angle it mirrors the sky.

## What exists, and the gap

- `emissive_tex` and `emissive_color` exist, but the picture's aspect fit (`albedo_uv`, `albedo_uv_outside`) applies to the albedo only, so putting the video
  in `emissive_tex` loses the fit (the video's aspect differs from the 16:9 quad); copying the pixels to letterbox them is what `albedo_uv` was made to avoid.
- `KHR_materials_unlit` makes a whole material unlit, which also removes the glass's highlight.
- Specular from a directional light follows `roughness`; we have lowered the screen's to 0.05, so the sun and moon leave a glint. There is no environment
  reflection: the sky is a flat colour behind the scene and no material reflects it.

## Wanted

1. **A way to make the albedo picture emissive.** For example a slot `emissive_from_albedo` (float, default 0): the surface adds `albedo texel * base_color *
   that value` to its light, after the lights, using the albedo's own texture coordinates, so `albedo_uv` and `albedo_uv_outside` apply to it. Settable
   as a per-entity override (`SetMeshSlotOverride`) and, if cheap, a glTF-less material parameter. The black bars (`albedo_uv_outside`) glow too (black stays black).
2. **A constant environment reflection.** Lit surfaces reflect `engine_environment.sky_color` (times a host-set `reflection_intensity`, default 0 so nothing
   changes) with a Fresnel term, so glass and shiny materials pick up the sky colour at a glancing angle: no cube map, no screen-space work. If a better,
   still cheap, option exists (a small prefiltered sky gradient from the sky colour and a ground colour), say so. Any engine-wide setting is fine
   (an `engine_environment` field, set from the script's `environment.set` too).

## Not wanted

Planar or screen-space reflections, probes, ray tracing, any Matrix or client concept in the engine.

## Tests this needs

- A quad with an albedo texture and `emissive_from_albedo` 1 draws the picture in a dark world (ambient 0, no lights); with the slot at 0 it is black.
  `albedo_uv` scale and offset move the glow with the picture, and the bars stay black.
- With `reflection_intensity` above 0, a smooth (roughness 0.05) dielectric quad seen at a glancing angle is closer to `sky_color` than the same quad seen head on, and
  with 0 the output equals today's (no change in existing picture checksums).
- Windows and macOS shaders re-minted like the other lighting changes.

## Built (2026-11-22, Linux; no day files)

- `emissive_from_albedo` (float slot, default 0): `albedo * value` is added to the G-buffer's emitted light, after the picture's fit and bars; settable
  with `entity_set_slot_override` and `entity_set_mesh_slot_override`.
- `engine_environment.reflection_intensity` (appended; a struct_size that ends before it means 0) and `environment.set`/`environment.get` field of the same name:
  lit surfaces add `sky_color * reflection_intensity * F`, F = Schlick from F0 (0.04, or the albedo for a metal) up to `max(1 - roughness, F0)`. Done in the
  lighting pass (push constants: the intensity in `ambient.a`, the sky in the former pad words); a flat colour, as asked. A sky gradient with a ground
  colour is the next step if wanted (one more environment field).
- Test `engine_c_glass_screen` (dark world: black at 0, the picture at 1, the fit moves the glow, bars stay black; head-on vs glancing reflection; the old
  struct size). Vulkan shaders re-minted; Direct3D 12 and Metal outputs are the other machines' to mint.
