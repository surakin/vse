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
- [[renderer::RenderGraph]] — passes and virtual resources, compiled into an ordered, culled schedule
- [[renderer::PassBuilder]] — declares what one pass reads and writes

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

### The render graph

A frame is described as passes that declare what they read and write, and compiled into an ordered schedule. Resources
are virtual: `CreateTexture`/`CreateBuffer` give a transient one (a descriptor, allocated later), and `ImportTexture`/
`ImportBuffer` wrap something that outlives the frame, with the state it starts in and the state it must end in
(`Present` for the swapchain image). A pass is a name, a callback (function pointer plus context pointer) and its
accesses: `Read`, `Write` (the old contents are not needed) or `ReadWrite`, each with a usage that fixes the resource
state it needs (`GraphUsage`, mapped by `StateForUsage`).

Meaning follows declaration order: a read sees the latest earlier write. From that, `Compile` derives edges (raw =
read-after-write, waw, war, and `after` for `AddDependency`), orders the passes with the smallest declaration index first
among those ready (so the schedule is stable), and culls passes whose results nothing needs. A pass is a root, and stays,
if it has `PassFlags::SideEffects` or writes an import; a live pass keeps the passes it reads from. Every mistake
(a read of something nobody wrote, a write to a read-only import, a usage that does not fit, a cycle) is stored as a named
message and `Compile` returns false; nothing is checked while executing. All graph storage comes from the arena you pass
in, so a frame's graph allocates nothing from the heap; reset the arena after the frame.

```cpp compile standalone run
#include <cstdio>

#include "mem/arena_allocator.h"
#include "renderer/render_graph.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    ArenaAllocator arena(1u << 20);
    RenderGraph graph(&arena, RenderGraphDesc{});

    rhi::TextureHandle swapchain_image;
    swapchain_image.index = 1;
    swapchain_image.generation = 1;
    const GraphResource backbuffer =
        graph.ImportTexture("backbuffer", swapchain_image, rhi::ResourceState::Undefined, rhi::ResourceState::Present, false);
    rhi::TextureDesc shadow_desc;
    shadow_desc.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
    rhi::TextureDesc color_desc;
    color_desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    const GraphResource shadow = graph.CreateTexture("shadow_map", shadow_desc);
    const GraphResource hdr = graph.CreateTexture("hdr", color_desc);
    const GraphResource unused = graph.CreateTexture("debug_view", color_desc);

    graph.AddPass("shadow", nullptr, nullptr).Write(shadow, GraphUsage::DepthAttachment);
    graph.AddPass("lighting", nullptr, nullptr).Read(shadow, GraphUsage::Sampled).Write(hdr, GraphUsage::ColorAttachment);
    graph.AddPass("debug", nullptr, nullptr).Read(hdr, GraphUsage::Sampled).Write(unused, GraphUsage::ColorAttachment);
    graph.AddPass("tonemap", nullptr, nullptr).Read(hdr, GraphUsage::Sampled).Write(backbuffer, GraphUsage::ColorAttachment);

    if (!graph.Compile()) {
        for (u32 i = 0; i < graph.ErrorCount(); ++i) std::printf("%s\n", graph.Error(i));
        return 1;
    }
    char text[2048];
    graph.Dump(text, sizeof(text));
    std::printf("%s", text);
    return 0;
}
```

```output
graph passes=4 resources=4 scheduled=3 culled=1
schedule: shadow, lighting, tonemap
culled: debug
pass 0 "shadow" at 0
  write "shadow_map" depth
pass 1 "lighting" at 1
  read "shadow_map" sampled
  write "hdr" color
pass 2 "debug" culled
  read "hdr" sampled
  write "debug_view" color
pass 3 "tonemap" at 2
  read "hdr" sampled
  write "backbuffer" color
resource 0 "backbuffer" texture import use=2..2
resource 1 "shadow_map" texture transient use=0..1
resource 2 "hdr" texture transient use=1..2
resource 3 "debug_view" texture transient unused
edge "shadow" -> "lighting" raw "shadow_map"
edge "lighting" -> "debug" raw "hdr"
edge "lighting" -> "tonemap" raw "hdr"
```

The first line of `Dump` gives the counts, `schedule:` and `culled:` list the passes, each `pass` block lists its
accesses, each `resource` line shows its first and last use as schedule positions (`unused` if every user was culled), and
each `edge` line names its kind and resource. `WriteDot` writes the same graph for Graphviz, with culled passes dashed.

#### Barriers, imports and transient resources

The graph owns every state transition, so a pass callback records no `Barrier` for a resource it declared. `Compile`
walks the scheduled passes and, per resource, tracks its state: an import starts in the state you gave it, a transient
resource in `Undefined`. A use needs the state of its usage; a barrier `before -> after` is recorded when that differs
from the current state, or when the previous use wrote the resource (a same-state barrier then orders the write against
what follows, as a compute pass writing a storage image twice needs). Consecutive reads in one state, such as `Sampled`
followed by `StorageRead`, share it and need nothing. All barriers a pass needs precede it, in the order of its accesses,
so its color and depth attachments change state together and always before its rendering pass begins. After the last pass
an import not in its final state gets an exit barrier, which is how the swapchain image ends in `Present`. `BarrierCount`,
`BarrierAt` and `DumpBarriers` expose the plan, so a test can compare it as text.

A transient texture must be declared with the usage flags every use needs (a sampled use needs `Sampled`, a storage write
`Storage`, and so on); `Compile` reports a missing flag by pass and resource name.

```cpp compile standalone run
#include <cstdio>

#include "mem/arena_allocator.h"
#include "renderer/render_graph.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    ArenaAllocator arena(1u << 20);
    RenderGraph graph(&arena, RenderGraphDesc{});
    rhi::TextureHandle swapchain_image;
    swapchain_image.index = 1;
    swapchain_image.generation = 1;
    const GraphResource backbuffer =
        graph.ImportTexture("backbuffer", swapchain_image, rhi::ResourceState::Undefined, rhi::ResourceState::Present, false);
    rhi::TextureDesc scene_desc;
    scene_desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    const GraphResource scene = graph.CreateTexture("scene", scene_desc);

    graph.AddPass("draw", nullptr, nullptr).Write(scene, GraphUsage::ColorAttachment);
    graph.AddPass("present", nullptr, nullptr).Read(scene, GraphUsage::Sampled).Write(backbuffer, GraphUsage::ColorAttachment);

    if (!graph.Compile()) return 1;
    char text[512];
    graph.DumpBarriers(text, sizeof(text));
    std::printf("%s", text);
    return 0;
}
```

```output
barrier at 0 "scene" undefined -> color
barrier at 1 "scene" color -> shader-read
barrier at 1 "backbuffer" undefined -> color
barrier at exit "backbuffer" color -> present
```

`RenderGraph::Execute(pool, list)` runs a compiled graph: for each scheduled pass it takes the transient resources whose
first use is that pass from a `TransientPool`, records the pass's barriers, calls its callback (which finds its textures
with `context.graph->ResolvedTexture(resource)`) and gives back each transient resource after its last use. The pool
matches by description, so a steady-state frame creates nothing, and two transient resources whose lifetimes do not
overlap share one texture even inside a frame (pooling by description, not memory aliasing). The pool remembers the
state a texture was left in and the graph starts the next user's first barrier from it, which also orders the new
writes after the old user's. A pooled resource unused for more than `max_idle_frames` frames is destroyed at
`EndFrame`; `Stats` reports created, reused, destroyed, live and idle counts.

The null device is the oracle for all of this: it tracks every resource's state and counts a barrier whose `before` does
not match as an error, so a graph that executes with `ErrorCount() == 0` has a consistent state machine. The real-device
test (`tests/rhi/vulkan_render_graph_test.cpp`) runs a clear, a sampling draw, a compute write and a combining draw with
validation and synchronization validation on and expects no messages.

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
