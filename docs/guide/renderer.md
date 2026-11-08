# The Renderer

> **Status:** draft  
> **Audience:** engine and game programmers  
> **Covers:** Phase 5 and Phase 8 (resetting the device at run time)

## Making my type survive a reset (checklist)

If your type owns device objects (buffers, textures, samplers, shaders, pipelines, bind groups), it must be able to make them again
when the device is replaced. Do these four things; "Surviving a reset" below has the details and a sample.

1. **Keep the CPU-side source** (pixels, mesh data, descriptors, or a loader that makes them), never only the GPU object.
2. **Register** with a `renderer::DeviceResources` at your tier (`renderer::tiers`), through a `renderer::ScopedRegistration` member.
3. **Implement `renderer::Recreatable`:** `Release` destroys your device objects and keeps the source; `Recreate` makes them again;
   `Live` says whether you hold them now.
4. **Never keep a handle past a frame** for anyone else; hold your owner (a `GpuMesh*`, a `GpuTexture*`) and read `Handle()` when you record.
   Bind textures to materials **by owner**, not by raw handle.

Then test it: reset the device with `tests/renderer/reset_rig.h` and compare `Device::Resources()` before and after
("Testing a reset" in the building-and-testing guide).

## Source of truth

- `engine/renderer/*.h`
- `docs/superpowers/specs/2026-11-04-renderer-design.md` (decisions R1-R11)
- `plans/phase-05-renderer-core.md`

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
- [[renderer::LightingPass]] — directional, ambient and point deferred lighting into an HDR target; [[renderer::TonemapPass]] brings it to the display
- [[renderer::GBufferPass]] — culls, sorts and draws the scene into the G-buffer; [[renderer::GBufferDebugView]] shows a layer

## Outline

### Overview

The renderer turns a [[renderer::RenderScene]] (objects with a mesh, a material and a transform, plus lights) and a
[[renderer::Camera]] into a picture on the RHI. Each frame, [[renderer::Renderer]] begins the device frame, calls your frame
callback to declare passes on a [[renderer::RenderGraph]], compiles the graph (which orders the passes, culls the unused ones and
inserts the barriers), records every pass, submits once and presents. The shipped pipeline is **deferred**: a geometry pass fills a
G-buffer (albedo, normal, material parameters, depth), a lighting pass shades it from the lights (a full-screen pass for ambient and
directional light, one scissored sphere per point light), and a tonemap pass maps the HDR result to the target. Materials come from
Slang shaders described by reflection, so a material is a template plus parameters and textures, and its pipelines are built on
demand and cached. Nothing here names Vulkan; a D3D12 or Metal backend plugs in under the RHI.

Shader reflection (Phase 5 Day 71) is described in [Shaders](rhi-shaders.md#reflection-shaders-describe-themselves), and shader variants (Day 72) in [Shaders](rhi-shaders.md#permutations-one-source-several-modules).

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
    rhi::NullDevicePtr device = rhi::CreateNullDevice();

    {
        MeshData data;
        if (MakeCubeMesh(1.0f, &data) != nullptr) return 1;
        GpuMesh mesh;
        if (!mesh.Create(device.Get(), data, "cube")) return 1;

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

    device.Reset();
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
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        MaterialSystem system(device.Get(), MaterialSystemDesc{});

        MaterialTemplateDesc desc;
        desc.name = "material_test";
        desc.vertex.words = shaders::kMaterialTestVertSpv; // the SPIR-V of the module; `dxil` holds its DXIL when minted
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
    device.Reset();
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
test (`tests/rhi/render_graph_gpu_test.cpp`) runs a clear, a sampling draw, a compute write and a combining draw with
validation and synchronization validation on and expects no messages.

### Frames, parallel recording and the swapchain

`Renderer` is the frame loop. It owns what lives across frames (the transient pool and the per-frame arena) and borrows the
device, an optional swapchain and an optional `jobs::Scheduler`. You give it a build callback; each `RenderFrame` then
opens a device frame, acquires the swapchain image (or imports the `FrameTarget` you pass when there is no swapchain), calls
the callback with a fresh graph that already holds the imported target, compiles, records, submits once and presents.

The status says how the frame ended: `Ok`; `Skipped` when the window is minimized (nothing was submitted, try again);
`DeviceLost` for the frame in which the device or surface was found gone (the renderer then recovers, see "Device loss and recovery"; without a `DeviceHost` it stays lost and destroyable); `Failed` for
anything else, such as a callback that gave up, a bad graph or a transient the device could not create (`LastError` names
it). A failed frame never leaves the swapchain stuck: an acquired image is moved to `Present` and shown as it is, and the
transients the frame had taken go back to the pool in the state they were taken in. A resize needs no handling by
the caller: the graph is rebuilt every frame from the new `width`/`height` in the `FrameContext`, so targets sized from
it get new pool entries and the old ones are destroyed once they age out.

Recording is parallel. `RenderGraph::ExecuteParallel` resolves the transients first (serially), then records every
scheduled pass into its own command list. With a scheduler the passes record concurrently, each list created on the thread
that records it; without one they record in order on the calling thread. All lists go to a single `Submit` in schedule
order, so the GPU sees exactly what serial recording would have produced. Pass callbacks run concurrently with each other,
so they must not share mutable state without synchronization; the graph only reads its own tables meanwhile. A
pass records into its one list: splitting one pass's draw list over several lists needs the RHI to suspend and resume a
rendering pass, which it cannot do yet (see `KNOWN-ISSUES.md`).

```cpp compile standalone run
#include <cstdio>

#include "renderer/renderer.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

void Clear(const PassExecuteContext& pass, void* user) {
    rhi::RenderingDesc rd;
    rd.color[0].texture = pass.graph->ResolvedTexture(*static_cast<GraphResource*>(user));
    rd.color_count = 1;
    pass.list->BeginRendering(rd);
    pass.list->EndRendering();
}

GraphResource g_target;

bool Build(const FrameContext& context, void*) {
    g_target = context.target;
    context.graph->AddPass("clear", &Clear, &g_target).Write(context.target, GraphUsage::ColorAttachment);
    return true;
}

} // namespace

int main() {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    rhi::SwapchainDesc sd;
    sd.width = 64;
    sd.height = 48;
    rhi::Swapchain* swapchain = device->CreateSwapchain(sd);
    {
        RendererDesc rd;
        rd.device = device.Get();
        rd.swapchain = swapchain;
        rd.build = &Build;
        Renderer renderer(rd);
        for (int i = 0; i < 3; ++i) std::printf("frame %d: %s\n", i, FrameStatusName(renderer.RenderFrame(nullptr, nullptr, nullptr)));
        swapchain->Resize(0, 0); // minimized
        std::printf("minimized: %s\n", FrameStatusName(renderer.RenderFrame(nullptr, nullptr, nullptr)));
        std::printf("rendered %llu, skipped %llu\n", static_cast<unsigned long long>(renderer.Stats().frames_rendered),
                    static_cast<unsigned long long>(renderer.Stats().frames_skipped));
    }
    device->DestroySwapchain(swapchain);
    device.Reset();
    return 0;
}
```

```output
frame 0: ok
frame 1: ok
frame 2: ok
minimized: skipped
rendered 3, skipped 1
```

### Vertex colors and primitive topology

A mesh is a list of triangles by default; `MeshData::Set` takes a [[rhi::PrimitiveTopology]] to make it a strip, a line
list or a point list instead, and the index count must then make whole primitives of that topology. In a strip the
all-ones index (`0xFFFF` for 16-bit indices, `0xFFFFFFFF` for 32-bit) ends the current strip and starts the next one;
`MeshData::HasPrimitiveRestart` says whether the indices contain one. A 16-bit strip addresses at most 65535 vertices,
because `0xFFFF` is taken. `GpuMesh` carries both facts, and `MaterialSystem::GetPipeline` and `Warmup` take them as
arguments, so a strip and a list of the same material get different pipelines (restart is part of the pipeline in Vulkan).
A pipeline asks for restart only on a strip topology; `rhi::PipelineDesc` rejects it otherwise.

Per-vertex colors are a fifth attribute in a separate layout, `ColorVertexLayout` (location 4, `UNorm8x4`, 52 bytes per
vertex), so a mesh without colors pays nothing. `MakeColoredMesh` adds a color array to a standard-layout mesh and
`MakeUniformColoredMesh` gives every vertex one color; the colors are 8-bit sRGB-encoded values and the shader
converts them to linear (Day 79). The color is interleaved with the other attributes rather than in a second stream, which
keeps one vertex buffer per mesh.

`MakePlaneStripMesh` builds the same grid as `MakePlaneMesh` as one strip with a restart between rows, which needs about
two thirds of the indices. `ListToStrip` turns any triangle list into a strip (greedy walk over shared edges, joined by a
restart or by degenerate triangles, as you choose) and `StripToList` reverses it; both keep every triangle and its winding.

```cpp compile standalone run
#include <cstdio>

#include "renderer/mesh.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    MeshData list, strip, back;
    if (MakePlaneMesh(2.0f, 2.0f, 8, 8, &list) != nullptr) return 1;
    if (ListToStrip(list, StripJoin::Restart, &strip) != nullptr) return 1;
    if (StripToList(strip, &back) != nullptr) return 1;
    std::printf("list %u indices, strip %u indices, back %u indices, restart %s\n", list.IndexCount(), strip.IndexCount(),
                back.IndexCount(), strip.HasPrimitiveRestart() ? "yes" : "no");
    return 0;
}
```

```output
list 384 indices, strip 151 indices, back 384 indices, restart yes
```

### The G-buffer geometry pass

`GBufferPass` is the first real pass of the deferred renderer. It draws every visible object once, with its material, into
four targets (the library is `renderer_passes`; the shaders are `gbuffer.vert` and `gbuffer.frag`):

| Target | Format | Contents |
|---|---|---|
| albedo | `RGBA8_SRGB` | rgb linear albedo (the attachment encodes sRGB on write), a = baked occlusion |
| normal | `RG16_Float` | world-space normal, octahedral (`OctEncode` / `OctDecode`) |
| material | `RGBA8_UNorm` | r roughness, g metallic, b emissive scale, a flags / 255 |
| depth | `D32_Float` | 0 at the near plane, 1 at the far plane, no reverse-Z |

`CheckGBufferFormats` asks the device for every format with the usage the pass needs and names the first one it lacks; call
it when the renderer is created. `FitCameraDepthRange` sets a camera's near and far planes from the scene's bounding spheres.

A frame uses the pass in two steps. `Prepare(scene, frustum)` runs on the frame's thread, after `FrameResources::BeginFrame`
and before `Flush`: it culls the scene, sorts the visible objects by `MakeDrawSortKey` (pipeline, then material, then mesh,
with the scene slot breaking ties, so the order objects were added never matters), writes the per-object data in that order
and builds the draw list. Runs of the same mesh and material become one instanced draw. `AddToGraph(graph, width, height)`
declares the four targets as transients and the pass that renders them. Recording binds the pipeline and the material group
only when they change and creates nothing; the frame and object bind groups are cached by the material system
(`FrameBindGroup`, `ObjectBindGroup`).

Materials come from `CreateGBufferMaterial(materials, templates, options, name)`. The options (`normal_map`, `alpha_test`,
`vertex_color`) pick the fragment permutation, and `vertex_color` also picks the vertex shader that reads the mesh's color, so
the two stages cannot disagree. Per-vertex colors are authored in sRGB; the shader converts them to linear and multiplies
the albedo. The parameters are `base_color`, `roughness`, `metallic`, `emissive_scale`, `alpha_cutoff`, `occlusion` and `flags`,
and the textures `albedo_tex` and `normal_tex`. `Prepare` leaves an object out and names it in `LastError` when its mesh was
never prepared (`GpuMesh::RecordPrepare`, then `MarkReady` after the submit), its material does not exist, no pipeline fits
the mesh, or the mesh and the material disagree about vertex colors (a colored mesh through a material that ignores them is
refused, not drawn plain). The rest of the frame is still drawn and `Prepare` returns false.

The later passes read the targets as `Sampled`: a depth texture read through a sampler is `Sampled`, while `DepthRead` is for
the depth test without writes (the two need different image layouts). `GBufferDebugView` draws one layer (albedo, normal,
material or linear depth) into a color target for debugging; it creates one bind group per frame and is not part of the
per-frame cost budget.

```cpp compile standalone run
#include <cstdio>

#include "renderer/gbuffer.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    int rc = 0;
    {
        char error[128];
        if (!CheckGBufferFormats(device.Get(), error, sizeof(error))) return 1;

        // A direction survives the trip through two octahedral coordinates.
        const Vec3 n(0.36f, -0.48f, -0.8f);
        const Vec2 e = OctEncode(n);
        const Vec3 d = OctDecode(e);
        std::printf("octahedral (%.3f, %.3f) -> (%.3f, %.3f, %.3f)\n", e.x, e.y, d.x, d.y, d.z);

        MaterialSystem materials(device.Get(), MaterialSystemDesc{});
        const GBufferTemplates templates = CreateGBufferTemplates(&materials);
        GBufferOptions options;
        options.vertex_color = true;
        const MaterialId material = CreateGBufferMaterial(&materials, templates, options, "painted");
        std::printf("a vertex color material reads the color attribute: %s\n", materials.ReadsVertexInput(material, 4) ? "yes" : "no");
        rc = material.IsValid() ? 0 : 1;
    }
    device.Reset();
    return rc;
}
```

```output
octahedral (0.707, -0.780) -> (0.360, -0.480, -0.800)
a vertex color material reads the color attribute: yes
```

### Deferred lighting and the tonemap

`LightingPass` turns the G-buffer into linear HDR light. `Prepare(scene, frame_data)` writes the scene's lights through
`FrameResources::WriteLights` (directional lights first, then the visible point lights, ambient lights summed) and plans the
point light draws. `AddToGraph(graph, targets, width, height)` declares the `RGBA16_Float` accumulation target and a pass that
reads the four G-buffer layers as `Sampled`, draws one fullscreen triangle for the ambient and directional light, and then
draws the point light volumes (see "Point lights" below). Light types are `Directional` (the unit vector `direction` points
from the surface toward the light), `Ambient` (`color * intensity` from everywhere) and `Point`. A light's radiance is
`color * intensity`.

Per pixel the shader reads the depth; a value of 1.0 is background and is discarded, leaving the cleared black. Otherwise it
rebuilds the world position from the pixel and the depth: `ndc = (2 (pixel + 0.5) / size - 1, depth)`, then
`world = inverse_view_projection * (ndc, 1)` divided by `w`. The projection's Y flip (clip Y points down) is already inside
that inverse, so the pixel row is used as it is: row 0 is the top of the picture, as in Vulkan's framebuffer. On Direct3D 12 the
same holds: the D3D12 build of every vertex shader negates clip-space Y once more (`ENGINE_CLIP`, `shaders/README.md`), so the
projection, this reconstruction and the light scissor rectangles keep one convention on every backend, and the renderer never
chooses a projection per backend. Using the
OpenGL convention here would mirror the picture, which the world-position debug layer (`GBufferLayer::WorldPosition`) and a
test against known geometry catch. `ReconstructWorldPosition` is the CPU twin.

The lighting model lives in one function, `ShadeLight` in `lighting_common.slang` (CPU twin `renderer::ShadeLight`), so a later
phase can change it. For a unit normal `N`, view vector `V` and light vector `L`:

- `a = max(roughness, 0.045)^2`, `H = normalize(V + L)`;
- `D = a^2 / (pi (dot(N,H)^2 (a^2 - 1) + 1)^2)` (Trowbridge-Reitz GGX);
- `Vis = 0.5 / (NL sqrt(NV^2 (1 - a^2) + a^2) + NV sqrt(NL^2 (1 - a^2) + a^2))` (height-correlated Smith, with the `1 / (4 NL NV)`);
- `F = F0 + (1 - F0) (1 - dot(V,H))^5` with `F0 = lerp(0.04, albedo, metallic)` (Schlick);
- one light adds `(albedo (1 - metallic) + pi D Vis F) * radiance * max(dot(N, L), 0)`;
- the ambient light adds `albedo (1 - metallic) * ambient * occlusion`, and emission adds `albedo * emissive_scale`.

The diffuse BRDF is the albedo itself: the `1/pi` of a Lambertian surface is folded into the intensity, so a white surface
facing a light of intensity 1 comes out at 1 (the same convention as the Phase 4 forward shader), and the specular lobe carries
a factor `pi` to match.

`TonemapPass` reads the accumulation target and writes a display target: `x = hdr * exposure`, then Narkowicz's ACES fit
`clamp(x (2.51 x + 0.03) / (x (2.43 x + 0.59) + 0.14), 0, 1)` (`TonemapAces`). It does not encode sRGB itself; the target
must be an `*_SRGB` format, which is what the swapchain format is chosen to be, so the hardware does the encoding.

```cpp compile standalone run
#include <cstdio>

#include "renderer/lighting.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    SurfacePoint surface;
    surface.albedo = Vec3(0.5f, 0.5f, 0.5f);
    surface.roughness = 1.0f;
    const Vec3 up(0, 1, 0);
    // A light straight overhead at intensity 2, seen from above: diffuse 0.5 * 2 = 1 plus a small GGX lobe.
    const Vec3 lit = ShadeLight(surface, up, up, Vec3(2, 2, 2));
    std::printf("lit %.3f, tonemapped %.3f\n", lit.x, TonemapAces(lit.x));
    // Away from the light only the ambient term remains.
    const Vec3 away = ShadeLight(surface, up, Vec3(0, -1, 0), Vec3(2, 2, 2));
    const Vec3 ambient = ShadeAmbient(surface, Vec3(0.1f, 0.1f, 0.1f));
    std::printf("away %.3f, ambient %.3f\n", away.x, ambient.x);
    return 0;
}
```

```output
lit 1.020, tonemapped 0.808
away 0.000, ambient 0.050
```

#### Point lights

A point light has a `position`, a `radius`, a `color` and an `intensity`. Its radiance at a surface point at distance `d` is
`color * intensity * PointLightAttenuation(d, radius)`, shaded by `ShadeLight` with the light vector pointing at the position:

`attenuation(d, R) = saturate(1 - (d / R)^4)^2 / (d^2 + 1)`

It is 1 at the light, behaves like an inverse square over most of its range (the `+ 1` keeps it finite), and the window makes it
reach exactly 0 at `d = R` with zero slope, staying 0 beyond. So the sphere of radius `R` is a true bound: a pixel outside it
receives exactly nothing, which is what lets the renderer light only the pixels the sphere covers.

Per frame, `WriteLights` leaves out every point light whose sphere is entirely outside the view frustum (the Day 73 sphere test,
`LightSummary::point_culled`) and sorts the rest nearest-first by squared distance from the camera, ties broken by the light's
slot, so the order is the same on every run. The lights go into one storage buffer, directional lights first. There is no
per-light bind group or pipeline: the volume pass binds one pipeline and the G-buffer group once and draws one light volume per
light, selecting the light by the instance index (`first_instance`). Each light volume is a sphere of triangles, generated in the
vertex shader from the vertex index (`kLightVolumeVertexCount` = 768 vertices; `LightVolumeVertex` is the CPU twin), enlarged by
`LightVolumeScale()` so its faces stay outside the light's sphere. The pipeline blends additively into the accumulation target,
culls front faces, so a camera inside a light's sphere still draws the far side and lights everything it sees, and has no depth
test. Each draw is scissored to the light's screen rectangle (`LightScreenRect`: the projection of the sphere's bounding box,
the whole target when the box reaches the camera plane); a light whose rectangle is empty gets no draw. `PointDrawsPlanned()`
and `PointDrawsRecorded()` report the draw counts.

Limits are enforced, not silent. A scene and a `FrameResources` hold at most `kMaxLights` (16384) lights; asking for more
aborts. If the visible lights do not fit in the buffer's `max_lights`, directional lights are written first, then the nearest
point lights; the rest are counted in `LightSummary::dropped` and an error is logged. Size the buffer to the scene's capacity
and the count is always zero.

Measured on the development machine (an NVIDIA GPU, release build, 1920x1080 G-buffer + lighting + tonemap, mean wall time per
frame from submit to idle, lights of radius 2.5 spread over a 24 x 24 plane): 1 light 0.6 ms, 100 lights 1.1 ms, 1000 lights
6.0 ms, with one draw per light. The numbers are recorded, not asserted.

```cpp compile standalone run
#include <cstdio>

#include "renderer/lighting.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

int main() {
    // A light of radius 10: full strength at the light, a quarter of the way out already much less, and nothing at the radius.
    for (f32 d : {0.0f, 2.5f, 5.0f, 9.0f, 10.0f, 12.0f}) {
        std::printf("d = %4.1f  attenuation %.5f\n", d, PointLightAttenuation(d, 10.0f));
    }
    return 0;
}
```

```output
d =  0.0  attenuation 1.00000
d =  2.5  attenuation 0.13686
d =  5.0  attenuation 0.03380
d =  9.0  attenuation 0.00144
d = 10.0  attenuation 0.00000
d = 12.0  attenuation 0.00000
```

### Surviving a reset

The device can be destroyed and created again while the program runs (a settings change, a lost device; see the RHI guide, "Lifecycle and
reset"). Everything in the renderer that owns device objects must therefore be able to make them again. The rule for an owner:

1. **Keep the CPU-side source** (pixels, mesh data, descriptors, or a loader that produces them), never only the GPU object.
2. **Register** with the [[renderer::DeviceResources]] registry at your tier (the `renderer::tiers` constants), with a [[renderer::ScopedRegistration]]
   member, so a destroyed owner is never called and a moved one follows its move.
3. **Implement** [[renderer::Recreatable]]: `Release` destroys your device objects and keeps the source; `Recreate` makes them again.
4. **Never keep a handle past a frame** for anyone else: handles change at every reset. Hold the owner (a `GpuMesh*`, a `GpuTexture*`)
   and read `Handle()` when you record.

The registry is the device host's observer: `OnDeviceReleasing` releases owners from the highest tier down, `OnDeviceCreated` recreates
them from the lowest up (0 textures and samplers, 1 meshes, 2 layouts and shaders, 3 materials and pipelines, 4 passes and per-frame
buffers). A `Recreate` that fails stops the sequence, releases everything that was built and names the owner
([[renderer::DeviceResources::FailedOwner]]); the host fails the reset and the caller rolls back.

The new device need not be of the same backend: a change of the `backend` setting (Vulkan to Direct3D 12 and back) or of the
`adapter` is the same reset. So `Recreate` must not assume anything about the device it got: pick shader bytecode by
[[rhi::DeviceCaps::shader_format]] (a [[rhi::ShaderBlobSet]] or [[renderer::PermutationModule::Blob]]), read limits and alignments
from its caps again, and expect the object counts to differ where the backends differ (Direct3D 12 adds the renderer's object index
stream, [[rhi::DeviceCaps::object_index_stream]]). `tests/rhi/backend_switch_gpu_test.cpp` switches 20 times and checks the picture
against the golden after each switch.

[[renderer::GpuMesh]] gets its source with `MakeRecreatable` (a borrowed [[renderer::MeshData]] or a loader) or `AdoptSource` (the mesh
keeps the data); [[renderer::GpuTexture]] holds a [[renderer::TextureImage]] or a [[renderer::TextureLoader]] (an asset file is read again
at every recreate, so its pixels are not kept in memory); [[renderer::GpuSampler]] keeps its description. A device with smaller limits
(`max_texture_dimension_2d`, a missing format) makes `Recreate` fail with a log line; a texture that cannot get its mip chain
generated falls back to the levels its source holds.

```cpp compile standalone run
#include <cstdio>

#include "rhi/backends/backends.h"
#include "renderer/device_resources.h"
#include "renderer/texture.h"
#include "rhi/device_host.h"

using namespace engine_placeholder;

int main() {
    rhi::RegisterCompiledBackends(); // once, before any device
    renderer::DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    host.Create(info, rhi::SwapchainDesc{});

    renderer::TextureImage image;
    image.desc.width = image.desc.height = 4;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(4 * 4 * 4);
    renderer::GpuTexture texture;
    texture.Create(host.GetDevice(), static_cast<renderer::TextureImage&&>(image), &registry, "checker");

    const rhi::TextureHandle before = texture.Handle();
    host.Recreate(info, rhi::SwapchainDesc{});   // the registry rebuilds the texture on the new device
    std::printf("valid: %s, handle changed: %s\n", texture.Valid() ? "yes" : "no", texture.Handle() != before ? "yes" : "no");
    texture.Destroy();
    host.Destroy();
    return 0;
}
```
```output
valid: yes, handle changed: yes
```

#### What each owner does at a reset

| Owner | `Release` | `Recreate` | Kept on the CPU |
|---|---|---|---|
| [[renderer::MaterialSystem]] (tier 3) | destroys layouts, default textures and sampler, shaders, parameter buffers, bind groups and pipelines | rebuilds them in that order; pipelines lazily on first use, or all at once with `MaterialSystemDesc::prewarm_pipelines` | template descriptors and reflection, parameter values, owner bindings, pipeline descriptors |
| [[renderer::GBufferDebugView]], [[renderer::LightingPass]], [[renderer::TonemapPass]] (tier 4) | destroys shaders, layouts, sampler, pipelines | rebuilds them; the tonemap and debug pipelines are made by the next frame for the output format it has then | the embedded SPIR-V |
| [[renderer::FrameResources]] (tier 4) | unmaps and destroys the frame, object and light buffers | creates them again at the same capacities | nothing: every frame rewrites all of it from the scene |
| [[renderer::TransientPool]] (tier 4) | destroys every pooled texture and buffer | nothing; the next frame takes what it needs | nothing (contents are per frame) |
| [[renderer::GBufferPass]] | nothing: it holds only per-frame lists | | |

Materials bind textures and samplers **by owner** (`MaterialSystem::SetTexture` with a [[renderer::GpuTexture]] pointer), so a
material follows the owner's new handle without being told: `FlushMaterials` notices the changed handle and rebuilds the bind group. A
material given a raw handle forgets it at a reset (the handle belongs to the old device) and reads its default texture until the caller
sets it again, so use the owner overloads for anything that must survive. **Pipelines: lazy or eager?** Lazy is the default: a reset
costs only the layouts, shaders and bind groups, and each pipeline is compiled when a draw first needs it (the pipeline cache file on
disk makes that cheap). Eager moves all the compile cost into the reset, so no frame after it stutters; choose it when a reset happens
at a loading screen. After a reset call [[renderer::Renderer::Rebind]] with the new device and swapchain; the pool and the passes are
rebuilt through the registry.

### Changing settings at run time

[[renderer::Renderer::ApplySettings]] changes any rendering setting while the program runs. It diffs the request against the settings in
force and performs the **cheapest sufficient action**, the highest cost class among the changed settings:

| Class | What the renderer does | Example settings |
|---|---|---|
| `None` | stores the value | a setting nothing reads yet |
| `Dynamic` | stores it; the next frame reads it | `gbuffer_debug_layer`, `render_scale` |
| `Swapchain` | `ReconfigureSwapchain` (and a fullscreen request to the window) | `present_mode`, `width`, `height`, `fullscreen`, `swapchain_format`, `hdr` |
| `PipelineRebuild` | releases and recreates the registry's tiers 3 and above (materials, passes); the device stays | `shader_quality` |
| `DeviceReset` | `DeviceHost::Recreate`; every registered owner rebuilds (the sequence in the RHI guide) | `backend`, `adapter`, `validation`, `sync_validation`, `frames_in_flight` |

**When it runs.** Called between frames it applies at once. Called inside a frame (from the build callback or a job) it is *queued* and
applied at the start of the next `RenderFrame`, before anything of that frame exists; a newer request replaces a pending one. It runs on
the owning thread with the device idle (`Quiesce`), so no worker is still recording; the job scheduler itself is not touched. While a
change runs, the window keeps showing its last picture, and the first frame after a swapchain change is a normal frame into the new
images (never uninitialized memory). The renderer calls the functions given to [[renderer::Renderer::SetResetCallbacks]] around any change
that touches the GPU, so the application can log a message, pause audio or drop its own GPU caches.

**Failure.** If the new device or swapchain cannot be made, or an owner cannot rebuild, the renderer puts the **previous settings** back
(it creates the previous device again). The result has `rolled_back` set and `reason` says why; the settings generation does not change.
If the rollback fails too, the renderer is in the device-lost state: `RenderFrame` returns `DeviceLost` and `ApplySettings` fails. A
rollback creates a new device, so rebind your own pointers from [[renderer::Renderer::GetDevice]] also when the end callback reports a
failure. A setting that needs a missing capability (fullscreen without a window that can do it, HDR on an SDR surface) is left as it was
and listed in `ApplyResult::change.unsupported`. A renderer built without a `DeviceHost` can apply `None` and `Dynamic` changes only.

```cpp compile standalone run
#include <cstdio>

#include "rhi/backends/backends.h"
#include "renderer/renderer.h"
#include "rhi/device_host.h"

using namespace engine_placeholder;

static bool Build(const renderer::FrameContext&, void*) { return true; }

int main() {
    rhi::RegisterCompiledBackends(); // once, before any device
    renderer::DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    host.Create(info, rhi::SwapchainDesc{});

    renderer::RenderSettings initial;                              // what the host's device was created with
    initial.SetEnum(renderer::settings::kBackend, 0);
    renderer::RendererDesc desc;
    desc.initial_settings = &initial;
    desc.device = host.GetDevice();
    desc.host = &host;
    desc.create_info = info;
    desc.resources = &registry;
    desc.build = &Build;
    renderer::Renderer renderer(desc);

    renderer::RenderSettings wanted = renderer.Settings();
    wanted.SetEnum(renderer::settings::kGBufferDebugLayer, 2);      // dynamic: no GPU work
    renderer::ApplyResult dynamic = renderer.ApplySettings(wanted);
    wanted.SetText(renderer::settings::kAdapter, "null-integrated"); // device reset
    renderer::ApplyResult reset = renderer.ApplySettings(wanted);
    std::printf("%s ok=%d, %s ok=%d, adapter %s\n", renderer::CostClassName(dynamic.performed), dynamic.ok,
                renderer::CostClassName(reset.performed), reset.ok, host.GetDevice()->Adapter().id);
    return 0;
}
```
```output
dynamic ok=1, device-reset ok=1, adapter null-integrated
```

### Device loss and recovery

A lost device is not terminal any more. When a frame ends `DeviceLost` and the renderer has a `DeviceHost`
([[renderer::RendererDesc::host]]), it runs the same reset as a device-reset settings change with the settings in force, following
[[renderer::RecoveryPolicy]]:

- the **first attempt** is made at once, in the frame that found the loss, which still returns `DeviceLost` (so the frame counters add up);
- if it fails, the frames that follow return `Skipped` while the renderer waits (`delay_frames`, doubling for each further attempt, up to
  `max_delay_frames`; counted in frames, not seconds), then it tries again, up to `max_attempts` (default 3);
- from the **second attempt** on, with `fallback_adapter` set, a saved adapter that keeps failing is replaced by the best one (the
  `adapter` setting is cleared and the change logged);
- after the last attempt the renderer gives up: it is device-lost for good (`IsBroken`), every frame returns `DeviceLost`, and `GetDevice`
  returns null. `max_attempts = 0` turns recovery off.

A **lost surface** on a healthy device (the swapchain returns `Lost` but `DeviceLost` is false) is repaired first with
`DeviceHost::RecreateSurface`, which keeps the device. Each attempt logs on `LogRenderer`, calls the reset callbacks with the stage name
`"recovery"` (so the application can show a message and rebind its own pointers from `GetDevice` when the end callback reports
success), counts in `RendererStats::recoveries` and `recovery_failures`, and shows `recovering (attempt N)` in the crash report. A
settings change asked for during the lost frame is applied at the first frame boundary after the recovery, not lost.

**What the application should show.** While frames return `Skipped` the window keeps its last picture; show a short "graphics device
lost, recovering" line from the `"recovery"` callback and clear it when the end callback reports success. If the renderer gives up,
tell the user and offer to restart the program (a driver that cannot create a device after three attempts will not on a fourth).

### The renderer demo

`renderer_demo` goes through `Renderer::ApplySettings`: with a window open, F1-F10 change settings live (vsync, validation, GPU, fullscreen, swapchain format, shader quality, a forced reset, a simulated device loss), `--gpu` and `--list-gpus` choose the GPU, and `--script` drives the same paths without a keyboard (`game/renderer_demo/README.md` lists the keys and the script words). The demos have no settings file or generic `--set` option: the engine has no settings system yet (`KNOWN-ISSUES.md`).

`game/renderer_demo` puts the pieces together: a scene of 540 objects in eight materials, 300 point lights, a sun and ambient
light, drawn by `Renderer::RenderFrame` through the G-buffer, lighting (with light volumes) and tonemap passes, in a window or
offscreen. Its README lists the options and controls and the tutorial [From a Scene to a Lit Frame](../tutorials/hello-renderer.md)
walks through the same steps on the null device. Depth correctness is tested in
`tests/rhi/renderer_demo_gpu_test.cpp`: the demo scene rendered with the objects added in three shuffled orders gives the
same pixels as the natural order, and two planes crossing at a line show the nearer one on each side of it, in either draw order.

### Failure behavior

Every function in `engine/renderer` either aborts or reports. An `ENGINE_ASSERT` marks a **programming error**: a null or
out-of-range argument, a stale id, a call made out of order (`FrameResources::WriteObjects` before `BeginFrame`). It is documented
as `Aborts:` and never fires because of what the machine does. Everything that can go wrong at run time because of the **environment**
(a lost device, memory the device refuses, a shader that does not match the conventions, a full list) is logged through the
`LogRender` channel, reported through a return value, and leaves the object usable or cleanly destroyable. Each row is exercised by a test: `tests/renderer/renderer_fault_test.cpp` and `renderer_test.cpp` (null device, so a loss can land in the callback or in either pass),
`gbuffer_test.cpp` and `lighting_test.cpp` (rejected objects, dropped lights), and `tests/rhi/renderer_demo_gpu_test.cpp` (the real
device, where the injected loss fires only at the start of a frame).

| What goes wrong | What you get | Afterwards |
|---|---|---|
| The device is lost before, during or after the frame callback, or while a pass records | `RenderFrame` returns `DeviceLost`; `frames_lost` counts it | With a `DeviceHost` the renderer recovers (below); otherwise every later frame returns `DeviceLost` at once and you destroy the renderer and the device |
| The device is lost while the scene is being created | `DemoScene::Create` / `GpuMesh::Create` return false, logging why | The partly built scene frees what it made |
| `CreateCommandList` returns null (device lost, out of memory) | `RenderFrame` returns `DeviceLost` or `Failed`; a caller of `RenderGraph::Execute` must check for null first, as a null list aborts | Nothing is submitted |
| A transient texture or buffer cannot be created | `RenderFrame` returns `Failed`; `LastError` names the resource | Pooled resources go back to the pool; the next frame is tried normally |
| The frame callback returns false | `RenderFrame` returns `Failed`; `LastError` says the callback abandoned the frame | The swapchain image is presented; the next frame is tried normally |
| A material's texture file is missing | The demo logs a warning and uses the procedural texture | The scene renders normally |
| An object has no pipeline (a mesh layout the material cannot read, an unprepared mesh, an unknown material) | The object is left out; `GBufferPass::Prepare` returns false, `LastError` names the first one, `Stats().rejected` counts them | The frame is `Ok` and draws everything else |
| A pass's pipeline could not be built | The pass logs an error and records nothing (`TonemapPass`/`LightingPass` `Valid()` is false at creation) | The frame is `Ok` but that pass draws nothing |
| More point lights than the buffer holds | The farthest are dropped and counted (`LightSummary::dropped`), logged once | The nearest are kept |
| The swapchain is minimized | `Skipped` | The next visible frame renders |

The tests also leave nothing behind: after each loss, failure and teardown the device holds no buffers, textures, bind groups or
pipelines, and `VulkanDevice::GetLeakCounts()` stays at zero.

### Performance notes

Recorded by `renderer_bench` on 2026-11-05: NVIDIA GeForce RTX 3060, AMD Ryzen 9 7950X3D, Linux, a **Release** build, validation
off, a 1280x720 offscreen target, the demo scene with 300 point lights (except where the light count varies), the device idled
after each frame outside the timer so the numbers are CPU time. Two runs agree within a few percent. They are a baseline, not
a promise, and are never asserted in CI. Do not tune on a Debug build: it is five to seven times slower here.

| Measurement | Result |
|---|---|
| Render graph, build + compile, a chain of 10 / 100 / 1000 passes | 0.6 / 5.4 / 54 microseconds (about 0.05 us per pass) |
| Whole frame, 1000 objects | 0.11 ms |
| Whole frame, 10000 objects | 0.78 ms (0.08 us per object) |
| Whole frame, 50000 objects | 3.9 ms (0.08 us per object) |
| The same frames with a job scheduler | no faster (0.15 / 0.8 / 3.9 ms): the cost is scene preparation on one thread, and the three passes depend on each other |
| What the 10000-object frame draws | 9312 visible objects in 16 draw calls, 3 pipeline binds and 9 material binds |
| Point lights: 100 / 1000 / 4000 / 16000 | +0.01 / +0.15 / +0.9 / +3.1 ms; about 0.1-0.2 us of CPU per light |
| Transient pool, 100 frames | 5 textures created, 520 reuses (99% hit rate), none destroyed |

The renderer instances objects that share a mesh and material, so 10000 objects cost 16 draws and the per-draw figure in
the [RHI performance notes](rhi.md#performance-notes) (about 0.5 us per draw, unique data for each draw) does not apply
to it; per object it is 0.08 us, which is culling, sorting, and writing the object's matrices. A scene in which no two objects
share a mesh and material loses the instancing and approaches one `rhi_bench` draw per object. A `perf` profile of the 10000-object
frame put the draw sort's comparator at 18%, `WriteObjects` at 8%, the per-object pipeline lookup (`ReadsVertexInput` and
`GetPipeline`) at 10%, the frustum test at 5% and the normal matrix at 3%; nothing else is above 2%. Caching the pipeline
per (mesh, material) pair rather than per object, and sorting by a precomputed key, are the first things to try when scenes grow
past 100000 objects.

### Rules & contracts

- **Order inside a frame.** `Device::BeginFrame`, then `MaterialSystem::BeginFrame` and `FlushMaterials`, `FrameResources::BeginFrame`, the
  frame block and objects/lights (`WriteFrameData`, each pass's `Prepare`), `FrameResources::Flush`, then each pass's `AddToGraph`, then
  `Compile`/`Execute`, then submit. `Renderer::RenderFrame` does the device and graph steps; your frame callback does the rest.
  Calling out of order aborts (`Aborts:` in the reference).
- **One pass object serves one view per frame.** `Prepare` then one `AddToGraph`; a second `AddToGraph`, or a different size from the
  one given to `Prepare`, aborts.
- **Ownership and order of destruction.** A `GpuMesh` must be destroyed (or go out of scope) before the device that created it; a
  scene holds meshes by pointer and does not own them, so a mesh must also outlive every object that uses it. Tear down in reverse
  order of creation: the renderer, the passes, the frame resources and material system, the meshes and textures, the device.
  Idle the device first (`WaitIdle`); `rhi::DevicePtr` does it when it releases the device.
- **Threads.** Everything is owner-thread-only except what the reference marks otherwise; pass `Prepare` and `AddToGraph` run on the
  frame's thread, and the recorded callbacks may run on scheduler participants, reading only what `Prepare` left.
- **Contract violations abort; environment failures report.** See "Failure behavior".
- **Shaders follow the set convention** (set 0 frame, set 1 material, set 2 objects) and read the object index from
  `SV_VulkanInstanceID`; see `shaders/README.md`.

### Common mistakes

- **Ignoring what `Prepare` returns.** `GBufferPass::Prepare` returns false when it left objects out; `LastError` names the first and
  `Stats().rejected` counts them. The frame still renders, so nothing else tells you an object is missing.
- **Adding a pass twice or out of order** (see the rules above). The abort message names the call.
- **Drawing a mesh before it is prepared.** `GpuMesh::Create` only creates buffers; record `RecordPrepare` into a submitted list and call
  `MarkReady` once the upload completed.
- **Passing a null list to `RenderGraph::Execute`.** `CreateCommandList` returns null when the device is lost; check it.
- **Tuning on a Debug build or with validation on.** The numbers in "Performance notes" are Release without validation.
- **Naming Vulkan outside `engine/rhi/vulkan/`.** The `rhi_boundary_check` ctest fails the build.
- **Lights past the buffer.** More than `max_lights` point lights keep the nearest and count the rest in `LightSummary::dropped`.

### Platform notes

The renderer is written and tested on Linux with the Vulkan backend (and the null device, which every non-GPU test uses), and on Windows
with Vulkan and Direct3D 12 (Phase 13); macOS (Metal, Phase 14) is planned behind the same RHI, and every shader already compiles to
Metal (`ctest -R shaders_portability`). The points that shape code are in the [renderer portability
review](../superpowers/specs/2026-11-04-renderer-portability-review.md); a deferred G-buffer is a poor fit for tile-based mobile GPUs
unless the passes are merged (Phase 22).

**On Direct3D 12** (Phase 13 Day 171) the renderer runs unchanged; what differs is below it:

- **Projection.** The projection functions keep Vulkan's clip space; the D3D12 build of each shader negates clip-space Y
  (`ENGINE_CLIP`, `shaders/target_common.slang`), so the same matrices, viewports and winding work on both.
- **The object index.** D3D12 shaders have no base instance: they read the index from `MaterialSystem::ObjectIndexStream`, an
  instance-rate vertex buffer of 0, 1, 2, ... at location 15 that the material pipelines (and the light volumes) bind when
  [[rhi::DeviceCaps::object_index_stream]] is set; a draw's `first_instance` then offsets into it. A frame has at most 65536 objects there.
- **Storage buffers** are structured views, so their bindings carry a stride (the object and light buffers do; reflection gives the
  materials' one), and slices of them start on the lcm of the storage alignment and the element size.
- **Tested:** the G-buffer, lighting, render-graph and demo-scene GPU tests run on both backends (`tests/rhi/*_gpu_test.cpp`); the demo
  scene's image on DX12 is within 1 level of Vulkan's on the same GPU, and both match `renderer_demo.ppm`.
- **Measured** (`renderer_bench`, Release, Radeon 860M): a 50000-object frame takes 7.1 to 7.3 ms of CPU time on DX12 against 6.4 to
  7.1 on Vulkan; 16000 point-light volume draws 4.4 ms against 3.15 (0.27 against 0.19 us a draw), the D3D12 draw call's own cost. Sanitizers: the TSan flavor excludes the `[gpu]` tests (the proprietary NVIDIA driver's threads crash it), the ASan
flavor runs them; see [the RHI guide](rhi.md#platform-notes).

### See also

- [The RHI](rhi.md) and the [renderer design](../superpowers/specs/2026-11-04-renderer-design.md).
- [Renderer portability review](../superpowers/specs/2026-11-04-renderer-portability-review.md): what Phases 13-14 and 22 add.
- [Shaders: from Slang to a shader module](rhi-shaders.md) and [Tutorial: From a Scene to a Lit Frame](../tutorials/hello-renderer.md).

## What the renderer adds to a crash report

Each frame the renderer stores its frame number in a crash state counter, so a report shows `frame: <n>` next to the `gpu:` line
the RHI registers. Add your own counter with [[debug::CrashStateCounter]] and store to it as often as you like (one atomic store).
