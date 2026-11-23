// The procedural sky on a real device: a ground plane under a horizontal camera is lit, the sky pass draws the rest of
// the light accumulation target, and probe pixels are compared with the CPU model of the sky shader (SkyColor).
#include "lang/scalar.h"
#include "log/output.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/span.h"
#include "gbuffer_rig.h"
#include "renderer/lighting.h"
#include "containers/shared_bytes.h"
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

namespace
{

// A cube map of 16 x 16 texels a face with one mip, made from `bytes` (six faces, tightly packed). Face f, texel (i, j)
// is at ((f * 16 + j) * 16 + i) * texel_bytes.
GpuTexturePtr SkyMakeCube(rhi::Device* device, rhi::Format format, u32 texel_bytes, Vector<byte>&& bytes)
{
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::Cube;
    desc.width = 16;
    desc.height = 16;
    desc.array_layers = 6;
    desc.format = format;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<renderer::TextureLevelSpan> levels;
    for (u32 face = 0; face < 6; ++face)
    {
        renderer::TextureLevelSpan span;
        span.offset = static_cast<u64>(face) * 16 * 16 * texel_bytes;
        span.bytes = 16 * 16 * texel_bytes;
        span.row_pitch = 16 * texel_bytes;
        span.mip = 0;
        span.layer = face;
        levels.Push(span);
    }
    GpuTexturePtr cube = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    REQUIRE(cube->CreateFromSharedLevels(device, desc, region,
                                         vse::Span<const renderer::TextureLevelSpan>(levels.Data(), levels.Size()),
                                         nullptr, "sky cube"));
    return cube;
}

// Face f texel (i, j): red from the column, green from the row, blue from the face.
GpuTexturePtr SkyGradientCube(rhi::Device* device)
{
    Vector<byte> bytes;
    bytes.Resize(6 * 16 * 16 * 4);
    for (u32 f = 0; f < 6; ++f)
    {
        for (u32 j = 0; j < 16; ++j)
        {
            for (u32 i = 0; i < 16; ++i)
            {
                byte* t = bytes.Data() + ((f * 16 + j) * 16 + i) * 4;
                t[0] = static_cast<byte>(i * 16 + 8);
                t[1] = static_cast<byte>(j * 16 + 8);
                t[2] = static_cast<byte>(f * 40);
                t[3] = 255;
            }
        }
    }
    return SkyMakeCube(device, rhi::Format::RGBA8_UNorm, 4, Move(bytes));
}

// The convention of the three APIs, as the minter's asset::CubeFaceDirection states it: the direction of face texel
// (u, v), each -1 to 1; inverted here for a direction to find the face and (u, v).
bool SkyDirectionToFace(const Vec3& d, u32* face, f32* u, f32* v)
{
    const f32 ax = Abs(d.x);
    const f32 ay = Abs(d.y);
    const f32 az = Abs(d.z);
    const f32 m = ax > ay ? (ax > az ? ax : az) : (ay > az ? ay : az);
    const f32 second = ax > ay ? (ay > az ? ay : (ax > az ? az : ax)) : (ax > az ? ax : (ay > az ? az : ay));
    if ((m - second) / m < 0.08f)
    {
        return false;  // near a seam: the linear filter blends two faces
    }
    if (ax == m)
    {
        *face = d.x > 0 ? 0 : 1;
        *u = d.x > 0 ? -d.z / m : d.z / m;
        *v = -d.y / m;
    }
    else if (ay == m)
    {
        *face = d.y > 0 ? 2 : 3;
        *u = d.x / m;
        *v = d.y > 0 ? d.z / m : -d.z / m;
    }
    else
    {
        *face = d.z > 0 ? 4 : 5;
        *u = d.z > 0 ? d.x / m : -d.x / m;
        *v = -d.y / m;
    }
    return Abs(*u) < 0.85f && Abs(*v) < 0.85f;  // away from the face's edge texels
}

Camera SkyLookCamera(const Vec3& direction)
{
    const bool vertical = Abs(direction.y) > 0.9f;
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), direction, vertical ? Vec3(0, 0, -1) : Vec3(0, 1, 0));
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;
    return cam;
}

}  // namespace

TEST_CASE("A cube map sky shows each face in the right place and orientation", "[rhi][gpu][sky]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        if (!device->Caps().cube_textures)
        {
            continue;
        }
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            RenderScene scene(RenderSceneDesc{});
            SkySettings sky;
            sky.mode = SkyMode::Cube;
            sky.cube = SkyGradientCube(device.Get());
            const Vec3 looks[6] = {Vec3(1, 0.1f, 0.07f),  Vec3(-1, 0.1f, 0.07f), Vec3(0.1f, 1, 0.07f),
                                   Vec3(0.1f, -1, 0.07f), Vec3(0.1f, 0.07f, 1),  Vec3(0.1f, 0.07f, -1)};
            u32 checked = 0;
            for (u32 look = 0; look < 6; ++look)
            {
                const Camera cam = SkyLookCamera(looks[look].Normalized());
                const SkyFrame frame = RenderSky(rig, scene, cam, sky);
                REQUIRE(frame.ok);
                REQUIRE(frame.sky_added);
                for (u32 y = 8; y < kH; y += 24)
                {
                    for (u32 x = 8; x < kW; x += 24)
                    {
                        u32 face = 0;
                        f32 u = 0;
                        f32 v = 0;
                        if (!SkyDirectionToFace(SkyCubeDirection(sky, PixelRay(cam, x, y)), &face, &u, &v))
                        {
                            continue;
                        }
                        INFO("look " << look << " pixel " << x << " " << y << " face " << face);
                        CHECK(Abs(frame.Accum(x, y, 0) - (u * 0.5f + 0.5f)) < 0.03f);
                        CHECK(Abs(frame.Accum(x, y, 1) - (v * 0.5f + 0.5f)) < 0.03f);
                        CHECK(Abs(frame.Accum(x, y, 2) - static_cast<f32>(face * 40) / 255.0f) < 0.01f);
                        ++checked;
                    }
                }
            }
            CHECK(checked >= 30);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A cube sky turns with its rotation, takes tint and exposure, draws HDR and LDR cubes, and falls back",
          "[rhi][gpu][sky]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        rhi::DevicePtr device = MakeDevice(backend);
        if (!device->Caps().cube_textures)
        {
            continue;
        }
        {
            Rig rig;
            REQUIRE(rig.Create(device.Get()));
            RenderScene scene(RenderSceneDesc{});
            const Camera cam = SkyLookCamera(Vec3(0, 0, -1));

            // Rotation: looking along -Z with a quarter turn shows the +X face (blue 0); without it the -Z face (200).
            SkySettings sky;
            sky.mode = SkyMode::Cube;
            sky.cube = SkyGradientCube(device.Get());
            SkyFrame frame = RenderSky(rig, scene, cam, sky);
            REQUIRE(frame.ok);
            CHECK(Abs(frame.Accum(32, 32, 2) - 200.0f / 255.0f) < 0.01f);
            sky.rotation = 1.57079632679f;
            frame = RenderSky(rig, scene, cam, sky);
            CHECK(Abs(frame.Accum(32, 32, 2) - 0.0f) < 0.01f);
            sky.rotation = -1.57079632679f;  // the other way: the -X face (40)
            frame = RenderSky(rig, scene, cam, sky);
            CHECK(Abs(frame.Accum(32, 32, 2) - 40.0f / 255.0f) < 0.01f);

            // HDR: every texel (2, 1, 0.5); tint and exposure multiply it.
            Vector<byte> hdr;
            hdr.Resize(6 * 16 * 16 * 8);
            for (usize i = 0; i < 6 * 16 * 16; ++i)
            {
                u16* t = reinterpret_cast<u16*>(hdr.Data() + i * 8);
                t[0] = 0x4000;  // 2.0
                t[1] = 0x3C00;  // 1.0
                t[2] = 0x3800;  // 0.5
                t[3] = 0x3C00;
            }
            SkySettings bright;
            bright.mode = SkyMode::Cube;
            bright.cube = SkyMakeCube(device.Get(), rhi::Format::RGBA16_Float, 8, Move(hdr));
            bright.tint = Vec3(0.5f, 1.0f, 2.0f);
            bright.exposure = 1.5f;
            frame = RenderSky(rig, scene, cam, bright);
            CHECK(Close(frame.Accum(32, 32, 0), 1.5f));
            CHECK(Close(frame.Accum(32, 32, 1), 1.5f));
            CHECK(Close(frame.Accum(32, 32, 2), 1.5f));
            bright.tint = Vec3(1, 1, 1);
            bright.exposure = 1.0f;
            frame = RenderSky(rig, scene, cam, bright);
            CHECK(Close(frame.Accum(32, 32, 0), 2.0f));
            CHECK(Close(frame.Accum(5, 60, 2), 0.5f));

            // LDR sRGB: a texel of 128 is 0.2158 linear.
            Vector<byte> ldr;
            ldr.Resize(6 * 16 * 16 * 4);
            for (usize i = 0; i < ldr.Size(); ++i)
            {
                ldr[i] = 128;
            }
            SkySettings dim;
            dim.mode = SkyMode::Cube;
            dim.cube = SkyMakeCube(device.Get(), rhi::Format::RGBA8_SRGB, 4, Move(ldr));
            frame = RenderSky(rig, scene, cam, dim);
            CHECK(Close(frame.Accum(32, 32, 0), 0.2158f));

            // No cube: the procedural sky is drawn, as it would be for the mode Procedural.
            SkySettings none;
            none.mode = SkyMode::Cube;
            frame = RenderSky(rig, scene, cam, none);
            SkySettings procedural = none;
            procedural.mode = SkyMode::Procedural;
            const Vec3 want = SkyColor(procedural, PixelRay(cam, 10, 10));
            CHECK(Close(frame.Accum(10, 10, 0), want.x));
            CHECK(Close(frame.Accum(10, 10, 1), want.y));
            CHECK(Close(frame.Accum(10, 10, 2), want.z));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
