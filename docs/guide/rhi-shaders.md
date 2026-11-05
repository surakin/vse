# Shaders: from Slang to a shader module

> **Audience:** engine and renderer programmers writing shaders  
> **Covers:** Phase 3, Day 58; Phase 4, Days 69-71 (Slang, reflection)

## Source of truth

- `shaders/` (Slang sources, the generated `*.spv.inc` and `*.refl.inc` files, the build and check scripts) and `shaders/README.md` (flags and pitfalls)
- `tools/shader_mint/` (turns `slangc` output into the two include files) and `third_party/nlohmann_json/` (the JSON reader it uses)
- `engine/renderer/shader_reflection.h` (the reflection data, its blob format and the layout builders)
- `engine/rhi/spirv.h` (the sanity check every `CreateShader` runs)
- `PLAN.md` assumption #18 (the shader toolchain decision)

## API reference

- [[rhi::ShaderDesc]] — a shader module to create from bytecode
- [[rhi::ValidateSpirv]] — the module sanity check
- [[rhi::Device]] — `CreateShader` and `DestroyShader`
- [[rhi::vulkan::VulkanDevice]] — the Vulkan device and its shader lookup
- [[renderer::ShaderReflection]] — what a compiled shader says about itself
- [[renderer::ParseShaderReflection]] — decode a reflection blob
- [[renderer::BuildBindGroupLayout]] — a bind group layout from one or more shaders
- [[renderer::BuildVertexLayout]] — a vertex layout from a vertex shader

## How shaders get into the engine

The engine never compiles shaders at runtime and contains no shader compiler. A shader is written in Slang,
compiled offline by `slangc` to SPIR-V, and embedded in the program as an array of 32-bit words:

```
shaders/triangle.vert.slang        Slang source (the interface is documented in its header comment)
shaders/triangle.vert.spv.inc      generated: kTriangleVertSpv and kTriangleVertSpvWordCount, committed
shaders/triangle.vert.refl.inc     generated: kTriangleVertRefl and kTriangleVertReflByteCount, committed
```

The generated files are committed, so a checkout without a shader compiler builds and runs as it is. When you
change a `.slang` file, run:

```
cmake --build build --target shaders
```

That runs `slangc` (with the arguments from `slang_compile_args()` in `shaders/names.cmake`, plus
`-reflection-json`), then `spirv-val` if it is installed, then `shader_mint`, and rewrites both `.inc` files. Commit
the source and both `.inc` files together. The
`shaders_check` ctest recompiles every shader and compares it with the committed file; it notes and skips a shader
when the installed `slangc` is not the version that generated the file (the compiler and version are recorded in
each file's `// compiler:` line). Changing a shader needs `slangc`: without it the `shaders` target and the
`shaders_check` test do not exist, the build keeps using the committed `.spv.inc` and an edit to a `.slang` file is
silently ignored (CMake prints a status line saying so).

To add a shader, put `<name>.<vert|frag|comp>.slang` in `shaders/` and add its name to `ENGINE_SHADERS` in
`shaders/CMakeLists.txt`. The stage comes from the file name; the entry point is called `main`. The array is named
from the file: `triangle.vert` becomes `kTriangleVertSpv`, in namespace `engine_placeholder::shaders`.

Pitfalls that cost time and how the sources avoid them (each is explained in `shaders/README.md`): write
`[[vk::binding(binding, set)]]` explicitly; use `SV_VulkanVertexID`, not `SV_VertexID` (which needs the
`shaderDrawParameters` device feature); give storage images an `[[vk::image_format]]` and declare write-only ones as
`WTexture2D`; keep `-fvk-invert-y` off because `PerspectiveVulkan` already flips Y; keep
`-matrix-layout-column-major` so a `Mat4` copied into a uniform block is not transposed.

The RHI takes precompiled bytecode in [[rhi::ShaderDesc]]. Slang also produces HLSL and Metal from the same
sources, which is how the DX12 and Metal backends will get their shaders; two things to handle then, both
recorded in `shaders/README.md`: the clip-space Y flip (the RHI follows Vulkan's convention) and Metal's renaming of
an entry point called `main`. See the portability review, sections 2 and 3.

## Reflection: shaders describe themselves

A shader's bindings, block layouts, vertex inputs and compute local size are written once, in the `.slang` file.
`shader_mint` reads them from `slangc`'s reflection output and commits them as `shaders/<name>.refl.inc`: a compact
blob (`k<Name>Refl`, parsed by [[renderer::ParseShaderReflection]]) plus constants for every uniform and push-constant
block (`k<Name><Block>Size`, `k<Name><Block><Member>Offset`). The tool also cross-checks the reflection against the
SPIR-V it just compiled, so a reflection that disagrees with the module fails the build.

Use it instead of writing a layout by hand:

- [[renderer::BuildBindGroupLayout]] turns one shader, or a vertex and fragment pair, into the layout of a descriptor
  set; stage masks of a shared binding are OR-ed, and two shaders that disagree about a binding are reported with both
  variable names.
- [[renderer::BuildVertexLayout]] packs a vertex shader's inputs into one interleaved binding.
- The generated constants feed `static_assert`s on the C++ structs copied into blocks: see `LitUniforms` in
  `game/common/cube_scene.cpp`. [[renderer::CheckBlockSize]] is the run-time form.

```cpp compile standalone run
#include <cstdio>

#include "renderer/shader_reflection.h"
#include "rhi/null/null_device.h"
#include "lit.frag.refl.inc"
#include "lit.vert.refl.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    ShaderReflection vs;
    ShaderReflection fs;
    if (ParseShaderReflection(shaders::kLitVertRefl, shaders::kLitVertReflByteCount, &vs) != nullptr ||
        ParseShaderReflection(shaders::kLitFragRefl, shaders::kLitFragReflByteCount, &fs) != nullptr) {
        return 1;
    }

    const ShaderReflection* pair[2] = {&vs, &fs};
    BindGroupLayoutData layout;
    ReflectionError error;
    if (!BuildBindGroupLayout(pair, 2, 0, &layout, &error)) {
        std::printf("%s\n", error.message);
        return 1;
    }

    rhi::NullDevice* device = rhi::CreateNullDevice();
    rhi::BindGroupLayoutHandle handle = device->CreateBindGroupLayout(layout.Desc("lit"));
    std::printf("bindings: %u, valid: %d, block: %u bytes\n", layout.entry_count, handle.IsValid() ? 1 : 0,
                vs.blocks[0].size);
    device->DestroyBindGroupLayout(handle);
    rhi::DestroyDevice(device);
}
```

```output
bindings: 3, valid: 1, block: 176 bytes
```

The blob is untrusted input to the parser: wrong magic or version, truncation, unknown types, names without room for a
terminator, counts above the fixed capacities and trailing bytes are all rejected with a message, and nothing is read
outside the given bytes. The JSON reader in `shader_mint` is a vendored third-party library (nlohmann/json, `PLAN.md`
assumption #19) used by that offline tool only; the engine and the renderer contain no JSON code.

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

Start every file with a header comment that lists the interface: locations, bindings and push constants. The rules
the next days rely on:

- **Vertex inputs and varyings** use explicit `[[vk::location(N)]]`. The location numbers must match the
  pipeline's vertex layout and the stage before or after.
- **Uniform buffers** are `[[vk::binding(B, S)]] ConstantBuffer<Name> name;` and come out as std140. The CPU struct
  must match std140: `float3` takes 16 bytes of alignment, arrays of scalars take 16 bytes per element, and
  `float4x4` is four `float4` columns. Prefer `float4` members and explicit padding so the C++ struct and the block
  agree.
- **Storage buffers** are `RWStructuredBuffer<T>` (written) or `StructuredBuffer<T>` (read only) and come out as
  std430; `GetDimensions` gives the element count.
- **Descriptor sets** are bind groups: the second argument of `[[vk::binding(binding, set)]]` is the group index, the
  first is the entry's `binding` in the bind group layout, and the stages named in the layout must include the
  stage that uses it.
- **Push constants** are `[[vk::push_constant]] ConstantBuffer<Name> pc;`, at most the device's push constant
  limit, and the range in the pipeline layout must cover the stages that read it.
- **Compute** declares `[numthreads(64, 1, 1)]` and indexes with `SV_DispatchThreadID`.
- **Math** (`normalize`, `dot`, `pow`, `lerp`, ...) is the HLSL-style library of Slang; `slangc` turns it into
  `GLSL.std.450` extended instructions, so nothing special is written by hand.
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
- Changing a `.slang` file and forgetting to regenerate: `shaders_check` fails when `slangc` is installed.
- Declaring the wrong stage in [[rhi::ShaderDesc]]: the module is refused with "implements a different stage".
- A `vec3` in a uniform block with a tightly packed C++ struct: std140 pads it to 16 bytes.
