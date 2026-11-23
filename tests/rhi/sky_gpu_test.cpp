// The procedural sky on a real device: a ground plane under a horizontal camera is lit, the sky pass draws the rest of
// the light accumulation target, and probe pixels are compared with the CPU model of the sky shader (SkyColor).
#include "lang/scalar.h"
#include "log/output.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/span.h"
#include "gbuffer_rig.h"
#include "renderer/lighting.h"
#include "renderer/sky.h"

using namespace vse;
using namespace vse::renderer;
using namespace gbuffer_rig;

namespace
{

struct SkyFrame
{
    Vector<u16> accum;  // RGBA16_Float
    bool ok = false;
    bool sky_added = false;
    f32 Accum(u32 x, u32 y, u32 c) const { return HalfToFloat(accum[(y * kW + x) * 4 + c]); }
};

struct SkyCopyAccum
{
    GraphResource accum;
    GraphResource buffer;
};

void SkyCopyAccumPass(const PassExecuteContext& ctx, void* user)
{
    const SkyCopyAccum* job = static_cast<const SkyCopyAccum*>(user);
    ctx.list->CopyTextureToBuffer(ctx.graph->ResolvedTexture(job->accum), ctx.graph->ImportedBuffer(job->buffer),
                                  rhi::BufferTextureCopy{});
}

Camera HorizontalCamera()
{
    Camera cam = MakeLookAtCamera(Vec3(0, 1.5f, 0), Vec3(0, 1.7f, -1), Vec3(0, 1, 0));
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;
    return cam;
}

// The world-space direction of the ray through the centre of a pixel.
Vec3 PixelRay(const Camera& cam, u32 x, u32 y)
{
    const FrameData fd = MakeFrameData(cam, kW, kH, 0, 0, 0);
    const Vec3 far_point = ReconstructWorldPosition(x, y, 1.0f, kW, kH, fd.inverse_view_projection);
    return far_point - cam.position;
}

SkyFrame RenderSky(Rig& rig, const RenderScene& scene, const Camera& camera, const SkySettings& sky)
{
    SkyFrame out;
    GBufferPass gbuffer(rig.device, rig.sys, rig.frame);
    LightingPass lighting(rig.device, rig.sys, rig.frame);
    SkyPass sky_pass(rig.device, rig.sys, rig.frame);
    REQUIRE(lighting.Valid());
    REQUIRE(sky_pass.Valid());
    const u64 f = rig.frame_index++;
    rig.device->BeginFrame();
    rig.sys->BeginFrame(f);
    rig.sys->FlushMaterials();
    rig.frame->BeginFrame(f);
    const FrameData frame_data = MakeFrameData(camera, kW, kH, 0, 0, f);
    rig.frame->WriteFrameData(frame_data);
    out.ok = gbuffer.Prepare(scene, Frustum::FromViewProjection(camera.ViewProjection()));
    lighting.Prepare(scene, frame_data);
    sky_pass.Prepare(sky);
    rig.frame->Flush();

    rhi::BufferDesc bd;
    bd.size = kW * kH * 8;
    bd.location = rhi::MemoryLocation::GpuOnly;
    bd.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::CopySrc;
    const rhi::BufferHandle accum_buf = rig.device->CreateBuffer(bd);
    REQUIRE(accum_buf.IsValid());

    ArenaAllocator arena(1 << 20);
    {
        RenderGraph graph(&arena, RenderGraphDesc{});
        rig.pool->BeginFrame(f);
        const GBufferTargets targets = gbuffer.AddToGraph(&graph, kW, kH);
        const GraphResource accum = lighting.AddToGraph(&graph, targets, kW, kH);
        out.sky_added = sky_pass.AddToGraph(&graph, targets, accum);
        SkyCopyAccum job;
        job.accum = accum;
        job.buffer = graph.ImportBuffer("accum host", accum_buf, rhi::ResourceState::Undefined,
                                        rhi::ResourceState::CopySrc, false);
        graph.AddPass("readback accum", &SkyCopyAccumPass, &job, PassFlags::SideEffects)
            .Read(accum, GraphUsage::CopySrc)
            .Write(job.buffer, GraphUsage::CopyDst);
        REQUIRE(graph.Compile());
        rhi::CommandList* list = rig.device->CreateCommandList();
        list->Begin();
        const bool executed = graph.Execute(rig.pool, list);
        if (!executed)
        {
            FAIL(graph.ExecuteError());
        }
        list->End();
        rhi::CommandList* lists[] = {list};
        REQUIRE(rig.device->Submit(lists, 1) != 0);
    }
    rig.device->EndFrame();
    rig.device->WaitIdle();
    out.accum.Resize(kW * kH * 4);
    REQUIRE(rig.device->ReadbackBuffer(accum_buf, 0, out.accum.Data(), kW * kH * 8));
    rig.device->DestroyBuffer(accum_buf);
    return out;
}

bool SkyClose(f32 got, f32 want)
{
    return Abs(got - want) <= 0.004f + 0.02f * Abs(want);
}

}  // namespace

TEST_CASE("The procedural sky matches its CPU model, and covered pixels are not drawn over", "[rhi][gpu][sky]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            MeshData d;
            REQUIRE(MakePlaneMesh(40.0f, 40.0f, 1, 1, &d) == nullptr);
            GpuMesh plane;
            REQUIRE(plane.Create(device.Get(), d, "ground"));
            rig.Prepare(&plane);
            const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
            rig.sys->SetVec4(m, "base_color", Vec4(0.6f, 0.5f, 0.4f, 1.0f));
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = m.index;
            scene.AddObject(od);
            Light ambient;
            ambient.type = LightType::Ambient;
            ambient.color = Vec3(0.3f, 0.3f, 0.3f);
            scene.AddLight(ambient);

            const Camera cam = HorizontalCamera();
            SkySettings sky;
            sky.mode = SkyMode::Procedural;
            sky.sun_direction = PixelRay(cam, 20, 6);  // a ray the sun disc covers exactly
            REQUIRE(SkySettingsValid(sky));

            SkySettings flat;  // the same scene with a black flat sky: what the covered pixels hold without the pass
            const SkyFrame base = RenderSky(rig, scene, cam, flat);
            REQUIRE(base.ok);
            CHECK_FALSE(base.sky_added);
            const SkyFrame lit = RenderSky(rig, scene, cam, sky);
            REQUIRE(lit.ok);
            CHECK(lit.sky_added);

            // Sky pixels: the corners, the top and the sun, a row between the horizon and the plane's far edge, and a
            // row just above the horizon.
            const u32 probes[][2] = {{0, 0}, {63, 0}, {32, 2}, {20, 6}, {22, 6}, {10, 20}, {50, 22}, {32, 27}};
            for (const auto& p : probes)
            {
                const Vec3 want = SkyColor(sky, PixelRay(cam, p[0], p[1]));
                INFO("pixel " << p[0] << " " << p[1]);
                // The plane covers nothing here: the base render left it black.
                CHECK(base.Accum(p[0], p[1], 0) == 0.0f);
                for (u32 c = 0; c < 3; ++c)
                {
                    INFO("channel " << c << " want " << (c == 0 ? want.x : (c == 1 ? want.y : want.z)) << " got "
                                    << lit.Accum(p[0], p[1], c));
                    CHECK(SkyClose(lit.Accum(p[0], p[1], c), c == 0 ? want.x : (c == 1 ? want.y : want.z)));
                }
            }
            // The sun pixel is far brighter than the sky around it.
            CHECK(lit.Accum(20, 6, 0) > lit.Accum(32, 2, 0) + 4.0f);

            // Pixels the plane covers (below its far edge) are exactly what the flat render left.
            const u32 covered[][2] = {{32, 60}, {20, 52}, {44, 56}, {32, 48}};
            for (const auto& p : covered)
            {
                INFO("covered pixel " << p[0] << " " << p[1]);
                CHECK(base.Accum(p[0], p[1], 0) > 0.0f);
                for (u32 c = 0; c < 3; ++c)
                {
                    CHECK(lit.Accum(p[0], p[1], c) == base.Accum(p[0], p[1], c));
                }
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
