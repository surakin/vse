# Maintenance: glTF coverage: the PBR textures, vertex colours, morph targets and punctual lights

Not a numbered phase. Asked by the user on 2026-11-19, after the list of what glTF 2.0 holds that the engine cannot import: "make a plan to
implement morph targets, vertex colours, punctual lights, dropped occlusion, metallic roughness and emissive textures". Planned the same
day on the Windows PC. **Not started; the decisions below wait for the user's answer.**

## Why

A typical exported model loses these today, so it looks wrong or does not import:
- **The metallic-roughness, occlusion and emissive textures** are dropped with a warning (`gltf_material.cpp`): the G-buffer template has
  only `albedo_tex` and `normal_tex`; roughness, metallic and occlusion are constants, and emissive is a scalar times the albedo.
- **`COLOR_0`** is dropped: the mesh blob (`MESH` v1) has no colour attribute, though the renderer already has one (`gbuffer.vert`'s
  `VERTEX_COLOR` option, location 4 `UNorm8x4` sRGB; the `gbuffer_color` and `gbuffer_skinned_color` templates).
- **Morph targets** are refused (`drop_morph_targets` imports the base shape); `MESH` section 40 (`MORP`) is reserved and refused; `weights`
  animation channels are dropped; nothing in the renderer, the ECS or the clip format holds a morph weight.
- **`KHR_lights_punctual`** is refused when required and ignored otherwise; the renderer has directional, point and ambient lights
  (`renderer::LightType`), no spot light, and its attenuation is `saturate(1 - (d/R)^4)^2 / (d^2 + 1)` with a required radius.

## What exists to build on (read 2026-11-19)

- **G-buffer** (`shaders/gbuffer.frag.slang`, `engine/renderer/gbuffer.*`): target 0 `RGBA8_SRGB` (albedo, a = occlusion), 1 `RG16_Float`
  (octahedral normal), 2 `RGBA8_UNorm` (roughness, metallic, emissive scale, flags). The lighting passes (`lighting.frag`,
  `point_light.frag`, `lighting_common.slang`) add `albedo * emissive_scale` in the ambient term. Material parameters: `base_color`,
  `roughness`, `metallic`, `emissive_scale`, `alpha_cutoff`, `occlusion`, `flags`, `albedo_uv`, `albedo_uv_outside`.
- **Default textures** (`renderer::DefaultTexture`): a slot left unset gets white, black or a flat normal, by the template's
  `texture_defaults` or by its name. **Permutations:** at most 32 keys per shader (`kMaxPermutationKeys`); `gbuffer.frag` has three options
  and 8 keys, so three more options (64 keys) do not fit.
- **Skinning** in `gbuffer.vert` (`SKINNED`: set 3 joint palette, the palette offset in the object's normal matrix); `GpuMesh` is a shared
  unit rebuilt from its `MeshData` on a device reset.
- **Lights in the ECS:** `scene::LightSource` (stored component 60, version 1: type 0..2, color, intensity, radius, direction toward the
  light in entity space), synced by `scene::RenderSync`; a level can hold it; scripts reach it as `light_source`. Stored components have
  versions and upgrade steps (`storage::ComponentInfo`).
- **Clips** (`ANIM` v1): per-joint TRS tracks of one skeleton; the importer drops channels on non-joint nodes and every `weights` channel.
- **Shaders** are minted per target on its machine: SPIR-V anywhere `slangc` is, DXIL on the PC, Metal on the Mac (`shaders/README.md`).
  A shader changed on one machine fails the other targets' staleness tests until they are minted (the unlit-materials series, 2026-11-19).

## Decisions (proposed; the user confirms before Day 1)

1. **The three maps are slots with default textures, not permutations.** `metallic_roughness_tex` (white), `occlusion_tex` (white),
   `emissive_tex` (white, times an `emissive_color` that defaults to black): every G-buffer template has them, a material that leaves one
   unset gets its default, and `gbuffer.frag` samples them always (3 more samples per pixel, measured on Day 1). The alternative, one option
   per map, needs 64 keys (over the 32 limit) or a hand-picked subset.
2. **Emissive is an RGB colour in a fourth G-buffer target**, `RGBA16_Float` (rgb = emissive radiance; 8 bytes per pixel more), written
   by the geometry pass and added once by the fullscreen lighting pass. glTF's emissive is `emissiveFactor * emissiveTexture *
   emissiveStrength`, not a multiple of the base colour, and today's 8-bit scale stops at 1. `emissive_scale` goes (material sources are
   upgraded offline by `asset_mint`), and `material.b` is free for later.
3. **Vertex colours: `MESH` version 2** adds an optional colour attribute (location 4, `UNorm8x4`, sRGB-encoded, as `gbuffer.vert` already
   reads it). glTF's `COLOR_0` is linear: the importer encodes it to sRGB (rounded; the loss is recorded). A glTF material used by a
   coloured primitive gets a `_color` twin on the `gbuffer_color` template (glTF materials do not know the mesh's attributes).
4. **Morph targets: `MESH` version 3** fills section 40 (`MORP`): up to 64 targets per mesh, each a sparse list of vertices with position,
   normal and tangent deltas (`f32`), and the targets' names (`extras.targetNames`). The renderer applies them in `gbuffer.vert` (option
   `MORPHED`, before skinning, as glTF orders them) from a storage buffer owned by `GpuMesh`; per object at most **8 active targets** (the
   largest `|weight|`, ties by index; more are dropped and counted). A dense layout or a compute pre-pass is the alternative if Day 7
   measures the vertex shader too slow.
5. **Morph weights in the ECS:** a stored component `MorphWeights` (64 `f32`, a count), set by levels (`mesh.weights`, `node.weights`),
   scripts and animation; `RenderSync` copies it to the render object.
6. **Weight animation: `ANIM` version 2** adds weight tracks (one per animated node: its target count, keyed like a scale channel) and
   lets a clip have no skeleton when it animates weights only. The alternative is a separate clip type.
7. **Lights:** a `Spot` light type (inner and outer cone, glTF's smooth angular falloff) lit by the point-light volume with a cone test in
   the shader; `LightSource` version 2 gains `inner_cone`/`outer_cone` (an upgrade step from version 1). **Units:** glTF's point and spot
   intensity is candela and directional is lux; the importer multiplies by the settings `assets.import.light_intensity_scale` (point and
   spot, default 1) and `assets.import.sun_intensity_scale` (directional, default 1/10000, so 10,000 lux of daylight is 1), and a light
   with no `range` gets the radius where its attenuation falls below `assets.import.light_cutoff` (default 1/256) of its intensity at 1 m.
   The engine's attenuation stays as it is (it has the `d^2` falloff and a window to zero; the difference to glTF's recommended form is
   recorded).
8. **Where the days run:** on the Linux machine, as the other series do, except the two days that mint DXIL (the PC) and Metal (the Mac).
   Between them, the DXIL and Metal staleness tests fail on the other machines (as for the unlit materials), recorded in
   `KNOWN-ISSUES.md`. Alternative: the whole series on the PC, which mints SPIR-V and DXIL together, then the Mac.

## Not wanted

- glTF's other material extensions (clearcoat, sheen, transmission, ...), `TEXCOORD_1` (a map's `texCoord` other than 0 stays a warning),
  per-texture samplers (a material keeps the first texture's sampler), node (non-joint) TRS animation, cameras, morph deltas of texture
  coordinates or colours, compressed geometry (Draco, meshopt). Each is its own later piece.
- Changing the engine's light attenuation or adding physical camera exposure.

## Days

1. [Metallic-roughness and occlusion maps in the G-buffer](gltf-coverage/01-metal-rough-occlusion-maps.md)
2. [Emissive colour: a fourth G-buffer target and the emissive map](gltf-coverage/02-emissive-target.md)
3. [The importer: the three maps and the emissive colour](gltf-coverage/03-import-pbr-maps.md)
4. [Vertex colours in the mesh blob (`MESH` v2)](gltf-coverage/04-mesh-colour-stream.md)
5. [The importer: `COLOR_0`](gltf-coverage/05-import-vertex-colours.md)
6. [Morph targets in the mesh blob (`MESH` v3)](gltf-coverage/06-mesh-morph-section.md)
7. [Morph targets on the GPU](gltf-coverage/07-gpu-morph-targets.md)
8. [Morph weights in the ECS and in levels](gltf-coverage/08-morph-weights-component.md)
9. [Weight tracks in clips (`ANIM` v2) and their playback](gltf-coverage/09-clip-weight-tracks.md)
10. [The importer: morph targets and weight animation](gltf-coverage/10-import-morph-targets.md)
11. [Spot lights](gltf-coverage/11-spot-lights.md)
12. [The importer: `KHR_lights_punctual`](gltf-coverage/12-import-punctual-lights.md)
13. [The PC: DXIL and Direct3D 12](gltf-coverage/13-windows-dxil.md)
14. [The Mac: Metal](gltf-coverage/14-macos-metal.md)
15. [Closing: the guides, the model viewer, the full runs](gltf-coverage/15-closing.md)

Days 1-3, 4-5, 6-10 and 11-12 are four independent strands (each needs only the earlier days of its own strand); Days 13 and 14 need every
shader day before them; Day 15 needs all.

**Definition of done:** a glTF with metallic-roughness, occlusion and emissive textures, `COLOR_0`, morph targets with a weight animation and
`KHR_lights_punctual` lights (directional, point, spot) imports without those warnings or refusals and draws right on Vulkan, Direct3D 12
and Metal: each feature has a golden picture or a pixel test against the CPU reference, the model viewer shows a sample of each, the
refusal of `KHR_lights_punctual` and morph targets is gone, and every format change has its version bump, offline upgrade and hostile-input
test.

**Rules for every day:** the usual day rules: scoped runs (`scripts/test-phase.sh --affected`, the sanitizer flavors for the lifetime and
GPU-owner code), `/code-review high`, `docgen lint` at zero and `docs/reference` regenerated for a changed public header, reference-style
updates of `docs/guide/assets.md` and `docs/guide/renderer.md` (the narrative waits for Day 15), `STATUS.md` and `KNOWN-ISSUES.md`, commit and
push. A GPU owner keeps its CPU source and registers with `DeviceResources` (the reset rule); a new shared unit goes in
`tools/shared_unit_check/units.txt`; a blob format change bumps its version and remints the committed blobs; minting stays deterministic.
