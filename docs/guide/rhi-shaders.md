# Shaders: from GLSL to a shader module

> **Audience:** engine and renderer programmers writing shaders  
> **Covers:** Phase 3, Day 58; Phase 4, Day 69 (Slang)

## Source of truth

- `shaders/` (GLSL sources, the generated `*.spv.inc` files, the build and check scripts)
- `shaders/slang/` (the Slang sources and their generated files) and `shaders/README.md` (flags and pitfalls)
- `tools/spv_embed/` (the tool that turns a SPIR-V binary into an include file)
- `engine/rhi/spirv.h` (the sanity check every `CreateShader` runs)
- `PLAN.md` assumption #18 (the shader toolchain decision)

## API reference

- [[rhi::ShaderDesc]] — a shader module to create from bytecode
- [[rhi::ValidateSpirv]] — the module sanity check
- [[rhi::Device]] — `CreateShader` and `DestroyShader`
- [[rhi::vulkan::VulkanDevice]] — the Vulkan device and its shader lookup

## How shaders get into the engine

The engine never compiles shaders at runtime and contains no shader compiler. A shader is written in GLSL 4.50,
compiled offline by `glslc` to SPIR-V, and embedded in the program as an array of 32-bit words:

```
shaders/triangle.vert              GLSL source (the interface is documented in its header comment)
shaders/triangle.vert.spv.inc      generated: kTriangleVertSpv and kTriangleVertSpvWordCount, committed
```

The generated files are committed, so a checkout without a shader compiler builds and runs as it is. When you
change a `.vert`, `.frag` or `.comp` file, run:

```
cmake --build build --target shaders
```

That runs `glslc --target-env=vulkan1.3 -O`, then `spirv-val` if it is installed, then `spv_embed`, and rewrites the
`.inc` file. Commit the source and the `.inc` together. The `shaders_check` ctest recompiles every shader and
compares it with the committed file; it skips, with a message, when the installed `glslc` is not the version that
generated the files (the compiler and version are recorded in each file's `// compiler:` line), and it is not
registered at all when no shader compiler is installed.

To add a shader, put the source in `shaders/` and add its name to `ENGINE_SHADERS` in `shaders/CMakeLists.txt`.
The array is named from the file: `triangle.vert` becomes `kTriangleVertSpv`, in namespace
`engine_placeholder::shaders`.

## The Slang workflow (Phase 4)

Phase 4 moves the engine's shaders from GLSL to Slang, compiled by `slangc` to the same SPIR-V (PLAN.md assumption
#18). During the migration both languages are built side by side, and the GLSL workflow above stays valid until
Day 70 removes it.

```
shaders/slang/triangle.vert.slang      Slang source (the interface is documented in its header comment)
shaders/slang/triangle.vert.spv.inc    generated: same symbol names as the GLSL output, committed
```

`cmake --build build --target shaders` runs `slangc` (with the arguments from `slang_compile_args()` in
`shaders/names.cmake`), `spirv-val`, then `spv_embed`. `shaders_check` covers both languages and skips a language
whose compiler is not the recorded version. Configure with `-DENGINE_SHADER_LANG=slang` to make the engine, tests
and demos include the Slang outputs (shaders not ported yet fall back to GLSL); the default stays `glsl` until Day
70. To add a Slang shader, put `<name>.slang` in `shaders/slang/` and add its name to `ENGINE_SLANG_SHADERS`.

Pitfalls that cost time and how the sources avoid them (each is explained in `shaders/README.md`): write
`[[vk::binding(binding, set)]]` explicitly; use `SV_VulkanVertexID`, not `SV_VertexID` (which needs the
`shaderDrawParameters` device feature); give storage images an `[[vk::image_format]]`; keep `-fvk-invert-y` off
because `PerspectiveVulkan` already flips Y; keep `-matrix-layout-column-major` so a `Mat4` copied into a uniform
block is not transposed.

The long-term plan for shader languages and for the DX12 and Metal backends is an open decision (`PLAN.md`
assumption #18); the RHI takes precompiled bytecode in [[rhi::ShaderDesc]]. If the decision is per-backend blobs,
`ShaderDesc` gains a format tag, and nothing else in the RHI changes; whichever is chosen must flip clip-space Y for
Direct3D 12 and Metal (the RHI follows Vulkan's convention). See the portability review, sections 2 and 3.

## Creating a shader module

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    NullDevice* device = CreateNullDevice();

    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.words = shaders::kTriangleVertSpv;
    vs.word_count = shaders::kTriangleVertSpvWordCount;

    ShaderDesc fs;
    fs.stage = ShaderStage::Fragment;
    fs.words = shaders::kTriangleFragSpv;
    fs.word_count = shaders::kTriangleFragSpvWordCount;

    ShaderHandle vertex = device->CreateShader(vs);
    ShaderHandle fragment = device->CreateShader(fs);
    std::printf("valid: %d %d\n", vertex.IsValid() ? 1 : 0, fragment.IsValid() ? 1 : 0);

    device->DestroyShader(vertex);
    device->DestroyShader(fragment);
    DestroyDevice(device);
}
```

```output
valid: 1 1
```

`CreateShader` checks the descriptor, then runs [[rhi::ValidateSpirv]] on the words, and only then gives them to the
driver, so garbage, a truncated module or a module for another stage fails with a logged error and an invalid handle
instead of reaching the driver. The default entry point name is `main`; names may be up to 63 bytes. A shader handle
follows the usual deferred rule: after `DestroyShader` the module lives until the GPU has finished the frames that
could have used it.

## What the engine accepts

The check is a subset check of what the engine's shaders use, not a validator (`spirv-val` is the validator, run by
the `shaders` target):

- SPIR-V 1.0 to 1.6, the `Shader` capability and not `Kernel`;
- the `Logical` addressing model and the `GLSL450` or `Vulkan` memory model;
- an entry point with the requested name whose execution model is vertex, fragment or compute, matching
  [[rhi::ShaderDesc]]'s `stage`;
- every instruction has a non-zero length, stays inside the module, and the stream ends exactly at the last word.

One module holds one stage.

## Writing shaders for the engine

Start every file with `#version 450` and a header comment that lists the interface: locations, bindings and push
constants. The rules the next days rely on:

- **Vertex inputs and varyings** use explicit `layout(location = N)`. The location numbers must match the
  pipeline's vertex layout and the stage before or after.
- **Uniform buffers** use `layout(std140, set = S, binding = B) uniform Name { ... };`. The CPU struct must match
  std140: `vec3` takes 16 bytes of alignment, arrays of scalars take 16 bytes per element, and `mat4` is four `vec4`
  columns. Prefer `vec4` members and explicit padding so the C++ struct and the block agree.
- **Storage buffers** use `layout(std430, set = S, binding = B) buffer Name { ... };` and are marked `readonly`
  or `writeonly` where that is true.
- **Descriptor sets** are bind groups: `set` is the group index, `binding` is the entry's `binding` in the bind
  group layout, and the stages named in the layout must include the stage that uses it.
- **Push constants** use `layout(push_constant) uniform Name { ... } pc;`, at most the device's push constant
  limit, and the range in the pipeline layout must cover the stages that read it.
- **Compute** declares `layout(local_size_x = 64) in;` (and `y`, `z` as needed) and indexes with
  `gl_GlobalInvocationID`.
- **Math** (`normalize`, `dot`, `pow`, `mix`, ...) is plain GLSL; `glslc` turns it into `GLSL.std.450` extended
  instructions, so nothing special is written by hand.
- **Colors**: do lighting math in linear space; an sRGB texture or swapchain format converts on sample and on write.

## The lit pipeline

The demo's shaders show how the rules combine. `lit.vert` and `lit.frag` share one uniform block (`Params`, bound at
set 0, binding 0 for both stages: the layout entry names `Vertex | Fragment`) holding the model-view-projection
matrix, the normal matrix, the direction to the light, the light color (intensity folded in) and the
ambient color - every member a `mat4` or `vec4`, so the C++ struct matches std140 without padding. The texture and
sampler are bindings 1 and 2 of the same group. The math, in linear light, is
`color = albedo * (ambient + light_color * max(dot(N, L), 0))`; its derivation and the interface are in the header
comment of each file, and the tests check the result against that formula pixel by pixel. The normal matrix is the
cofactor matrix of the model's upper 3x3, so normals stay perpendicular to surfaces under non-uniform scale.

## Common mistakes

- Editing a `.spv.inc` by hand: it is overwritten the next time the `shaders` target runs.
- Changing a `.vert` or `.frag` and forgetting to regenerate: `shaders_check` fails when `glslc` is installed.
- Declaring the wrong stage in [[rhi::ShaderDesc]]: the module is refused with "implements a different stage".
- A `vec3` in a uniform block with a tightly packed C++ struct: std140 pads it to 16 bytes.
