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
- [[renderer::LightingPass]] — directional, ambient and point deferred lighting into an HDR target; [[renderer::TonemapPass]] brings it to the display
- [[renderer::GBufferPass]] — culls, sorts and draws the scene into the G-buffer; [[renderer::GBufferDebugView]] shows a layer

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
test (`tests/rhi/vulkan_render_graph_test.cpp`) runs a clear, a sampling draw, a compute write and a combining draw with
validation and synchronization validation on and expects no messages.

### Frames, parallel recording and the swapchain

`Renderer` is the frame loop. It owns what lives across frames (the transient pool and the per-frame arena) and borrows the
device, an optional swapchain and an optional `jobs::Scheduler`. You give it a build callback; each `RenderFrame` then
opens a device frame, acquires the swapchain image (or imports the `FrameTarget` you pass when there is no swapchain), calls
the callback with a fresh graph that already holds the imported target, compiles, records, submits once and presents.

The status says how the frame ended: `Ok`; `Skipped` when the window is minimized (nothing was submitted, try again);
`DeviceLost` when the device or surface is gone (the renderer stays destroyable, it just cannot render again); `Failed` for
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
that inverse, so the pixel row is used as it is: row 0 is the top of the picture, as in Vulkan's framebuffer. Using the
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
facing a light of intensity 1 comes out at 1 (the same convention as the Phase 3 forward shader), and the specular lobe carries
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

### The renderer demo

`game/renderer_demo` puts the pieces together: a scene of 540 objects in eight materials, 300 point lights, a sun and ambient
light, drawn by `Renderer::RenderFrame` through the G-buffer, lighting (with light volumes) and tonemap passes, in a window or
offscreen. Its README lists the options and controls and the tutorial [From a Scene to a Lit Frame](../tutorials/hello-renderer.md)
walks through the same steps on the null device. Depth correctness is tested in
`tests/rhi/vulkan_renderer_demo_test.cpp`: the demo scene rendered with the objects added in three shuffled orders gives the
same pixels as the natural order, and two planes crossing at a line show the nearer one on each side of it, in either draw order.

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
