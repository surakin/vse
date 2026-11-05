# The Renderer

> **Status:** skeleton — not yet written  
> **Audience:** engine and game programmers  
> **Covers:** Phase 4

## Source of truth

- `engine/renderer/*.h`
- `docs/superpowers/specs/2026-11-04-renderer-design.md` (decisions R1-R11)
- `plans/phase-04-renderer-core.md`

## API reference

Full reference: [The renderer](../reference/renderer.md).

- [[renderer::RenderLogV]] — logging on the render channel
- [[renderer::RendererName]] — identification of the library
- [[renderer::ShaderReflection]] — what a compiled shader says about itself (stage, bindings, block layouts, vertex inputs)
- [[renderer::BuildBindGroupLayout]] — a bind group layout from reflection
- [[renderer::BuildVertexLayout]] — a vertex layout from reflection
- [[renderer::ShaderPermutations]] — the key-to-module table of a permuted shader
- [[renderer::FindPermutation]] — choose a shader variant by key
- [[renderer::Camera]] — position, orientation and lens; view and projection matrices
- [[renderer::Frustum]] — the six planes of a view-projection, with sphere and box tests
- [[renderer::MeshData]] — CPU geometry with bounds; [[renderer::MakeCubeMesh]] and friends build shapes
- [[renderer::GpuMesh]] — a mesh's vertex and index buffers
- [[renderer::RenderScene]] — objects and lights with generation-checked ids, and [[renderer::RenderScene::Cull]]
- [[renderer::FrameResources]] — the per-frame and per-object buffers shaders read
- [[renderer::MaterialSystem]] — templates, materials, bind groups and the pipeline cache
- [[renderer::MaterialTemplateDesc]] — the shaders and fixed-function state of a template
- [[renderer::MakeDrawSortKey]] — the draw order: pipeline, then material, then mesh

## Outline

### Overview

_TODO_

Shader reflection (Phase 4 Day 71) is described in [Shaders](rhi-shaders.md#reflection-shaders-describe-themselves), and shader variants (Day 72) in [Shaders](rhi-shaders.md#permutations-one-source-several-modules).

### Meshes, cameras and the scene

A [[renderer::RenderScene]] is a fixed-capacity list of objects (a `GpuMesh`, a material index, a world matrix) and lights.
`AddObject` returns an id that carries a generation, so an id outliving its object is detected rather than reused.
`Cull` fills a caller-provided array with the slots of the objects that are not hidden and whose world-space bounds touch
the camera's [[renderer::Frustum]]; the array must hold `SlotCount()` entries. `FrameResources::WriteObjects` then packs
the world and normal matrices of exactly those slots into one buffer, and a draw picks its entry with `first_instance`.

The conventions are right-handed, Y up, camera looking down -Z, depth 0 at the near plane and 1 at the far plane.

```cpp compile standalone run
#include <cstdio>

#include "renderer/camera.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    rhi::NullDevice* device = rhi::CreateNullDevice();

    {
        MeshData data;
        if (MakeCubeMesh(1.0f, &data) != nullptr) return 1;
        GpuMesh mesh;
        if (!mesh.Create(device, data, "cube")) return 1;

        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc front;
        front.mesh = &mesh;
        front.transform = ModelMatrix(Vec3(0, 0, -10), 1.0f, 0.0f);
        RenderObjectDesc behind = front;
        behind.transform = ModelMatrix(Vec3(0, 0, 10), 1.0f, 0.0f);
        scene.AddObject(front);
        scene.AddObject(behind);

        Camera camera = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
        u32 visible[2];
        u32 count = scene.Cull(Frustum::FromViewProjection(camera.ViewProjection()), visible, 2);
        std::printf("%u of %u objects visible\n", count, scene.ObjectCount());
    } // the mesh and scene go before the device

    rhi::DestroyDevice(device);
    return 0;
}
```

```output
1 of 2 objects visible
```

### Materials

A material is a shader permutation plus a parameter block and textures. A `MaterialSystem` holds templates (a vertex
module, a fragment module or permuted fragment shader, and render state) and materials made from them. Descriptor sets
follow one convention, which creation checks against the shaders' reflection:

| Set | Contents | Bound |
|---|---|---|
| 0 | `FrameData` uniform, binding 0 | once per pass |
| 1 | the material: one uniform block, sampled textures, samplers | when the material changes |
| 2 | the per-object storage buffer, binding 0 | once per pass |

Parameter names are the members of the set-1 uniform block; texture and sampler names are the shader's variable names. A
setter whose type or size does not match the reflected member returns false and logs, so a typo fails at the call and
not as a wrong color. Writes go to a CPU copy and reach the GPU once per frame in `FlushMaterials`; the buffer has three
frame regions, so a change is copied three times and then costs nothing. Changing a texture rebuilds only that
material's bind groups, at the next flush. A texture slot left unset shows a default: the one the template lists in
`texture_defaults`, otherwise one chosen by name (`normal...` flat normal, `emissive...` and `black...` black, anything
else white), so name slots to match or list them. A shader with nothing in set 1 is also a valid material.

Pipelines are cached by (template, permutation, target formats, vertex layout): two materials of one template and key
share one. `GetPipeline` creates on a miss, so call it (or `Warmup`) before recording starts; the counters in `Stats()`
show hits and misses. `MakeDrawSortKey` packs pipeline, material and mesh into one integer: sorting by it keeps pipeline
changes rarest and material changes next.

```cpp compile standalone run
#include <cstdio>

#include "material_test.frag.perm.inc"
#include "material_test.vert.refl.inc"
#include "material_test.vert.spv.inc"
#include "renderer/material.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        MaterialSystem system(device, MaterialSystemDesc{});

        MaterialTemplateDesc desc;
        desc.name = "material_test";
        desc.vertex.words = shaders::kMaterialTestVertSpv;
        desc.vertex.word_count = shaders::kMaterialTestVertSpvWordCount;
        desc.vertex.reflection = shaders::kMaterialTestVertRefl;
        desc.vertex.reflection_size = shaders::kMaterialTestVertReflByteCount;
        desc.fragment_permutations = &shaders::kMaterialTestFragPermutations;
        const TemplateId tmpl = system.CreateTemplate(desc);

        const MaterialId red = system.CreateMaterial(tmpl, PermutationKey{}, "red");
        const MaterialId blue = system.CreateMaterial(tmpl, PermutationKey{}, "blue");
        system.SetVec4(red, "base_color", Vec4(1, 0, 0, 1));
        system.SetVec4(blue, "base_color", Vec4(0, 0, 1, 1));
        const bool typo = system.SetVec4(blue, "base_colour", Vec4(0, 0, 1, 1));

        PipelineTarget target;
        target.color_formats[0] = rhi::Format::RGBA8_UNorm;
        const PipelineLookup a = system.GetPipeline(red, target, StandardVertexLayout());
        const PipelineLookup b = system.GetPipeline(blue, target, StandardVertexLayout());
        std::printf("typo accepted: %s\n", typo ? "yes" : "no");
        std::printf("one pipeline shared: %s (hits %u, misses %u)\n", a.pipeline == b.pipeline ? "yes" : "no", system.Stats().pipeline_hits,
                    system.Stats().pipeline_misses);
    }
    rhi::DestroyDevice(device);
    return 0;
}
```

```output
[LogRender] ERROR: material 'blue': the parameter block has no member 'base_colour'
typo accepted: no
one pipeline shared: yes (hits 1, misses 1)
```

### Rules & contracts

- A `GpuMesh` must be destroyed (or go out of scope) before the device that created it; a scene holds meshes by pointer and
  does not own them, so a mesh must also outlive every object that uses it.

_TODO_

### Common mistakes

_TODO_

### Platform notes

_TODO_

### See also

- [The RHI](rhi.md) and the [renderer design](../superpowers/specs/2026-11-04-renderer-design.md).
