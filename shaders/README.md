# Shaders

Shaders are compiled offline to SPIR-V and embedded as committed `*.spv.inc` files (PLAN.md assumption #18). Building
the engine never needs a shader compiler: a checkout without one uses the committed outputs as they are.

Two source languages coexist while Phase 4 migrates from GLSL to Slang (Days 69-70; the GLSL sources are deleted on
Day 70):

| Language | Sources | Committed output | Compiler |
|---|---|---|---|
| GLSL  | `shaders/<name>.vert\|frag\|comp` | `shaders/<name>.spv.inc` | `glslc` |
| Slang | `shaders/slang/<name>.vert\|frag\|comp.slang` | `shaders/slang/<name>.spv.inc` | `slangc` |

`-DENGINE_SHADER_LANG=glsl|slang` (default `glsl` until Day 70) chooses which outputs the engine, tests and demos
include. With `slang`, a shader that has no Slang port yet falls back to its GLSL output. The symbol names
(`kTriangleVertSpv`, ...) are the same in both, so no C++ changes between the two.

The first line after the banner of every generated file records the compiler: `// compiler: slangc v2026.18.2`,
`// compiler: glslc 2026.4`. `shaders_check` recompiles with the installed compiler and byte-compares, and **skips**
(prints `SKIP:`) when the installed version differs from the recorded one, so a newer compiler never fails the suite.

## Tools (optional, dev-time)

| Tool | Version pinned | Arch | Windows / macOS |
|---|---|---|---|
| `slangc` | v2026.18.2 | `pacman -S shader-slang` | release archive from github.com/shader-slang/slang (Windows `slangc.exe`, macOS via the release archive or `brew install shader-slang`) |
| `glslc` | 2026.4 | `pacman -S shaderc` | Vulkan SDK |
| `spirv-val` | any recent | `pacman -S spirv-tools` | Vulkan SDK |

Regenerate: `cmake --build build --target shaders`. Check: `ctest --test-dir build -R shaders_check`.

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

`tests/rhi/slang_shader_test.cpp` compares every ported shader against its GLSL original: entry point count, stage,
compute local size, descriptor (set, binding) pairs and interface locations, and runs `rhi::ValidateSpirv`.
