#pragma once
// Shared by the G-buffer and lighting real-device tests: a device-side rig (materials, frame resources, transient pool,
// G-buffer templates), the layer readback of one G-buffer frame, and small helpers (half floats, projection of a point).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

namespace gbuffer_rig {

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;



constexpr u32 kW = 64;
constexpr u32 kH = 64;

inline f32 HalfToFloat(u16 h) {
    const u32 sign = (h >> 15) & 1u;
    const u32 exp = (h >> 10) & 0x1fu;
    const u32 man = h & 0x3ffu;
    f32 v;
    if (exp == 0) v = std::ldexp(static_cast<f32>(man), -24);
    else if (exp == 31) v = man ? NAN : INFINITY;
    else v = std::ldexp(static_cast<f32>(man | 0x400u), static_cast<int>(exp) - 25);
    return sign ? -v : v;
}

inline f32 LinearToSrgb(f32 c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

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

inline Pixel Project(const Camera& cam, const Vec3& p) {
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

inline Camera TopDownCamera() {
    Camera cam = MakeLookAtCamera(Vec3(0, 10, 0), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.0f;
    cam.near_plane = 1.0f;
    cam.far_plane = 30.0f;
    return cam;
}

inline rhi::Device* MakeDevice() {
    rhi::DeviceCreateInfo info;
    info.validation = rhi::ValidationSetting::On;
    info.sync_validation = true;
    info.pipeline_cache = false;
    return rhi::CreateDevice(info);
}

inline bool Near(int got, int want, int tol) { return std::abs(got - want) <= tol; }


} // namespace gbuffer_rig
