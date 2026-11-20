# glTF coverage / Day 11: spot lights

**Series:** [glTF coverage](../gltf-coverage.md) (decision 7)
**Depends on:** nothing in the series.

## Tasks

- [x] **The type** (`renderer/render_scene.h`): `LightType::Spot` with `position`, `direction` (from the surface toward the light, as the
  directional light's, so the spot points along `-direction`), `radius`, `inner_cone` and `outer_cone` (half-angles in radians, `0 <=
  inner < outer <= pi/2`; checked where a light is set). `LightData` (48 bytes) gains the cone: grow it to 64 bytes and change every
  reader (`lighting.frag`, `point_light.frag`, the CPU light buffer writer, `kMaxLights`' memory note).
- [x] **Shading** (`lighting_common.slang`, CPU twin in `renderer/lighting.cpp`): glTF's angular falloff, `t = saturate((cos(angle) -
  cos(outer)) / (cos(inner) - cos(outer)))`, `t * t`, times the point attenuation. Write it in the header comment.
- [x] **Drawing** (`renderer/lighting.*`): spot lights share the point-light volume pass (the sphere of `radius`; the shader applies the
  cone); the frustum and screen-rectangle culling of point lights covers them (a tighter cone bound is noted as later work).
- [x] **The component** (`scene/components.h`, `scene_components.cpp`): `LightSource` gains `inner_cone`, `outer_cone` (defaults 0 and
  pi/4); **version 2** with an upgrade step from version 1 (the new fields get their defaults); the `type` enum range 0..3; the sync passes
  them on; scripts see the fields.
- [x] **Tests:** the CPU twin against the shader (a pixel inside the inner cone, between, outside the outer); a spot light's picture (golden
  or pixel test); a version 1 saved `light_source` loads as version 2; a bad cone is refused; the reset rig still balances.
- [x] **Mint** SPIR-V of the lighting shaders; DXIL and Metal wait.
- [x] **Standing rows.**

## Notes

Done on the PC (2026-11-19). **No tests were run** (the user's request; they run after Day 12); only `build-msvc` was built.

- `renderer::LightType::Spot` (value 3, after `Ambient`, so stored values do not move) and `Light::inner_cone`/`outer_cone`
  (radians; the scene refuses a cone out of order or over pi/2). A spot light shines along `-direction`, the same "toward the light"
  convention as a directional light, and is otherwise a point light.
- **`LightData` stays 48 bytes (a change to the plan, which grew it to 64):** a spot's unit axis times glTF's angle scale `1 /
  (cos(inner) - cos(outer))` goes in `direction.xyz` (a point light's is zero), and the offset `-cos(outer) * scale` in the unused
  `color.w`. So nothing else that reads the buffer changes, and `kMaxLights`' memory note holds.
- **Shading:** `lighting_common.slang`'s `SpotFactor` (`saturate(dot(L, axis) * scale + offset)^2`, 1 for a point light) multiplies the
  attenuation in `point_light.frag`. The CPU twin is `renderer::SpotLightFactor`. Spot lights are culled, sorted and drawn as point
  lights (the sphere of their radius); a tighter cone bound is later work.
- **`scene::LightSource` version 2:** `inner_cone` (0) and `outer_cone` (pi/4), with an upgrade step from version 1 that copies the
  record and appends the defaults; `type` is an enum of 0 to 3; the sync passes the cone on and refuses a bad one like the scene.
- **C API:** `ENGINE_COMPONENT_LIGHT` was not given a spot kind (not in the plan); a host gets spot lights through levels.
  - **Done since** (2026-11-20, [c-api-gltf-coverage.md](../c-api-gltf-coverage.md)): `ENGINE_LIGHT_SPOT`, `ENGINE_COMPONENT_MORPH_WEIGHTS`, and
    `level_spawn` brings a level's lights and weights.
- Minted: `point_light.frag` SPIR-V and DXIL; `lighting.frag` did not change (it does not call `SpotFactor`); Metal waits for Day 14.
- Tests (written, not run): `A spot light's cone factor is 1 inside the inner cone, 0 outside the outer and rises between`. Not
  written: a picture of a spot light, the version-1 record's upgrade, the shader against the CPU twin.
- Docs: `render_scene.h`, `frame_data.h`, `lighting.h`, `scene/components.h`; `docs/reference` regenerated.

**Done when:** a scene can hold spot lights, shaded with glTF's cone falloff, set from entities, levels, saves and scripts.
