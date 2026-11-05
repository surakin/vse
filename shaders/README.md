# Shaders

Shaders are compiled offline to SPIR-V and embedded as committed `*.spv.inc` files (PLAN.md assumption #18). Building
the engine never needs a shader compiler: a checkout without one uses the committed outputs as they are.

Sources are Slang: `shaders/<name>.vert|frag|comp.slang`, outputs `shaders/<name>.spv.inc` (the SPIR-V words) and `shaders/<name>.refl.inc` (the reflection blob and block constants), compiled by `slangc` and turned into includes by `tools/shader_mint`. The
symbol names (`kTriangleVertSpv`, ...) come from the file name. The GLSL path (`glslc`, Phase 3) was retired on
Phase 4 Day 70 after the Slang outputs reproduced every golden image unchanged.

The second line of every generated file records the compiler: `// compiler: slangc v2026.18.2`. `shaders_check`
recompiles with the installed compiler and byte-compares; a shader whose recorded version differs from the installed
one is noted and not compared, and the test reports SKIP only if nothing could be compared, so a newer compiler never
fails the suite.

## Tools (optional, dev-time)

| Tool | Version pinned | Arch | Windows / macOS |
|---|---|---|---|
| `slangc` | v2026.18.2 | `pacman -S shader-slang` | release archive from github.com/shader-slang/slang (Windows `slangc.exe`, macOS via the release archive or `brew install shader-slang`) |
| `spirv-val` | any recent | `pacman -S spirv-tools` | Vulkan SDK |

Per shader, the `shaders` target runs `slangc ... -o x.spv -reflection-json x.json`, `spirv-val`, then `shader_mint`, which reads the JSON with nlohmann/json (vendored, `PLAN.md` assumption #19, this tool only), cross-checks the reflection against the SPIR-V (descriptor set/binding pairs, compute local size, vertex input locations) and writes both includes. `-reflection-json` omits `space` for set 0, lists unused parameters (only those the entry point uses are reflected), and reports a vertex input's location as `index`.

Regenerate: `cmake --build build --target shaders`. Changing a shader requires `slangc`: without it neither this target nor `shaders_check` exists and the build silently keeps using the committed outputs. Check: `ctest --test-dir build -R shaders_check`.

## How Slang is invoked

```
slangc <name>.slang -target spirv -profile spirv_1_5 -entry main -stage <vertex|fragment|compute> \
       -matrix-layout-column-major -O2 -o <name>.spv
```

The arguments live in one place, `slang_compile_args()` in `names.cmake`, shared by the build and `shaders_check`. Each
shader is one file with one entry point named `main`; the stage comes from the file name.

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

## Other targets (checked on Day 70, for Phases 5-6)

`slangc -target hlsl` and `-target metal` produce `lit.vert` and `lit.frag` without errors. Two things to settle then:

- **Clip-space Y is not flipped.** The emitted HLSL and Metal write `SV_Position`/`[[position]]` unchanged. Vulkan's
  clip space has Y down, D3D12 and Metal have Y up, and `-fvk-invert-y` only applies to SPIR-V. The engine's
  `PerspectiveVulkan` bakes the flip into the matrix, so those backends need the un-flipped projection (or a flip in a
  shader epilogue behind a define). The shaders themselves are correct as written.
- **Metal renames `main`** (`warning E40100: entry point 'main' has been renamed to 'main_0'`), because `main` is
  reserved in Metal Shading Language. A Metal pipeline cannot look the entry up as `main`: when that backend is built,
  either give the entry points real names and pass them through (the RHI already takes an entry name) or look up the
  renamed one. `-fvk-use-entrypoint-name` is the matching flag on the SPIR-V side.

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
- `shader_mint` starts `slangc` itself in this mode (`posix_spawn`, so Linux only for now; Phase 5 revisits it for
  Windows).

