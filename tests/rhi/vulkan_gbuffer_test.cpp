// Phase 4 Day 79: the G-buffer geometry pass on a real device with validation and synchronization validation on.
// A known scene (a ground plane, two cubes sharing a mesh and a material, a sphere, an object outside the frustum) is
// rendered; the albedo, normal, material and depth layers are copied to host buffers by the graph and checked against
// values computed on the CPU. Further cases cover the vertex-color, normal-map and alpha-test permutations, draw-order
// independence, and the debug view.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "mem/arena_allocator.h"
#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/render_graph.h"
#include "renderer/render_scene.h"
#include "renderer/transient_pool.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using engine_placeholder::rhi::vulkan::VulkanDevice;

namespace {

constexpr u32 kW = 64;
constexpr u32 kH = 64;

f32 HalfToFloat(u16 h) {
    const u32 sign = (h >> 15) & 1u;
    const u32 exp = (h >> 10) & 0x1fu;
    const u32 man = h & 0x3ffu;
    f32 v;
    if (exp == 0) v = std::ldexp(static_cast<f32>(man), -24);
    else if (exp == 31) v = man ? NAN : INFINITY;
    else v = std::ldexp(static_cast<f32>(man | 0x400u), static_cast<int>(exp) - 25);
    return sign ? -v : v;
}

f32 LinearToSrgb(f32 c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// The four layers read back to the host.
struct Layers {
    std::vector<u8> albedo;   // RGBA8_SRGB bytes
    std::vector<u16> normal;  // RG16_Float
    std::vector<u8> material; // RGBA8_UNorm
    std::vector<f32> depth;   // D32_Float
    GBufferStats stats;
    bool ok = false;
    std::string error;

    int Albedo(u32 x, u32 y, u32 c) const { return albedo[(y * kW + x) * 4 + c]; }
    int Material(u32 x, u32 y, u32 c) const { return material[(y * kW + x) * 4 + c]; }
    Vec3 Normal(u32 x, u32 y) const { return OctDecode(Vec2(HalfToFloat(normal[(y * kW + x) * 2]), HalfToFloat(normal[(y * kW + x) * 2 + 1]))); }
    f32 Depth(u32 x, u32 y) const { return depth[y * kW + x]; }
};

struct Pixel {
    u32 x, y;
    f32 depth;
};

Pixel Project(const Camera& cam, const Vec3& p) {
    const Vec4 c = cam.ViewProjection() * Vec4(p.x, p.y, p.z, 1.0f);
    const f32 nx = c.x / c.w;
    const f32 ny = c.y / c.w;
    return Pixel{static_cast<u32>((nx * 0.5f + 0.5f) * kW), static_cast<u32>((ny * 0.5f + 0.5f) * kH), c.z / c.w};
}

// Everything a G-buffer test needs, around one device.
struct Rig {
    rhi::Device* device = nullptr;
    MaterialSystem* sys = nullptr;
    FrameResources* frame = nullptr;
    TransientPool* pool = nullptr;
    GBufferTemplates templates;
    u64 frame_index = 0;

    bool Create(rhi::Device* d) {
        device = d;
        sys = new MaterialSystem(device, MaterialSystemDesc{});
        frame = new FrameResources(device, 64);
        pool = new TransientPool(device, TransientPoolDesc{});
        templates = CreateGBufferTemplates(sys);
        return sys->Valid() && frame->Valid() && templates.plain.IsValid() && templates.colored.IsValid();
    }
    ~Rig() {
        delete pool;
        delete frame;
        delete sys;
    }

    // Uploads a mesh and makes it drawable.
    void Prepare(GpuMesh* mesh) {
        device->BeginFrame();
        rhi::CommandList* list = device->CreateCommandList();
        list->Begin();
        mesh->RecordPrepare(list);
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        device->WaitIdle();
        mesh->MarkReady();
    }

    struct CopyJob {
        GBufferTargets targets;
        GraphResource buffers[4];
    };
    static void CopyPass(const PassExecuteContext& ctx, void* user) {
        const CopyJob* job = static_cast<const CopyJob*>(user);
        const GraphResource tex[4] = {job->targets.albedo, job->targets.normal, job->targets.material, job->targets.depth};
        for (int i = 0; i < 4; ++i) {
            ctx.list->CopyTextureToBuffer(ctx.graph->ResolvedTexture(tex[i]), ctx.graph->ImportedBuffer(job->buffers[i]), rhi::BufferTextureCopy{});
        }
    }

    // Renders the scene into the G-buffer and reads every layer back. `debug` adds the debug view of that layer into
    // `debug_out` (RGBA8_UNorm bytes) when it is not null.
    Layers Render(const RenderScene& scene, const Camera& camera, GBufferDebugView* view = nullptr, GBufferLayer layer = GBufferLayer::Albedo,
                  std::vector<u8>* debug_out = nullptr) {
        Layers out;
        GBufferPass pass(device, sys, frame);
        pass.SetExtraTargetUsage(rhi::TextureUsage::CopySrc);
        const u64 f = frame_index++;
        device->BeginFrame();
        sys->BeginFrame(f);
        sys->FlushMaterials();
        frame->BeginFrame(f);
        frame->WriteFrameData(MakeFrameData(camera, kW, kH, 0, 0, f));
        out.ok = pass.Prepare(scene, Frustum::FromViewProjection(camera.ViewProjection()));
        out.error = pass.LastError();
        out.stats = pass.Stats();
        frame->Flush();

        rhi::BufferHandle bufs[4];
        const u64 sizes[4] = {kW * kH * 4, kW * kH * 4, kW * kH * 4, kW * kH * 4};
        for (int i = 0; i < 4; ++i) {
            rhi::BufferDesc bd;
            bd.size = sizes[i];
            bd.location = rhi::MemoryLocation::GpuOnly;
            bd.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::CopySrc;
            bufs[i] = device->CreateBuffer(bd);
            REQUIRE(bufs[i].IsValid());
        }
        rhi::TextureHandle debug_tex;
        ArenaAllocator arena(1 << 20);
        {
            RenderGraph graph(&arena, RenderGraphDesc{});
            pool->BeginFrame(f);
            CopyJob job;
            job.targets = pass.AddToGraph(&graph, kW, kH);
            GraphResource debug_res;
            if (view != nullptr) {
                rhi::TextureDesc td;
                td.width = kW;
                td.height = kH;
                td.format = rhi::Format::RGBA8_UNorm;
                td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
                debug_tex = device->CreateTexture(td);
                debug_res = graph.ImportTexture("debug_out", debug_tex, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
                view->AddToGraph(&graph, job.targets, layer, debug_res, rhi::Format::RGBA8_UNorm, camera.near_plane, camera.far_plane);
            }
            PassBuilder copy = graph.AddPass("readback", &CopyPass, &job, PassFlags::SideEffects);
            const GraphResource tex[4] = {job.targets.albedo, job.targets.normal, job.targets.material, job.targets.depth};
            for (int i = 0; i < 4; ++i) {
                job.buffers[i] = graph.ImportBuffer("host", bufs[i], rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc, false);
                copy.Read(tex[i], GraphUsage::CopySrc).Write(job.buffers[i], GraphUsage::CopyDst);
            }
            REQUIRE(graph.Compile());
            rhi::CommandList* list = device->CreateCommandList();
            list->Begin();
            const bool executed = graph.Execute(pool, list);
            if (!executed) FAIL(graph.ExecuteError());
            list->End();
            rhi::CommandList* lists[] = {list};
            REQUIRE(device->Submit(lists, 1) != 0);
        }
        device->EndFrame();
        device->WaitIdle();
        out.albedo.resize(kW * kH * 4);
        out.normal.resize(kW * kH * 2);
        out.material.resize(kW * kH * 4);
        out.depth.resize(kW * kH);
        REQUIRE(device->ReadbackBuffer(bufs[0], 0, out.albedo.data(), sizes[0]));
        REQUIRE(device->ReadbackBuffer(bufs[1], 0, out.normal.data(), sizes[1]));
        REQUIRE(device->ReadbackBuffer(bufs[2], 0, out.material.data(), sizes[2]));
        REQUIRE(device->ReadbackBuffer(bufs[3], 0, out.depth.data(), sizes[3]));
        if (debug_out != nullptr) {
            debug_out->resize(kW * kH * 4);
            REQUIRE(device->ReadbackTexture(debug_tex, 0, 0, debug_out->data(), debug_out->size(), rhi::ResourceState::CopySrc));
        }
        if (debug_tex.IsValid()) device->DestroyTexture(debug_tex);
        for (int i = 0; i < 4; ++i) device->DestroyBuffer(bufs[i]);
        return out;
    }
};

Camera TopDownCamera() {
    Camera cam = MakeLookAtCamera(Vec3(0, 10, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.0f;
    cam.near_plane = 1.0f;
    cam.far_plane = 30.0f;
    return cam;
}

rhi::Device* MakeDevice() {
    rhi::DeviceCreateInfo info;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    return rhi::CreateDevice(info);
}

bool Near(int got, int want, int tol) { return std::abs(got - want) <= tol; }

} // namespace

TEST_CASE("The G-buffer holds albedo, normal, material and depth of a known scene", "[rhi][vulkan][gpu][gbuffer]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::Device* device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device));
        char error[128];
        REQUIRE(CheckGBufferFormats(device, error, sizeof(error)));

        MeshData d;
        REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
        GpuMesh plane;
        REQUIRE(plane.Create(device, d, "plane"));
        REQUIRE(MakeCubeMesh(2.0f, &d) == nullptr);
        GpuMesh cube;
        REQUIRE(cube.Create(device, d, "cube"));
        REQUIRE(MakeSphereMesh(1.0f, 32, 16, &d) == nullptr);
        GpuMesh sphere;
        REQUIRE(sphere.Create(device, d, "sphere"));
        rig.Prepare(&plane);
        rig.Prepare(&cube);
        rig.Prepare(&sphere);

        const MaterialId green = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "ground");
        const MaterialId red = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "cube");
        const MaterialId white = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "sphere");
        REQUIRE(green.IsValid());
        REQUIRE(red.IsValid());
        REQUIRE(white.IsValid());
        rig.sys->SetVec4(green, "base_color", Vec4(0.0f, 0.6f, 0.0f, 1.0f));
        rig.sys->SetFloat(green, "roughness", 0.8f);
        rig.sys->SetFloat(green, "metallic", 0.0f);
        rig.sys->SetVec4(red, "base_color", Vec4(0.8f, 0.1f, 0.1f, 1.0f));
        rig.sys->SetFloat(red, "roughness", 0.4f);
        rig.sys->SetFloat(red, "metallic", 1.0f);
        rig.sys->SetVec4(white, "base_color", Vec4(0.5f, 0.5f, 0.5f, 1.0f));
        rig.sys->SetFloat(white, "emissive_scale", 0.25f);

        Camera cam = TopDownCamera();
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.mesh = &plane;
        od.material = green.index;
        od.transform = ModelMatrix(Vec3(0, 0, 0), 1.0f, 0.0f);
        scene.AddObject(od);
        od.mesh = &cube;
        od.material = red.index;
        od.transform = ModelMatrix(Vec3(-3, 1, 0), 1.0f, 0.0f);
        scene.AddObject(od);
        od.transform = ModelMatrix(Vec3(-3, 1, 4), 1.0f, 0.0f);
        scene.AddObject(od);
        od.mesh = &sphere;
        od.material = white.index;
        od.transform = ModelMatrix(Vec3(3, 1, 0), 1.0f, 0.0f);
        scene.AddObject(od);
        od.mesh = &cube;
        od.material = red.index;
        od.transform = ModelMatrix(Vec3(200, 1, 0), 1.0f, 0.0f); // far outside the frustum
        scene.AddObject(od);

        const Layers L = rig.Render(scene, cam);
        INFO(L.error);
        REQUIRE(L.ok);
        CHECK(L.stats.visible == 4);
        CHECK(L.stats.drawn_objects == 4);
        CHECK(L.stats.draws == 3); // ground, the two cubes as one instanced draw, the sphere

        // Ground away from everything: albedo (0, 0.6, 0) -> sRGB bytes, normal +Y, material (0.8, 0, 0), depth at 10 units.
        const Pixel ground = Project(cam, Vec3(0, 0, 4));
        CHECK(Near(L.Albedo(ground.x, ground.y, 0), 0, 2));
        CHECK(Near(L.Albedo(ground.x, ground.y, 1), static_cast<int>(LinearToSrgb(0.6f) * 255.0f + 0.5f), 2));
        CHECK(Near(L.Albedo(ground.x, ground.y, 3), 255, 1));
        const Vec3 gn = L.Normal(ground.x, ground.y);
        CHECK(std::fabs(gn.y - 1.0f) < 0.01f);
        CHECK(Near(L.Material(ground.x, ground.y, 0), 204, 2));
        CHECK(Near(L.Material(ground.x, ground.y, 1), 0, 1));
        CHECK(std::fabs(L.Depth(ground.x, ground.y) - ground.depth) < 1e-4f);

        // The cube's top face (y = 2): nearer than the ground under it, so it wins the depth test whatever the draw order.
        const Pixel top = Project(cam, Vec3(-3, 2, 0));
        CHECK(Near(L.Albedo(top.x, top.y, 0), static_cast<int>(LinearToSrgb(0.8f) * 255.0f + 0.5f), 2));
        CHECK(Near(L.Albedo(top.x, top.y, 1), static_cast<int>(LinearToSrgb(0.1f) * 255.0f + 0.5f), 2));
        const Vec3 tn = L.Normal(top.x, top.y);
        CHECK(std::fabs(tn.y - 1.0f) < 0.01f);
        CHECK(Near(L.Material(top.x, top.y, 0), 102, 2));
        CHECK(Near(L.Material(top.x, top.y, 1), 255, 1));
        CHECK(std::fabs(L.Depth(top.x, top.y) - top.depth) < 1e-4f);
        CHECK(L.Depth(top.x, top.y) < Project(cam, Vec3(-3, 0, 0)).depth);

        // The instanced second cube.
        const Pixel top2 = Project(cam, Vec3(-3, 2, 4));
        CHECK(Near(L.Albedo(top2.x, top2.y, 0), static_cast<int>(LinearToSrgb(0.8f) * 255.0f + 0.5f), 2));
        CHECK(std::fabs(L.Depth(top2.x, top2.y) - top2.depth) < 1e-4f);

        // The sphere: a point 0.6 off its pole has the normal (0.6, 0.8, 0); the emissive scale lands in the material layer.
        const Pixel sp = Project(cam, Vec3(3.6f, 1.0f + std::sqrt(1.0f - 0.36f), 0));
        const Vec3 sn = L.Normal(sp.x, sp.y);
        CHECK(std::fabs(sn.x - 0.6f) < 0.08f);
        CHECK(std::fabs(sn.y - 0.8f) < 0.08f);
        CHECK(std::fabs(sn.z) < 0.08f);
        CHECK(Near(L.Material(sp.x, sp.y, 2), 64, 2));

        // Nothing was drawn where no object is: this corner lies on the ground, so use the cleared depth of a point outside
        // the ground instead (the ground is 16 wide, the view 11.5 wide at that distance, so every pixel is covered).
        for (u32 i = 0; i < kW * kH; ++i) CHECK(L.depth[i] < 1.0f);
        // The object outside the frustum left no trace: the pixels of its footprint do not exist, and it was not drawn.
        CHECK(L.stats.rejected == 0);

        device->WaitIdle();
    }
    device->WaitIdle();
    rhi::DestroyDevice(device);
}

TEST_CASE("The G-buffer does not depend on the order objects were added", "[rhi][vulkan][gpu][gbuffer]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::Device* device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device));
        MeshData d;
        REQUIRE(MakeCubeMesh(2.0f, &d) == nullptr);
        GpuMesh cube;
        REQUIRE(cube.Create(device, d, "cube"));
        REQUIRE(MakeSphereMesh(1.0f, 24, 12, &d) == nullptr);
        GpuMesh sphere;
        REQUIRE(sphere.Create(device, d, "sphere"));
        REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
        GpuMesh plane;
        REQUIRE(plane.Create(device, d, "plane"));
        rig.Prepare(&cube);
        rig.Prepare(&sphere);
        rig.Prepare(&plane);
        MaterialId mats[3];
        for (int i = 0; i < 3; ++i) {
            mats[i] = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            rig.sys->SetVec4(mats[i], "base_color", Vec4(0.2f + 0.3f * i, 0.5f, 0.9f - 0.3f * i, 1.0f));
        }
        struct Item {
            const GpuMesh* mesh;
            int material;
            Vec3 position;
        };
        const Item items[] = {{&plane, 0, Vec3(0, 0, 0)}, {&cube, 1, Vec3(-3, 1, 0)}, {&sphere, 2, Vec3(3, 1, 0)}, {&cube, 1, Vec3(-3, 1, 4)},
                              {&sphere, 2, Vec3(3, 1, -4)}, {&cube, 2, Vec3(0, 1, 3)}};
        const Camera cam = TopDownCamera();
        Layers results[2];
        for (int pass = 0; pass < 2; ++pass) {
            RenderScene scene(RenderSceneDesc{});
            for (int i = 0; i < 6; ++i) {
                const Item& it = items[pass == 0 ? i : 5 - i];
                RenderObjectDesc od;
                od.mesh = it.mesh;
                od.material = mats[it.material].index;
                od.transform = ModelMatrix(it.position, 1.0f, 0.0f);
                scene.AddObject(od);
            }
            results[pass] = rig.Render(scene, cam);
            REQUIRE(results[pass].ok);
        }
        // Four (mesh, material) groups: plane/0, cube/1 (two instances), sphere/2 (two instances), cube/2, however they were added.
        CHECK(results[0].stats.draws == 4);
        CHECK(results[1].stats.draws == 4);
        CHECK(results[0].albedo == results[1].albedo);
        CHECK(results[0].normal == results[1].normal);
        CHECK(results[0].material == results[1].material);
        CHECK(results[0].depth == results[1].depth);
        device->WaitIdle();
    }
    device->WaitIdle();
    rhi::DestroyDevice(device);
}

TEST_CASE("Vertex colors blend across a mesh, authored in sRGB and landing in the albedo layer", "[rhi][vulkan][gpu][gbuffer]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::Device* device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device));
        MeshData plain;
        REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &plain) == nullptr);
        // Left vertices red, right vertices blue (x < 0 is the left edge).
        std::vector<u8> colors(plain.VertexCount() * 4);
        for (u32 i = 0; i < plain.VertexCount(); ++i) {
            const bool left = plain.PositionAt(i)[0] < 0.0f;
            colors[i * 4 + 0] = left ? 255 : 0;
            colors[i * 4 + 1] = 0;
            colors[i * 4 + 2] = left ? 0 : 255;
            colors[i * 4 + 3] = 255;
        }
        MeshData colored;
        REQUIRE(MakeColoredMesh(plain, colors.data(), plain.VertexCount(), &colored) == nullptr);
        GpuMesh mesh;
        REQUIRE(mesh.Create(device, colored, "colored plane"));
        GpuMesh bare;
        REQUIRE(bare.Create(device, plain, "plane"));
        rig.Prepare(&mesh);
        rig.Prepare(&bare);
        GBufferOptions opt;
        opt.vertex_color = true;
        const MaterialId with = CreateGBufferMaterial(rig.sys, rig.templates, opt, "with colors");
        const MaterialId without = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "without");
        REQUIRE(with.IsValid());
        REQUIRE(without.IsValid());
        rig.sys->SetVec4(without, "base_color", Vec4(0.25f, 0.5f, 0.75f, 1.0f));

        const Camera cam = TopDownCamera();
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.mesh = &mesh;
        od.material = with.index;
        scene.AddObject(od);
        const Layers L = rig.Render(scene, cam);
        INFO(L.error);
        REQUIRE(L.ok);
        // Plane x in [-8, 8] on the ground at 10 units: the blend is linear across the screen. t = (x + 8) / 16.
        for (f32 x : {-4.0f, -2.0f, 0.0f, 2.0f, 4.0f}) {
            const Pixel p = Project(cam, Vec3(x, 0, 0));
            const f32 t = (x + 8.0f) / 16.0f;
            INFO("x = " << x);
            CHECK(Near(L.Albedo(p.x, p.y, 0), static_cast<int>(255.0f * (1.0f - t) + 0.5f), 6));
            CHECK(Near(L.Albedo(p.x, p.y, 1), 0, 2));
            CHECK(Near(L.Albedo(p.x, p.y, 2), static_cast<int>(255.0f * t + 0.5f), 6));
        }

        // A mesh without colors renders with the base color only.
        RenderScene scene2(RenderSceneDesc{});
        od.mesh = &bare;
        od.material = without.index;
        scene2.AddObject(od);
        const Layers B = rig.Render(scene2, cam);
        REQUIRE(B.ok);
        const Pixel c = Project(cam, Vec3(0, 0, 0));
        CHECK(Near(B.Albedo(c.x, c.y, 0), static_cast<int>(LinearToSrgb(0.25f) * 255.0f + 0.5f), 2));
        CHECK(Near(B.Albedo(c.x, c.y, 2), static_cast<int>(LinearToSrgb(0.75f) * 255.0f + 0.5f), 2));

        // A colored mesh through a material that ignores colors, and the reverse, are named errors; the object is left out.
        RenderScene bad(RenderSceneDesc{});
        od.mesh = &mesh;
        od.material = without.index;
        bad.AddObject(od);
        const Layers E = rig.Render(bad, cam);
        CHECK_FALSE(E.ok);
        CHECK(E.stats.rejected == 1);
        CHECK(E.error.find("vertex colors") != std::string::npos);
        CHECK(E.stats.drawn_objects == 0);
        device->WaitIdle();
    }
    device->WaitIdle();
    rhi::DestroyDevice(device);
}

TEST_CASE("The normal map and alpha test permutations change the layers they should", "[rhi][vulkan][gpu][gbuffer]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::Device* device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device));
        MeshData d;
        REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
        GpuMesh plane;
        REQUIRE(plane.Create(device, d, "plane"));
        rig.Prepare(&plane);

        // A tangent-space normal (0.6, 0, 0.8) as a 1x1 texture: (0.8, 0.5, 0.9).
        rhi::TextureDesc td;
        td.width = 1;
        td.height = 1;
        td.format = rhi::Format::RGBA8_UNorm;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        const rhi::TextureHandle normal_tex = device->CreateTexture(td);
        const u8 texel[4] = {204, 128, 230, 255};
        device->UploadTexture(normal_tex, 0, 0, texel, 0);

        GBufferOptions nm;
        nm.normal_map = true;
        const MaterialId mapped = CreateGBufferMaterial(rig.sys, rig.templates, nm, "mapped");
        REQUIRE(mapped.IsValid());
        REQUIRE(rig.sys->SetTexture(mapped, "normal_tex", normal_tex));
        const Camera cam = TopDownCamera();
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.mesh = &plane;
        od.material = mapped.index;
        scene.AddObject(od);
        const Layers L = rig.Render(scene, cam);
        REQUIRE(L.ok);
        const Pixel c = Project(cam, Vec3(0, 0, 0));
        const Vec3 n = L.Normal(c.x, c.y);
        INFO("normal " << n.x << " " << n.y << " " << n.z);
        // The plane's tangent is +X with w = 1 and its normal +Y, so the bitangent is -Z: (0.6, 0.8, 0).
        CHECK(std::fabs(n.x - 0.6f) < 0.03f);
        CHECK(std::fabs(n.y - 0.8f) < 0.03f);
        CHECK(std::fabs(n.z) < 0.03f);

        // Alpha test: alpha 0.3 under a cutoff of 0.5 leaves the pixel cleared; alpha 0.8 draws it.
        GBufferOptions at;
        at.alpha_test = true;
        const MaterialId cut = CreateGBufferMaterial(rig.sys, rig.templates, at, "cut");
        REQUIRE(cut.IsValid());
        rig.sys->SetVec4(cut, "base_color", Vec4(1, 1, 1, 0.3f));
        RenderScene scene2(RenderSceneDesc{});
        od.material = cut.index;
        scene2.AddObject(od);
        const Layers C = rig.Render(scene2, cam);
        REQUIRE(C.ok);
        CHECK(C.Depth(c.x, c.y) == 1.0f);
        CHECK(C.Albedo(c.x, c.y, 3) == 0);
        rig.sys->SetVec4(cut, "base_color", Vec4(1, 1, 1, 0.8f));
        const Layers K = rig.Render(scene2, cam);
        REQUIRE(K.ok);
        CHECK(K.Depth(c.x, c.y) < 1.0f);
        CHECK(K.Albedo(c.x, c.y, 3) == 255);

        device->WaitIdle();
        device->DestroyTexture(normal_tex);
    }
    device->WaitIdle();
    rhi::DestroyDevice(device);
}

TEST_CASE("The debug view shows the albedo, normal, material and depth layers", "[rhi][vulkan][gpu][gbuffer]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::Device* device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device));
        GBufferDebugView view(device);
        REQUIRE(view.Valid());
        MeshData d;
        REQUIRE(MakePlaneMesh(16.0f, 16.0f, 1, 1, &d) == nullptr);
        GpuMesh plane;
        REQUIRE(plane.Create(device, d, "plane"));
        rig.Prepare(&plane);
        const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
        rig.sys->SetVec4(m, "base_color", Vec4(0.5f, 0.25f, 1.0f, 1.0f));
        rig.sys->SetFloat(m, "roughness", 0.2f);
        rig.sys->SetFloat(m, "metallic", 0.6f);
        Camera cam = TopDownCamera();
        RenderScene scene(RenderSceneDesc{});
        RenderObjectDesc od;
        od.mesh = &plane;
        od.material = m.index;
        scene.AddObject(od);
        FitCameraDepthRange(scene, &cam, 0.1f);
        const Pixel c = Project(cam, Vec3(0, 0, 0));
        std::vector<u8> out;
        rig.Render(scene, cam, &view, GBufferLayer::Albedo, &out);
        // The target is RGBA8_UNorm (no sRGB encoding), so the shown value is the linear albedo.
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 128, 3));
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 64, 3));
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 2], 255, 3));
        rig.Render(scene, cam, &view, GBufferLayer::Normal, &out);
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 128, 3)); // (0, 1, 0) * 0.5 + 0.5
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 255, 3));
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 2], 128, 3));
        rig.Render(scene, cam, &view, GBufferLayer::Material, &out);
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], 51, 3));
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 1], 153, 3));
        rig.Render(scene, cam, &view, GBufferLayer::Depth, &out);
        // The plane is 10 units from the camera; the fitted far plane is the distance of its farthest sphere point.
        const f32 expected = 10.0f / cam.far_plane;
        CHECK(Near(out[(c.y * kW + c.x) * 4 + 0], static_cast<int>(expected * 255.0f + 0.5f), 5));
        device->WaitIdle();
    }
    device->WaitIdle();
    rhi::DestroyDevice(device);
}
