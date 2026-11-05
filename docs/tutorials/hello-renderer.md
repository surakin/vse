# Tutorial: From a Scene to a Lit Frame with the Renderer

**Goal:** Build a scene (a mesh and an object), give it a material, add lights, and render a frame with the deferred
renderer - first on the null device (no GPU) and then on a real one with the `renderer_demo` app.

**You need:** a built tree (see getting-started) and the [RHI tutorial](hello-rhi.md) for the device and frame basics. A GPU
and a display are needed only for the last step.

## Steps

1. **Meshes.** Build geometry on the CPU (`MakeSphereMesh`, `MakeCubeMesh`, `MakePlaneMesh`) and copy it to the GPU with
   `GpuMesh::Create`. A new mesh needs its upload barriers once: record `RecordPrepare` in a command list, submit it, then
   call `MarkReady`.
2. **Materials.** A `MaterialSystem` owns materials and pipelines. `CreateGBufferTemplates` makes the two G-buffer
   templates; `CreateGBufferMaterial` makes a material from one, with parameters (`base_color`, `roughness`, `metallic`,
   `emissive_scale`) set by name and textures (`albedo_tex`, `normal_tex`) set with `SetTexture`.
3. **The scene.** A `RenderScene` is a list of objects (mesh, material, transform) and lights. Directional lights point
   *toward* the light; ambient lights are one color; point lights have a position and a radius beyond which they give
   exactly nothing.
4. **The frame.** A `Renderer` runs the frame loop (acquire, build the graph, compile, record, submit, present). Your build
   callback fills the graph: write the per-frame data, `Prepare` the G-buffer and lighting passes, and add the G-buffer,
   lighting and tonemap passes to the graph. The target is the swapchain image, or an offscreen texture.
5. **Run it for real:** `./build/game/renderer_demo/renderer_demo` (add `--backend x11` or `--backend wayland`,
   `--validation`, `--frames 1000`, `--screenshot out.ppm`). Tab switches between the orbit and fly cameras; see
   [the demo's README](../../game/renderer_demo/README.md).

## Complete code

This runs on the null device, which checks every barrier and binding the Vulkan backend relies on and counts the errors
it catches. The real thing is [demo_frame.cpp](../../game/renderer_demo/demo_frame.cpp) (the frame) and
[demo_scene.cpp](../../game/renderer_demo/demo_scene.cpp) (the scene).

```cpp compile standalone run
#include <cstdio>

#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/lighting.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/renderer.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

// Everything the frame callback needs.
struct Frame {
    MaterialSystem* materials = nullptr;
    FrameResources* resources = nullptr;
    GBufferPass* gbuffer = nullptr;
    LightingPass* lighting = nullptr;
    TonemapPass* tonemap = nullptr;
    LightSummary lights;
};

// Declares the passes of one frame on the graph the renderer gives us.
static bool BuildFrame(const FrameContext& context, void* user) {
    Frame* f = static_cast<Frame*>(user);
    f->materials->BeginFrame(context.frame_index);
    f->materials->FlushMaterials();
    f->resources->BeginFrame(context.frame_index);
    const FrameData data = MakeFrameData(*context.camera, context.width, context.height, 0.0f, 0.016f, context.frame_index);
    f->resources->WriteFrameData(data);
    f->gbuffer->Prepare(*context.scene, Frustum::FromViewProjection(context.camera->ViewProjection()));
    f->lights = f->lighting->Prepare(*context.scene, data);
    f->resources->Flush();
    const GBufferTargets targets = f->gbuffer->AddToGraph(context.graph, context.width, context.height);
    const GraphResource hdr = f->lighting->AddToGraph(context.graph, targets, context.width, context.height);
    f->tonemap->AddToGraph(context.graph, hdr, context.target, context.format, 1.0f);
    return true;
}

int main() {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    rhi::Device* d = device.Get();
    {
        // 1. A mesh, uploaded and made ready.
        MeshData sphere_data;
        MakeSphereMesh(1.0f, 32, 16, &sphere_data);
        GpuMesh sphere;
        sphere.Create(d, sphere_data, "sphere");
        d->BeginFrame();
        rhi::CommandList* list = d->CreateCommandList();
        list->Begin();
        sphere.RecordPrepare(list);
        list->End();
        d->Submit(&list, 1);
        d->EndFrame();
        d->WaitIdle();
        sphere.MarkReady();

        // 2. A material: a rough, copper-colored metal.
        MaterialSystem materials(d, MaterialSystemDesc{});
        const GBufferTemplates templates = CreateGBufferTemplates(&materials);
        const MaterialId copper = CreateGBufferMaterial(&materials, templates, GBufferOptions{}, "copper");
        materials.SetVec4(copper, "base_color", Vec4(0.95f, 0.5f, 0.35f, 1.0f));
        materials.SetFloat(copper, "roughness", 0.4f);
        materials.SetFloat(copper, "metallic", 1.0f);

        // 3. A scene: the sphere, a sun, a little ambient light and a point light beside it.
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc object;
        object.mesh = &sphere;
        object.material = copper.index;
        scene.AddObject(object);
        Light sun;
        sun.type = LightType::Directional;
        sun.direction = Vec3(0.3f, 0.8f, 0.5f);
        sun.intensity = 0.8f;
        scene.AddLight(sun);
        Light ambient;
        ambient.type = LightType::Ambient;
        ambient.intensity = 0.1f;
        scene.AddLight(ambient);
        Light lamp;
        lamp.type = LightType::Point;
        lamp.position = Vec3(1.5f, 0.5f, 1.5f);
        lamp.radius = 4.0f;
        lamp.intensity = 5.0f;
        lamp.color = Vec3(1.0f, 0.6f, 0.3f);
        scene.AddLight(lamp);

        // 4. A frame: the passes, the renderer and an offscreen target.
        FrameResources resources(d, 16, 16);
        GBufferPass gbuffer(d, &materials, &resources);
        LightingPass lighting(d, &materials, &resources);
        TonemapPass tonemap(d);
        Frame frame;
        frame.materials = &materials;
        frame.resources = &resources;
        frame.gbuffer = &gbuffer;
        frame.lighting = &lighting;
        frame.tonemap = &tonemap;
        RendererDesc rd;
        rd.device = d;
        rd.build = &BuildFrame;
        rd.build_user = &frame;
        Renderer renderer(rd);

        rhi::TextureDesc td;
        td.width = 320;
        td.height = 180;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        FrameTarget target;
        target.texture = d->CreateTexture(td);
        target.width = td.width;
        target.height = td.height;
        target.format = td.format;
        target.initial = rhi::ResourceState::Undefined;
        target.final_state = rhi::ResourceState::CopySrc;

        Camera camera = MakeLookAtCamera(Vec3(3, 2, 4), Vec3(0, 0, 0), Vec3(0, 1, 0));
        camera.aspect = 16.0f / 9.0f;
        int ok = 0;
        for (int i = 0; i < 3; ++i) {
            ok += renderer.RenderFrame(&scene, &camera, &target) == FrameStatus::Ok ? 1 : 0;
            target.initial = rhi::ResourceState::CopySrc;
        }
        d->WaitIdle();
        std::printf("frames ok: %d, directional: %u, point: %u, point draws: %u, errors: %u\n", ok, frame.lights.directional_count, frame.lights.point_count,
                    lighting.PointDrawsRecorded(), device->ErrorCount());
        d->DestroyTexture(target.texture);
    }
    device.Reset();
}
```

## Expected output

```output
frames ok: 3, directional: 1, point: 1, point draws: 1, errors: 0
```

## What to change next

- **More lights.** Add hundreds of point lights: each costs one scissored draw and nothing else, and a light the camera cannot
  see costs none (see "Point lights" in the [renderer guide](../guide/renderer.md)). `FrameResources` and `RenderSceneDesc`
  must be sized for them (`max_lights`, at most `kMaxLights`); lights that do not fit are counted in `LightSummary::dropped`.
- **Textures and normal maps.** Create the material with `GBufferOptions::normal_map` and `SetTexture(material, "normal_tex",
  texture)`. The demo's textures are written by `renderer_demo_mint` from procedural code, so nothing is a binary you cannot
  regenerate.
- **See the buffers.** `GBufferDebugView` draws one G-buffer layer (albedo, normal, material, depth, world position) instead
  of the lit picture; `renderer_demo --gbuffer normal` does that.
- **Check depth.** Draw order must never change the picture: `tests/rhi/vulkan_renderer_demo_test.cpp` renders the demo scene
  in shuffled order and crosses two planes to check the intersection line.
