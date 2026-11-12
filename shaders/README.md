# Shaders

Shaders are compiled offline to SPIR-V and DXIL and embedded as committed `*.spv.inc` and `*.dxil.inc` files (PLAN.md assumption
#18). Building the engine never needs a shader compiler: a checkout without one uses the committed outputs as they are.

Sources are Slang: `shaders/<name>.vert|frag|comp.slang`, outputs `shaders/<name>.spv.inc` (the SPIR-V words) and `shaders/<name>.refl.inc` (the reflection blob and block constants), compiled by `slangc` and turned into includes by `tools/shader_mint`. The
symbol names (`kTriangleVertSpv`, ...) come from the file name. The GLSL path (`glslc`, Phase 4) was retired on
Phase 5 Day 70 after the Slang outputs reproduced every golden image unchanged.

The second line of every generated file records the compiler: `// compiler: slangc v2026.18.2`. `shaders_check`
recompiles with the installed compiler and byte-compares; a shader whose recorded version differs from the installed
one is noted and not compared, and the test reports SKIP only if nothing could be compared, so a newer compiler never
fails the suite.

## Tools (optional, dev-time)

| Tool | Version pinned | Arch | Windows / macOS |
|---|---|---|---|
| `slangc` | v2026.18.2 | `pacman -S shader-slang` | release archive from github.com/shader-slang/slang (Windows `slangc.exe`, macOS via the release archive or `brew install shader-slang`) |
| `spirv-val` | any recent | `pacman -S spirv-tools` | Vulkan SDK |
| `dxcompiler.dll` + `dxil.dll` (DXIL) | 1.9.2602.17 | the Arch `dxc` package aborts (`KNOWN-ISSUES.md`): no DXIL on Linux | the Windows SDK's `bin/<version>/x64` |

**Where the build finds them (Phase 13 Day 163).** `slangc`: `ENGINE_SLANGC` (a cache or environment variable), then `PATH`, then
`C:/Tools/slang-2026.18.2/bin` (where `BUILD.md` puts it on Windows). DXIL: `ENGINE_DXC` (a `dxc` or its directory), else the
Windows SDK the toolchain found (`ENGINE_SDK_BIN`); never the first `dxc` on `PATH`, which on the Windows PC is the Vulkan SDK's
older 1.9.0.5512. `slangc` is given that directory with `-dxc-path`, so it loads that `dxcompiler.dll` and `dxil.dll` (which signs the
container; `shader_mint` refuses an unsigned one). The configure step prints `DXIL: minting with dxcompiler <version> from <dir>`.
The recorded compiler line of a `.dxil.inc` is `// compiler: slangc v2026.18.2, dxcompiler 1.9.2602.17`.

Per shader, the `shaders` target runs `slangc ... -o x.spv -reflection-json x.json`, `spirv-val`, then `shader_mint`, which reads the JSON with nlohmann/json (vendored, `PLAN.md` assumption #19, this tool only), cross-checks the reflection against the SPIR-V (descriptor set/binding pairs, compute local size, vertex input locations) and writes both includes. `-reflection-json` omits `space` for set 0, lists unused parameters (only those the entry point uses are reflected), and reports a vertex input's location as `index`.

Regenerate: `cmake --build build --target shaders`. Changing a shader requires `slangc`: without it neither this target nor `shaders_check` exists and the build silently keeps using the committed outputs. Check: `ctest --test-dir build -R shaders_check`.

## How Slang is invoked

```
slangc <name>.slang -target spirv -profile spirv_1_5 -entry main -stage <vertex|fragment|compute> \
       -matrix-layout-column-major -O2 -o <name>.spv
slangc <name>.slang -target dxil -profile sm_6_0 -entry main -stage <vertex|fragment|compute> \
       -matrix-layout-column-major -O2 -DENGINE_TARGET_D3D12=1 -dxc-path <dir> -o <name>.dxil
```

The arguments live in one place, `slang_compile_args()` and `slang_dxil_args()` in `names.cmake`, shared by the build and
`shaders_check`. Each shader is one file with one entry point named `main`; the stage comes from the file name. Shader model 6.0
is the D3D12 baseline (`docs/superpowers/specs/2026-11-07-windows-port-design.md`, decision 10).

## Targets: one source for SPIR-V and DXIL (Phase 13 Day 163)

`shaders/target_common.slang`, included by every vertex shader, holds what differs between the targets; the DXIL build defines
`ENGINE_TARGET_D3D12`:

- **Clip space.** `output.position = ENGINE_CLIP(...)`: the D3D12 build negates Y, because D3D12's clip space has Y up and the
  engine's projection (`PerspectiveVulkan`) is Vulkan's, with Y down. With the flip in the shader, everything else is one
  convention on both: the CPU projection and its inverse, the light scissor rectangles, the UV to NDC mapping of the fullscreen and
  deferred passes, `SV_Position` in pixels and the front-face rule (counter-clockwise on screen on both). Depth is 0..1 on both.
  (The Phase 13 plan first named a D3D projection, `PerspectiveD3D`; Day 163 chose the shader flip because the fullscreen passes and
  the depth reconstruction in `lighting.frag` compute clip positions without the projection.)
- **Vertex index.** `ENGINE_VERTEX_ID` is `SV_VulkanVertexID` on SPIR-V and `SV_VertexID` on D3D12 (the same number for a
  non-indexed draw that starts at vertex 0, which is all the vertex-pulling shaders do).
- **Object index.** `ENGINE_OBJECT_INDEX_STREAM` is defined on D3D12, whose shaders have no base instance. Material vertex shaders
  (and the light volume) then read the index from an instance-rate attribute, `[[vk::location(15)]] uint instance : TEXCOORD15`,
  fed by the renderer's object index stream (`MaterialSystem::ObjectIndexStream`, the values `0 .. 65535`, bound at vertex-buffer
  slot 1 by the G-buffer pass and slot 0 by the light volume); D3D12 fetches it at `StartInstanceLocation + SV_InstanceID`, so the
  renderer still draws object `i` with `first_instance = i`. A device says it needs this with `DeviceCaps::object_index_stream`.
  SPIR-V keeps `SV_VulkanInstanceID` (decided on Day 163: the Vulkan output stays byte for byte as it was, so no Vulkan draw cost
  was measured). The Metal build defines it too (Phase 26 Day 315): Metal's `[[instance_id]]` excludes the base instance as
  D3D12's does, and the stream is the same attribute (`[[attribute(15)]]` in the Metal output).
- **Registers (Phase 26 Day 315).** A resource is declared with `ENGINE_REGISTER(t0, space2)`, which is `: register(t0, space2)` in
  the DXIL build and nothing in the others. Slang also takes `register()` as the Metal slot, and Metal has no spaces, so `b0` in
  two spaces would put two parameters at `[[buffer(0)]]`, which the Metal compiler rejects (found on Day 298 in `gbuffer.vert`
  and `lighting.frag`). Without it Slang gives every Metal resource a slot of its own; the SPIR-V and DXIL outputs were checked
  unchanged by the macro.

Two rules every shader follows, which `shader_mint` checks:

- **A vertex input at location N has the semantic `TEXCOORD<N>`** (`[[vk::location(2)]] float2 uv : TEXCOORD2`): D3D12 matches
  input elements by semantic, and the DX12 backend names location N that way. Checked on every machine (the SPIR-V reflection
  carries the semantics). Semantics do not change the SPIR-V.
- **Registers follow the Vulkan bindings:** `[[vk::binding(b, g)]]` is declared with `register(<class><b>, space<g>)`, the class
  `b` for a constant buffer, `t` for a texture or a `StructuredBuffer`, `u` for a RW buffer or storage texture, `s` for a sampler;
  push constants are `register(b0, space15)`. The DX12 root signature is built from the bind group layouts with exactly these
  registers. Checked against the DXIL build's reflection when DXIL is minted. A read-only `StructuredBuffer` reflects as
  `rhi::BindingType::ReadOnlyStorageBuffer` (a shader resource view on D3D12), a `RWStructuredBuffer` as `StorageBuffer` (an
  unordered access view).

**DXIL outputs.** `<name>.dxil.inc` holds `k<Base>Dxil`/`k<Base>DxilByteCount`, `k<Base>DxilSpirvFnv` (the FNV-1a hash of the SPIR-V
it was minted beside), each extra permutation module's DXIL (`k<Base>Perm<N>Dxil`), and the shader's blob set `k<Base>Shader`
(`rhi::ShaderBlobSet`, both formats); `<name>.perm.inc` includes it. Without DXIL tools the `shaders` target leaves the committed
`.dxil.inc` alone, so a shader changed and minted on Linux keeps old DXIL until it is minted on Windows: `shader_dxil_test` compares
the recorded hash with the committed SPIR-V on every machine and names the shader. The mint is deterministic: two runs give the same
bytes, no path is embedded, and the recorded `slangc` version is `v<version>` on both platforms.

## What each flag and convention prevents

- **`-matrix-layout-column-major`** (also the default for the SPIR-V target in this version; it is passed anyway so a
  default change cannot silently transpose every matrix). The engine's `Mat4` is column-major and is copied byte for
  byte into std140 uniform blocks; `mul(M, v)` must see the same matrix. Slang's naming is the reverse of GLSL's
  (the same layout comes out as GLSL `layout(row_major)` with `v * M`), so judge it by behavior: the row-major
  setting emits the other decoration and would transpose every matrix. End-to-end proof is the cube golden images
  when the 3D shaders are ported on Day 70.
- **`-fvk-invert-y` stays off.** `PerspectiveVulkan` already flips Y; inverting again would render upside down. The
  triangle SPIR-V contains no `OpFNegate` on the position, and the GPU golden image is unchanged.
- **`SV_VertexID` is not `gl_VertexIndex`.** In HLSL semantics it means `VertexIndex - BaseVertex`, which makes Slang
  emit the `DrawParameters` capability. That needs the `shaderDrawParameters` device feature, which the RHI does not
  enable. Use `SV_VulkanVertexID` (maps to the raw `VertexIndex` builtin), as `fullscreen.vert.slang` does.
- **Descriptors are written `[[vk::binding(binding, set)]]`** (binding first). Without it Slang assigns bindings by
  declaration order, which drifts when a declaration is added. Set 0 = frame, 1 = material, 2 = per-object (spec R5).
- **Uniform blocks are `ConstantBuffer<T>`** and come out as std140 (`Params_std140`), matching the host structs.
  **Storage buffers are `RWStructuredBuffer<T>` / `StructuredBuffer<T>`** and come out as std430 (`ArrayStride`).
  `-fvk-use-gl-layout` / scalar layout flags are not needed and not used.
- **Push constants are `[[vk::push_constant]] ConstantBuffer<T>`** (std430, offset 0). One block per shader, as in GLSL.
- **Storage images need an explicit format**: `[[vk::image_format("rgba8")]]`, or Slang emits `Unknown` and the
  validation layers complain on a write-only store without a format capability.
- **Array lengths**: `GetDimensions()` on a structured buffer replaces GLSL's `data.length()`; keep the bounds check
  (dispatches are rounded up to the workgroup size).
- **Entry point name** is `main` in the source and in the SPIR-V (`-entry main`); `rhi::ValidateSpirv` looks for it.
- **`[[vk::location(n)]]`** pins stage inputs/outputs; without it Slang numbers them by semantic order.

`tests/rhi/shader_interface_test.cpp` checks every committed shader without the compiler: entry point count, stage,
compute local size, descriptor (set, binding) pairs, interface locations, std140/std430 member offsets (the lit block
is 176 bytes, matching the C++ struct), `NonReadable`, push-constant blocks and that no capability beyond
Matrix/Shader/ImageQuery is used; it also runs `rhi::ValidateSpirv`.

## Other targets (checked by `shaders_portability`, for Phases 13 and 26)

`ctest -R shaders_portability` (skipped without `slangc`; `portability.cmake`) compiles every shader and every permutation
key with `-target hlsl` (with the D3D12 define) and `-target metal` (with the Metal define), using the same entry, stage and
`-matrix-layout-column-major` flags as the SPIR-V build, and fails on any error. If `dxc` is installed it also compiles the HLSL to
DXIL: on Windows with the SDK's `dxc` a rejection fails the test, elsewhere it is a note. On a Mac `tools/metal_mint` compiles every
key's Metal source with the driver as well, and a rejection fails the test. The compute test shaders `fill.comp` and `saxpy.comp`
take their element count as a push constant (`{k, count}`, `{a, count}`) instead of asking the buffer with
`RWStructuredBuffer.GetDimensions`, which Metal cannot do (Phase 26 Day 315), so every shader compiles to every target; where the
target can ask (SPIR-V, DXIL) the count is also bounded by the buffers' lengths. Per target:

- **Clip-space Y.** Vulkan's clip space has Y down, D3D12 and Metal have Y up, and `-fvk-invert-y` only applies to SPIR-V. The
  D3D12 and Metal builds flip it with `ENGINE_CLIP` (above). Depth is 0..1 on all three.
- **Matrix layout is the same bytes on all three.** With `-matrix-layout-column-major` the HLSL starts with
  `#pragma pack_matrix(column_major)` and keeps `mul(M, v)`; the Metal output declares column-major storage structs
  (`_MatrixStorage_float4x4_ColMajor...`). `row-major` flips both (`#pragma pack_matrix(row_major)`) and would transpose every
  matrix, so never drop the flag.
- **Metal renames `main`** (`warning E40100: entry point 'main' has been renamed to 'main_0'`), because `main` is
  reserved in Metal Shading Language. `tools/metal_mint` reads the name the minted library exports and `shader_mint` records it
  beside the blob (`<Base>MetallibEntry`, carried by `rhi::MetalLibBlob`), so `ShaderDesc::SetCode` passes it and no backend
  guesses. The test accepts this warning and prints any other.
- **Metal flattens descriptor sets to argument slots.** Each resource gets a `[[buffer(n)]]`, `[[texture(n)]]` or `[[sampler(n)]]`
  index, not a set and binding: in `gbuffer.vert` the frame block is `[[buffer(0)]]` and the object buffer `[[buffer(1)]]`, in
  `lighting.frag` the four G-buffer textures are `[[texture(0..3)]]` and the frame, lights and push blocks `[[buffer(0..2)]]`.
  `shader_mint` reads the Metal build's reflection and records each binding's slot (`ReflectedBinding::metal_slot`, and the
  push-constant block's), and rejects two resources in one slot; the Metal backend binds from those (Phase 26 Day 318).
- **No base instance in D3D12 or Metal shaders.** Handled by the object index stream (above).

## Metal libraries (Phase 26 Day 315)

Every shader is also minted into `<name>.metallib.inc`: an AIR-only, executable `.metallib` (`<Base>Metallib`), the function it
exports (`<Base>MetallibEntry`, `main_0`) and the FNV-1a hash of the SPIR-V it was minted beside (`shader_metallib_test` compares
it). The `.dxil.inc` includes it and its blob set (`<Base>Shader`) holds all three formats; a permuted shader's `.perm.inc` lists
each module's library. Minting needs a Mac: `slangc -target metal` writes the Metal Shading Language, and `tools/metal_mint`
compiles it with the driver's own compiler and serializes it through the undocumented `-[MTLLibrary serializeToURL:error:]`, the
**interim route the user chose on Day 298, pending until they have a new Mac** (`KNOWN-ISSUES.md`); the tool checks the file loads
as an executable library with one function of the right stage before writing it, and fails with a message naming
`KNOWN-ISSUES.md` if the call is missing. The bytes embed the driver compiler's version and the macOS build (the `.metallib.inc`'s
`// compiler:` line records both), so the same source mints the same bytes only on the same macOS build: `shaders_check` compares the
committed libraries only on a Mac whose build matches, and mints each library twice to check determinism. Off a Mac the committed
`.metallib.inc` files are used as they are, and the Metal build's reflection (which needs no Mac) still makes the `.refl.inc`, byte
for byte as on a Mac. A shader added, or a permuted shader whose modules change, on a machine without a Mac gets a placeholder
`.metallib.inc` (null blobs, `// compiler: none (not minted)`), so everything still builds; `shader_metallib_test` then names it, and
the next `shaders` run on a Mac mints it.

## Permuted shaders (Day 72)

A `<name>.perm` manifest next to `<name>.slang` makes the shader permuted (see the guide, `docs/guide/rhi-shaders.md`,
and the renderer spec R10). The source declares its options on a `// permutation-options: A B C` line in the first 60
lines and uses them as `#ifdef A` / `#if defined(A)`; the manifest lists the keys: `key -` (required, the base module)
and `key A B`. `shader_mint --permute` runs `slangc` once per key with `-DA=1 -DB=1`, mints each module, dedupes
identical ones and writes `<name>.perm.inc` beside the usual pair. Limits: 16 options, 32 keys per shader.

- **Each module has its own reflection**: a resource a variant does not use is reported as unused and left out of that
  module's layout (verified for a plain `#ifdef` and for a branch on a constant derived from the option), so layouts
  must be built per module.
- Every option must appear in the source outside the declaration line, or `shader_mint` rejects the shader (a typo would
  otherwise build identical modules silently).
- Compiler version: the recorded `// compiler:` line is checked like any shader's; `shaders_check` regenerates the
  whole table and compares it.
- `shader_mint` starts `slangc` itself in this mode (`run_process.h`: `posix_spawnp` on Linux, `CreateProcessW` on Windows since
  Phase 13 Day 163), once per key for SPIR-V and once more for DXIL when DXIL is minted. Modules are deduplicated on SPIR-V and
  reflection only, so a machine without DXIL tools writes the same table; a module's DXIL is its first key's.


## Material shaders (Day 74)

Shaders drawn through `renderer::MaterialSystem` follow one descriptor-set convention: set 0 is the frame uniform
(`renderer::FrameData`, binding 0), set 1 the material (one uniform block, sampled textures, samplers), set 2 the
per-object storage buffer (`renderer::ObjectData`, binding 0). `material_test.vert/.frag` are the reference.

- **Read the object index from `SV_VulkanInstanceID`, not `SV_InstanceID`** (SPIR-V), and from the object index stream under
  `ENGINE_OBJECT_INDEX_STREAM` (D3D12; "Targets" above). The renderer draws object `i` with `first_instance = i`. Slang's
  `SV_InstanceID` subtracts the base instance (`gl_InstanceIndex - gl_BaseInstance`), so it is 0 for every one-instance draw and
  every object would read entry 0; `SV_StartInstanceLocation` would add the base back but needs the `shaderDrawParameters` device
  feature, which the RHI does not enable (and shader model 6.8 on D3D12, above the 6.0 baseline). `SV_VulkanInstanceID` is
  `gl_InstanceIndex` and needs nothing.
- **Set 2's object buffer is a `StructuredBuffer`** (read-only, `ReadOnlyStorageBuffer`): the material system's set 2 layout says
  so, and a D3D12 shader resource view must match it.
- A vertex input the shader does not use is removed from the SPIR-V while the reflection still lists it, which
  `shader_mint` rejects as a location mismatch. Pass unused attributes through to an output (the test shader forwards
  the tangent to a varying the fragment shader declares).
