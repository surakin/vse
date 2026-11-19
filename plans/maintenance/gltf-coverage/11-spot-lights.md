# glTF coverage / Day 11: spot lights

**Series:** [glTF coverage](../gltf-coverage.md) (decision 7)
**Depends on:** nothing in the series.

## Tasks

- [ ] **The type** (`renderer/render_scene.h`): `LightType::Spot` with `position`, `direction` (from the surface toward the light, as the
  directional light's, so the spot points along `-direction`), `radius`, `inner_cone` and `outer_cone` (half-angles in radians, `0 <=
  inner < outer <= pi/2`; checked where a light is set). `LightData` (48 bytes) gains the cone: grow it to 64 bytes and change every
  reader (`lighting.frag`, `point_light.frag`, the CPU light buffer writer, `kMaxLights`' memory note).
- [ ] **Shading** (`lighting_common.slang`, CPU twin in `renderer/lighting.cpp`): glTF's angular falloff, `t = saturate((cos(angle) -
  cos(outer)) / (cos(inner) - cos(outer)))`, `t * t`, times the point attenuation. Write it in the header comment.
- [ ] **Drawing** (`renderer/lighting.*`): spot lights share the point-light volume pass (the sphere of `radius`; the shader applies the
  cone); the frustum and screen-rectangle culling of point lights covers them (a tighter cone bound is noted as later work).
- [ ] **The component** (`scene/components.h`, `scene_components.cpp`): `LightSource` gains `inner_cone`, `outer_cone` (defaults 0 and
  pi/4); **version 2** with an upgrade step from version 1 (the new fields get their defaults); the `type` enum range 0..3; the sync passes
  them on; scripts see the fields.
- [ ] **Tests:** the CPU twin against the shader (a pixel inside the inner cone, between, outside the outer); a spot light's picture (golden
  or pixel test); a version 1 saved `light_source` loads as version 2; a bad cone is refused; the reset rig still balances.
- [ ] **Mint** SPIR-V of the lighting shaders; DXIL and Metal wait.
- [ ] **Standing rows.**

## Notes

(written during the day)

**Done when:** a scene can hold spot lights, shaded with glTF's cone falloff, set from entities, levels, saves and scripts.
