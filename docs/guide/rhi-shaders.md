# Shaders: from Slang to a shader module

> **Audience:** engine and renderer programmers writing shaders  
> **Covers:** Phase 4, Day 58; Phase 5, Days 69-84 (Slang, reflection, permutations, material and deferred shaders, portability)

## Source of truth

- `shaders/` (Slang sources, the generated `*.spv.inc` and `*.refl.inc` files, the build and check scripts) and `shaders/README.md` (flags and pitfalls)
- `tools/shader_mint/` (turns `slangc` output into the two include files) and `third_party/nlohmann_json/` (the JSON reader it uses)
- `engine/renderer/shader_reflection.h` (the reflection data, its blob format and the layout builders)
- `engine/renderer/shader_permutation.h` and `tools/shader_mint/permutations.*` (permutation tables and how they are minted)
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
- [[renderer::ShaderPermutations]] — the committed key-to-module table of a permuted shader
- [[renderer::PermutationKey]] — a set of options, as bits
- [[renderer::MakePermutationKey]] — build a key from option names
- [[renderer::FindPermutation]] — the module for a key, or an error naming the missing key

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
sources, which is how the DX12 and Metal backends will get their shaders, and `ctest -R shaders_portability` proves it
today: every shader and every permutation key compiles to both. Four things to handle then, all recorded in
`shaders/README.md`: the clip-space Y flip (the RHI follows Vulkan's convention, and the flip lives in the projection),
Metal's renaming of an entry point called `main`, Metal's flattening of descriptor sets to buffer slots, and the missing
base instance in D3D12 shaders. See the RHI portability review, sections 2 and 3, and the
[renderer portability review](../superpowers/specs/2026-11-04-renderer-portability-review.md).

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

    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    rhi::BindGroupLayoutHandle handle = device->CreateBindGroupLayout(layout.Desc("lit"));
    std::printf("bindings: %u, valid: %d, block: %u bytes\n", layout.entry_count, handle.IsValid() ? 1 : 0,
                vs.blocks[0].size);
    device->DestroyBindGroupLayout(handle);
    device.Reset();
}
```

```output
bindings: 3, valid: 1, block: 176 bytes
```

The blob is untrusted input to the parser: wrong magic or version, truncation, unknown types, names without room for a
terminator, counts above the fixed capacities and trailing bytes are all rejected with a message, and nothing is read
outside the given bytes. The JSON reader in `shader_mint` is a vendored third-party library (nlohmann/json, `PLAN.md`
assumption #19) used by that offline tool only; the engine and the renderer contain no JSON code.

## Permutations: one source, several modules

A material system needs variants of one shader (with or without a normal map, alpha test, ...). The engine builds them
all offline and chooses at run time by key; nothing is compiled while the game runs and nothing falls back silently.
The mechanism is **compile-time defines**: each variant is the same Slang source compiled with `-DOPTION=1` for the
options that are on. (Specialization constants were evaluated and not chosen; the evidence is in the renderer spec,
section R10.)

A permuted shader has three parts:

1. **Declare the options** in the first 60 lines of the `.slang` file, upper-case, at most
   [[renderer::kMaxPermutationOptions]] (16), and use each one with `#ifdef` / `#if defined(...)`:

   ```
   // permutation-options: INVERT HALF TINT DEBUG
   #ifdef INVERT
       color.rgb = 1.0 - color.rgb;
   #endif
   ```

2. **List the keys to build** in `<name>.perm` next to the source: `key -` is the base module (always required), and
   `key INVERT HALF` turns those options on. At most [[renderer::kMaxPermutationKeys]] (32) keys. Only listed
   combinations exist; adding an option does not build every combination.
3. **Add the shader to `ENGINE_SHADERS`** like any other. The presence of the `.perm` file is what makes it permuted.

`cmake --build build --target shaders` then runs `slangc` once per listed key (the cost of the manifest is one compiler
run per line: the 7-key test shader takes about a second), mints each result like a plain shader, drops modules whose SPIR-V and
reflection are identical to an earlier key's, and commits four files: `<name>.spv.inc` and `<name>.refl.inc` (the base
module, as for any shader) and `<name>.perm.inc` (the other modules and the table). The output is deterministic, and
`shaders_check` regenerates and compares the table too.

At run time, build a key from names and look it up:

```cpp compile standalone run
#include <cstdio>

#include "permute_test.frag.perm.inc"
#include "renderer/shader_permutation.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    const ShaderPermutations& table = shaders::kPermuteTestFragPermutations;

    const char* names[] = {"INVERT", "HALF"};
    PermutationKey key;
    ReflectionError error;
    if (!MakePermutationKey(table, names, 2, &key, &error)) {
        std::printf("%s\n", error.message);
        return 1;
    }
    const PermutationModule* module = FindPermutation(table, key, &error);
    std::printf("INVERT HALF: %s, %zu words\n", module != nullptr ? "found" : "missing", module != nullptr ? module->word_count : 0);

    // TINT with INVERT was not listed in the manifest, so it was not built: an error, never a fallback.
    const char* unbuilt[] = {"INVERT", "TINT"};
    MakePermutationKey(table, unbuilt, 2, &key, &error);
    if (FindPermutation(table, key, &error) == nullptr) {
        std::printf("%s\n", error.message);
    }
}
```

```output
INVERT HALF: found, 93 words
shader 'permute_test.frag' has no permutation {INVERT, TINT} (add the key to its .perm manifest)
```

The module's bytecode (`module->Blob(device->Caps().shader_format)`) goes into [[rhi::ShaderDesc]] and its `reflection` blob into [[renderer::ParseShaderReflection]],
exactly as for a plain shader; each module has its own reflection, because a variant can use different resources.
Keys that build identical SPIR-V share one module (`key DEBUG` and `key DEBUG TINT` in the test shader), so the
binary pays for distinct code only.

Two things to know when writing the shader:

- **Each module has the reflection of its own variant.** Slang reports a resource a variant never uses as unused, and
  the layout builders leave it out, so a texture declared once and used only under `#ifdef TEX` appears in the TEX
  module's layout and not in the base module's. (Checked with a plain `#ifdef` and with a branch on a constant derived
  from the option; both mint.) Build one layout per module, not one per shader.
- **Keep the key space small.** Options multiply: eight independent options would be 256 combinations, which the
  manifest limit refuses on purpose. List the combinations the material system actually uses.

## Creating a shader module

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"
#include "triangle.frag.spv.inc"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    NullDevicePtr device = CreateNullDevice();

    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.SetCode(SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount));

    ShaderDesc fs;
    fs.stage = ShaderStage::Fragment;
    fs.SetCode(SpirvBlob(shaders::kTriangleFragSpv, shaders::kTriangleFragSpvWordCount));

    ShaderHandle vertex = device->CreateShader(vs);
    ShaderHandle fragment = device->CreateShader(fs);
    std::printf("valid: %d %d\n", vertex.IsValid() ? 1 : 0, fragment.IsValid() ? 1 : 0);

    device->DestroyShader(vertex);
    device->DestroyShader(fragment);
    device.Reset();
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

## Bytecode formats

A [[rhi::ShaderDesc]] carries a format-tagged blob: `format` ([[rhi::ShaderFormat]]: `Spirv`, `Dxil`, and `Msl` reserved for
the Metal backend), `bytes` and `size`. Each device takes one format, [[rhi::DeviceCaps::shader_format]]: SPIR-V on Vulkan,
DXIL on Direct3D 12. A module in another format is refused with an error that names both, before any driver sees it.
`Validate` checks the shape the format promises without a device: SPIR-V is 4-byte aligned, a whole number of words and at
least a header; DXIL starts with the `DXBC` container tag and its size matches the one its header records; MSL is refused
until a backend takes it.

Code that creates shaders names each shader once and picks the device's blob. [[rhi::ShaderBlobSet]] holds a shader in every
format it was minted in, and [[rhi::CreateShaderFromSet]] creates the module from the blob of the device's format, failing
with the shader's name when that format was not minted. The renderer's [[renderer::PermutationModule]] does the same for
permuted and material shaders (`Blob(format)`), with one reflection blob for every format, since all formats come from the
same Slang source.

## DXIL

Every shader and every permutation key is also minted to DXIL for Direct3D 12 (Phase 13 Day 163): `slangc -target dxil -profile
sm_6_0 -DENGINE_TARGET_D3D12=1`, signed by the Windows SDK's `dxil.dll`, committed as `shaders/<name>.dxil.inc` beside the
`.spv.inc`. That file also defines the shader's blob set, `shaders::k<Base>Shader`, which is what code passes to
[[rhi::CreateShaderFromSet]]:

```cpp
#include "lighting.frag.dxil.inc" // the DXIL, the SPIR-V it includes, and kLightingFragShader

rhi::ShaderHandle fragment = rhi::CreateShaderFromSet(*device, shaders::kLightingFragShader, rhi::ShaderStage::Fragment);
```

One source serves both targets through `shaders/target_common.slang` (the clip-space flip, the vertex index, the object index
stream) and two rules `shader_mint` checks: a vertex input at location N has the semantic `TEXCOORD<N>`, and a resource at
`[[vk::binding(b, g)]]` is declared with `register(<class><b>, space<g>)` (push constants `register(b0, space15)`). The details,
and how the build finds `slangc` and the SDK's `dxcompiler.dll`, are in `shaders/README.md` ("Targets"). DXIL is minted on
Windows only (the Linux `dxc` aborts); `shader_dxil_test` checks on every machine that each DXIL module was minted beside the
committed SPIR-V.

```cpp compile standalone run
#include <cstdio>

#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "triangle.vert.spv.inc"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

int main() {
    const ShaderBlobSet triangle{"triangle.vert", SpirvBlob(shaders::kTriangleVertSpv, shaders::kTriangleVertSpvWordCount), {}};

    NullDevicePtr spirv_device = CreateNullDevice();
    ShaderHandle module = CreateShaderFromSet(*spirv_device, triangle, ShaderStage::Vertex);
    std::printf("SPIR-V device: %s\n", module.IsValid() ? "created" : "refused");
    spirv_device->DestroyShader(module);

    NullDeviceOptions options;
    options.caps.shader_format = ShaderFormat::Dxil; // a device that takes DXIL, as Direct3D 12 does
    NullDevicePtr dxil_device = CreateNullDevice(options);
    LogRhi.SetEnabled(false); // the refusal is logged as an error; this sample expects it
    module = CreateShaderFromSet(*dxil_device, triangle, ShaderStage::Vertex);
    std::printf("DXIL device: %s\n", module.IsValid() ? "created" : "refused (no dxil blob)");
    return 0;
}
```

```output
SPIR-V device: created
DXIL device: refused (no dxil blob)
```

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
  std430; `GetDimensions` gives the element count, but Metal cannot answer it (`fill.comp` and `saxpy.comp` do not compile to
  Metal for that reason), so a shader meant for every backend takes the count as a constant.
- **Descriptor sets** are bind groups: the second argument of `[[vk::binding(binding, set)]]` is the group index, the
  first is the entry's `binding` in the bind group layout, and the stages named in the layout must include the
  stage that uses it.
- **Push constants** are `[[vk::push_constant]] ConstantBuffer<Name> pc;`, at most the device's push constant
  limit, and the range in the pipeline layout must cover the stages that read it.
- **Compute** declares `[numthreads(64, 1, 1)]` and indexes with `SV_DispatchThreadID`.
- **Math** (`normalize`, `dot`, `pow`, `lerp`, ...) is the HLSL-style library of Slang; `slangc` turns it into
  `GLSL.std.450` extended instructions, so nothing special is written by hand.
- **Colors**: do lighting math in linear space; an sRGB texture or swapchain format converts on sample and on write.

## Shaders drawn through the material system

Shaders drawn through [[renderer::MaterialSystem]] (the `material_test`, `gbuffer`, `lighting`, `point_light` and `tonemap` shaders)
follow one more convention: set 0 is the frame block ([[renderer::FrameData]]), set 1 the material (one uniform block, sampled
textures, samplers), set 2 the per-object storage buffer ([[renderer::ObjectData]]). The object index is
`SV_VulkanInstanceID`, because the renderer draws object `i` with `first_instance = i`. The G-buffer shaders are permuted
(`NORMAL_MAP`, `ALPHA_TEST`, `VERTEX_COLOR`); the pass picks the module from the material's options. The full rules, with the
reasons, are in `shaders/README.md` ("Material shaders"), and the renderer guide shows the passes working together.

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
