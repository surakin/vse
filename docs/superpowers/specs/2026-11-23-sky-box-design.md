# Sky box, cube textures and sky lighting: design

Date: 2026-11-23 (Phase 28, Day 478). Decisions of the user (2026-11-23): the cube map is part of the sky box, the procedural gradient is the default
when no cube map is assigned, and the feature order is sky box, terrain, instanced meshes, hierarchical instanced meshes.

## What is built

1. **A cube texture in the RHI** (`TextureDimension::Cube`, today rejected): 6 square faces, mips, sampled only.
2. **Cube maps as minted assets**: six faces or one equirectangular image in, one blob out, loaded as a reset-safe `GpuTexture`.
3. **A sky pass** drawn after lighting: a procedural sky (gradient and sun disc) or the cube map, behind everything.
4. **Sky lighting**: reflections and ambient light read the sky (cube or procedural) instead of a flat colour.
5. **The sky from hosts**: the environment resource, the C API, and scripts.

## Cube textures in the RHI (spike 1)

No binding-layout change is needed: `BindingType::SampledTexture` names no dimension, the dimension belongs to the texture and its default view. So the
RHI change is the texture's creation and view, per backend:

- **Validation (`rhi/desc.cpp`)**: `Cube` is accepted with `width == height`, `depth == 1` and `array_layers == 6` (cube arrays, a multiple of 6, are
  rejected with a message); usage `Sampled | CopyDst | CopySrc` only (no render target, no storage in this phase); the check "cube textures are not
  supported yet" in `TextureDesc::Validate` and in `VulkanDevice::CreateTexture` is replaced.
- **Vulkan**: `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT` in the image create flags (and in the `flags` argument of the
  `GetPhysicalDeviceImageFormatProperties` probe, which passes 0 today), view type `VK_IMAGE_VIEW_TYPE_CUBE` for the default view
  (`vulkan_device_textures.cpp:273`, which picks 3D, 2D array or 2D by the description). Uploads and copies already address face and mip as
  `array_layer` and `mip_level`; they need no change.
- **Direct3D 12**: the resource stays a 6-slice `Texture2D` array; only the shader resource view changes to `D3D12_SRV_DIMENSION_TEXTURECUBE` with
  `MipLevels` (`dx12_device_resources.cpp:696-726` chooses the dimension). The subresource index of a face is `mip + face * mips`, which the existing
  copy code (`dx12_command_list.cpp:304`) already computes.
- **Metal**: `MTLTextureTypeCube` in the texture descriptor (`metal_device_resources.mm:658-677`); `replaceRegion` and `copyFromBuffer` take the face as the
  slice, which the existing code passes as `region.array_layer`.
- **Caps**: `rhi::Caps::cube_textures`, true on all three backends and on the null device. Slang `TextureCube<float4>` compiles to SPIR-V, DXIL and
  Metal without a workaround; the `shader_mint` reflection must name the dimension of a texture so a 2D texture bound where a cube is expected is caught.
- **Face order and handedness**: +X, -X, +Y, -Y, +Z, -Z (`asset::kCubeFaces`), the order of all three APIs; the direction-to-face mapping is the
  APIs' common one (the same on Vulkan, D3D12 and Metal), written in the guide with a table, proven by Day 482's six-colour test. The engine's world is
  right-handed with +Y up; a cube map's +Y face is the sky overhead.

Result of the spike: no backend needs a workaround. Direct3D 12 and Metal are small changes made on Days 502 and 503.

## Stored format and minting (spike 4)

The RHI has `RGBA8_UNorm` (sRGB variant), `RGBA16_Float` and `RGBA32_Float`; it has no `RGB9E5` or `R11G11B10`. The sky is stored as:

- **HDR: `RGBA16_Float`**, 8 bytes per texel; a 512x512 cube with its full chain is 6 x 512 x 512 x 8 x 4/3 = 16.8 MB. Source: raw little-endian
  half-float RGBA images (six, or one equirectangular).
- **LDR: `RGBA8_UNorm` sRGB**, 4 bytes per texel, from raw 8-bit RGBA (the texture minter's existing input).

The engine reads no PNG, JPEG or HDR file at run time and the minter reads none either, as the texture minter does today; a conversion from `.hdr` or
`.exr` is the user's tool outside the repository.

## The prefiltered chain (spike 2)

Choice: **prefilter at minting**, deterministically, stored in the asset. For a cube marked `environment`, mip 0 is the sharp image (what the
background draws) and mips 1..N are the **specular chain**: mip `i` is the image convolved with a GGX lobe of roughness `i / (N)` (squared for
alpha), so the shader reads `reflection = textureLod(cube, reflect(v, n), roughness * (levels - 1))`. A cube that is not marked `environment` keeps
the ordinary box-filtered chain.

- Cost measured (a throwaway program, one thread, -O2): mips 1 to 8 of a 512 cube, 128 samples per texel = 67.1 million samples in **1.2 s**. Minting
  time is not a problem, and the order of operations is fixed so the output is byte-identical at any thread count (the faces are minted in parallel,
  each texel's samples are summed in one fixed order).
- Size: no bigger than the ordinary chain (the same mip count and texel format).
- Rejected: a GPU prefilter at load (a compute pass on every backend, a second code path, results that differ by driver, nothing to gain in size).

## Ambient light from the sky (spike 3)

Choice: **9-coefficient spherical-harmonic irradiance**, 27 floats (9 per channel, 108 bytes), computed at minting for an `environment` cube and, for
the procedural sky, on the CPU from the gradient when the settings change. The lighting shader evaluates the irradiance at the surface normal
(`E(n) = sum A_l L_lm Y_lm(n)` with the cosine-lobe factors pi, 2pi/3, pi/4) in place of the flat ambient colour.

- Accuracy measured against a brute-force cosine-weighted integral on a gradient sky with a bright sun (200,000 directions): worst error **2.7%**
  over six normals (1.1% straight up), so the approximation is fine for ambient light. The ambient term of the old flat model is the special case
  where only the first coefficient is non-zero.
- With no sky assigned (mode `Color`) the flat values apply unchanged and a regression test pins today's images.

## The sky pass

- A full-screen triangle (the existing `fullscreen.vert`), after the lighting pass and before the output step, in the HDR colour target.
- It reads the depth buffer and writes only pixels at the far plane (`depth == 1`), so draw order does not matter and nothing behind a mesh is
  shaded. In mode `None` the pass is not in the graph.
- The view ray is rebuilt in the shader from the inverse view-projection in the frame block (no new per-frame data beyond the sky values).
- Push constants stay within 32 bytes and use scalars (a `float3` aligns to 16 in HLSL and Slang: the lighting pass's lesson).
- `renderer::SkySettings`: mode (`None`, `Color`, `Procedural`, `Cube`), the gradient colours (`zenith`, `horizon`, `ground`), the sun (direction,
  colour, angular size), `horizon_blend`, `exposure`, `rotation` (yaw), `tint`, and the cube texture as a `GpuTexturePtr`. `Color` reproduces
  `SceneFrame::SetEnvironment`'s background and is the default, so nothing changes until a sky is assigned.

## Reset and ownership

The cube texture is a `GpuTexture` (a shared unit, aliases `GpuTexturePtr`/`GpuTextureConstPtr`) with its CPU source kept (the loaded asset), registered
with `renderer::DeviceResources` at its tier, never cached by handle past a frame (`docs/guide/renderer.md`, "Surviving a reset"). The sky pass
holds the owner and reads `Handle()` when it records. A missing or not-yet-loaded cube draws the procedural sky (the id is reported by the asset
system, the frame is never invalid).

## Hosts

The environment (`Environment` in `engine_c`) gains the sky fields as appended, `struct_size`-guarded values; a host cube texture is uploaded from six
faces and kept as a CPU source by the engine for a surface rebind; scripts set the sky through `environment.set` masks; all of it follows the pattern of
`reflection_intensity`.

## Out of scope

Sun and atmosphere scattering, clouds, a time-of-day system, cube arrays, dynamic reflection probes, a GPU prefilter, rendering into a cube.
