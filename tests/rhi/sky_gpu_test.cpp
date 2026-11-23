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

// What the lighting pass is given besides the scene: the environment cube and the two intensities.
struct SkyLighting
{
    GpuTexturePtr cube;
    f32 ambient = 0.0f;
    f32 reflection = 0.0f;
};

SkyFrame RenderSky(Rig& rig, const RenderScene& scene, const Camera& camera, const SkySettings& sky,
                   const SkyLighting& sky_lighting = SkyLighting{})
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
    lighting.SetSkyReflection(sky_lighting.reflection);
    lighting.SetEnvironmentCube(sky_lighting.cube, sky_lighting.ambient, sky.rotation);
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

namespace
{

// A cube of `mips` levels (4 texels a face at level 0) whose face f at level m is flat `colors[m * 6 + f]`.
GpuTexturePtr SkyColorCube(rhi::Device* device, u32 mips, const Vec3* colors)
{
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::Cube;
    desc.width = 4;
    desc.height = 4;
    desc.array_layers = 6;
    desc.mip_levels = mips;
    desc.format = rhi::Format::RGBA16_Float;
    desc.usage = rhi::TextureUsage::Sampled;
    Vector<byte> bytes;
    Vector<renderer::TextureLevelSpan> spans;
    for (u32 face = 0; face < 6; ++face)
    {
        for (u32 mip = 0; mip < mips; ++mip)
        {
            const u32 size = 4u >> mip;
            renderer::TextureLevelSpan span;
            span.offset = bytes.Size();
            span.bytes = size * size * 8;
            span.row_pitch = size * 8;
            span.mip = mip;
            span.layer = face;
            spans.Push(span);
            const Vec3 c = colors[mip * 6 + face];
            // Half floats of the values used here (multiples of 1/8 up to 8), by their bit patterns.
            auto half = [](f32 v) -> u16
            {
                if (v == 0.0f)
                {
                    return 0;
                }
                int exponent = 0;
                f32 m = v;
                while (m >= 2.0f)
                {
                    m *= 0.5f;
                    ++exponent;
                }
                while (m < 1.0f)
                {
                    m *= 2.0f;
                    --exponent;
                }
                return static_cast<u16>(((exponent + 15) << 10) | static_cast<u32>((m - 1.0f) * 1024.0f));
            };
            for (u32 i = 0; i < size * size; ++i)
            {
                const u16 t[4] = {half(c.x), half(c.y), half(c.z), 0x3C00};
                for (u32 k = 0; k < 4; ++k)
                {
                    bytes.Push(static_cast<byte>(t[k] & 0xff));
                    bytes.Push(static_cast<byte>(t[k] >> 8));
                }
            }
        }
    }
    GpuTexturePtr cube = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    REQUIRE(cube->CreateFromSharedLevels(device, desc, region,
                                         vse::Span<const renderer::TextureLevelSpan>(spans.Data(), spans.Size()),
                                         nullptr, "color cube"));
    return cube;
}

// A 40 x 40 plane at y = 0 with a grey-brown dielectric of the given roughness.
struct SkyLitPlane
{
    GpuMesh plane;
    RenderScene scene{RenderSceneDesc{}};
    SurfacePoint surface;
};

void SkyMakePlane(Rig& rig, SkyLitPlane* out, f32 roughness, bool flat_ambient)
{
    MeshData d;
    REQUIRE(MakePlaneMesh(40.0f, 40.0f, 1, 1, &d) == nullptr);
    REQUIRE(out->plane.Create(rig.device, d, "ground"));
    rig.Prepare(&out->plane);
    const MaterialId m = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
    rig.sys->SetVec4(m, "base_color", Vec4(0.6f, 0.5f, 0.4f, 1.0f));
    rig.sys->SetFloat(m, "roughness", roughness);
    rig.sys->SetFloat(m, "metallic", 0.0f);
    rig.sys->SetFloat(m, "occlusion", 1.0f);
    rig.sys->SetVec4(m, "emissive_color", Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    RenderObjectDesc od;
    od.mesh = &out->plane;
    od.material = m.index;
    out->scene.AddObject(od);
    if (flat_ambient)
    {
        Light ambient;
        ambient.type = LightType::Ambient;
        ambient.color = Vec3(0.5f, 0.5f, 0.5f);
        out->scene.AddLight(ambient);
    }
    out->surface.albedo = Vec3(0.6f, 0.5f, 0.4f);
    out->surface.roughness = roughness;
    out->surface.metallic = 0.0f;
    out->surface.occlusion = 1.0f;
    out->surface.normal = Vec3(0, 1, 0);
}

Vec3 SkyViewTo(const Camera& cam, u32 x, u32 y)
{
    // From the plane point under pixel (x, y) toward the eye: the ray hits y = 0.
    const Vec3 ray = PixelRay(cam, x, y);
    const f32 t = -cam.position.y / ray.y;
    return (cam.position - (cam.position + ray * t)).Normalized();
}

}  // namespace

TEST_CASE("The sky cube lights and is reflected by a surface, replacing the flat ambient light", "[rhi][gpu][sky]")
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
            SkyLitPlane lit;
            SkyMakePlane(rig, &lit, 0.5f, true);
            const Camera cam = HorizontalCamera();
            const Vec3 sky_color(2.0f, 1.0f, 0.5f);
            Vec3 flat[6];
            for (Vec3& c : flat)
            {
                c = sky_color;
            }
            SkyLighting sl;
            sl.cube = SkyColorCube(device.Get(), 1, flat);
            sl.ambient = 1.0f;
            sl.reflection = 1.0f;
            SkySettings none;
            none.mode = SkyMode::None;
            const SkyFrame frame = RenderSky(rig, lit.scene, cam, none, sl);
            REQUIRE(frame.ok);
            // Plane pixels under the horizon, near and far.
            const u32 pixels[3][2] = {{32, 60}, {20, 52}, {44, 56}};
            for (const auto& p : pixels)
            {
                const Vec3 v = SkyViewTo(cam, p[0], p[1]);
                const Vec3 want = ShadeAmbient(lit.surface, sky_color) +
                                  ShadeSkyReflection(lit.surface, v, sky_color, 1.0f);
                INFO("pixel " << p[0] << " " << p[1]);
                CHECK(SkyClose(frame.Accum(p[0], p[1], 0), want.x));
                CHECK(SkyClose(frame.Accum(p[0], p[1], 1), want.y));
                CHECK(SkyClose(frame.Accum(p[0], p[1], 2), want.z));
            }
            // With the sky's ambient intensity at 0 the flat ambient light (0.5) is back, and the reflection stays.
            sl.ambient = 0.0f;
            const SkyFrame flat_frame = RenderSky(rig, lit.scene, cam, none, sl);
            const Vec3 v = SkyViewTo(cam, 32, 60);
            const Vec3 want = ShadeAmbient(lit.surface, Vec3(0.5f, 0.5f, 0.5f)) +
                              ShadeSkyReflection(lit.surface, v, sky_color, 1.0f);
            CHECK(SkyClose(flat_frame.Accum(32, 60, 0), want.x));
            CHECK(SkyClose(flat_frame.Accum(32, 60, 2), want.z));
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("A smooth surface reflects the face of the sky cube its mirror direction hits", "[rhi][gpu][sky]")
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
            SkyLitPlane lit;
            SkyMakePlane(rig, &lit, 0.05f, false);
            const Vec3 faces[6] = {Vec3(4, 0, 0), Vec3(0, 4, 0), Vec3(0, 0, 4),
                                   Vec3(4, 4, 0), Vec3(0, 4, 4), Vec3(4, 0, 4)};
            SkyLighting sl;
            sl.cube = SkyColorCube(device.Get(), 1, faces);
            sl.reflection = 1.0f;
            SkySettings none;
            none.mode = SkyMode::None;
            // A camera 6 up looking along -Z and down: pixels give mirror directions toward -Z and up.
            Camera cam = MakeLookAtCamera(Vec3(0, 6, 0), Vec3(0, 0, -6), Vec3(0, 1, 0));
            cam.aspect = 1.0f;
            cam.near_plane = 0.1f;
            cam.far_plane = 100.0f;
            const SkyFrame frame = RenderSky(rig, lit.scene, cam, none, sl);
            REQUIRE(frame.ok);
            u32 faces_seen[6] = {};
            u32 checked = 0;
            for (u32 y = 8; y < kH; y += 8)
            {
                for (u32 x = 16; x < 56; x += 16)
                {
                    const Vec3 v = SkyViewTo(cam, x, y);
                    const Vec3 r = Vec3(-v.x, v.y, -v.z);  // reflect(-V, up): the horizontal parts flip, y stays
                    u32 face = 0;
                    f32 u = 0;
                    f32 w = 0;
                    if (!SkyDirectionToFace(r, &face, &u, &w) || Abs(u) > 0.7f || Abs(w) > 0.7f)
                    {  // a 4-texel face blends with its neighbour over its outer texel
                        continue;
                    }
                    const Vec3 want = ShadeSkyReflection(lit.surface, v, faces[face], 1.0f);
                    INFO("pixel " << x << " " << y << " face " << face << " got " << frame.Accum(x, y, 0) << " "
                                  << frame.Accum(x, y, 1) << " " << frame.Accum(x, y, 2) << " want " << want.x << " "
                                  << want.y << " " << want.z);
                    CHECK(SkyClose(frame.Accum(x, y, 0), want.x));
                    CHECK(SkyClose(frame.Accum(x, y, 1), want.y));
                    CHECK(SkyClose(frame.Accum(x, y, 2), want.z));
                    ++faces_seen[face];
                    ++checked;
                }
            }
            CHECK(checked >= 6);
            u32 distinct = 0;
            for (u32 n : faces_seen)
            {
                distinct += n > 0 ? 1 : 0;
            }
            CHECK(distinct >= 2);  // the view sees more than one face
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("Roughness picks the mip of the sky cube a surface reflects", "[rhi][gpu][sky]")
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
            // Three mips; every face of mip m is the same colour: red, green, blue.
            Vec3 chain[18];
            for (u32 face = 0; face < 6; ++face)
            {
                chain[face] = Vec3(4, 0, 0);
                chain[6 + face] = Vec3(0, 4, 0);
                chain[12 + face] = Vec3(0, 0, 4);
            }
            const GpuTexturePtr cube = SkyColorCube(device.Get(), 3, chain);
            const Camera cam = HorizontalCamera();
            SkySettings none;
            none.mode = SkyMode::None;
            const f32 roughness[3] = {0.0f, 0.5f, 1.0f};
            const Vec3 expect[3] = {Vec3(4, 0, 0), Vec3(0, 4, 0), Vec3(0, 0, 4)};
            for (u32 i = 0; i < 3; ++i)
            {
                SkyLitPlane lit;
                SkyMakePlane(rig, &lit, roughness[i], false);
                SkyLighting sl;
                sl.cube = cube;
                sl.reflection = 1.0f;
                const SkyFrame frame = RenderSky(rig, lit.scene, cam, none, sl);
                REQUIRE(frame.ok);
                const Vec3 v = SkyViewTo(cam, 32, 60);
                const Vec3 want = ShadeSkyReflection(lit.surface, v, expect[i], 1.0f);
                INFO("roughness " << roughness[i] << " got " << frame.Accum(32, 60, 0) << " " << frame.Accum(32, 60, 1)
                                  << " " << frame.Accum(32, 60, 2) << " want " << want.x << " " << want.y << " "
                                  << want.z);
                // The roughness is stored in a byte (127/255 for 0.5), so the level is within 1% of the whole one.
                CHECK(Abs(frame.Accum(32, 60, 0) - want.x) < 0.03f + 0.02f * want.x);
                CHECK(Abs(frame.Accum(32, 60, 1) - want.y) < 0.03f + 0.02f * want.y);
                CHECK(Abs(frame.Accum(32, 60, 2) - want.z) < 0.03f + 0.02f * want.z);
            }
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}

TEST_CASE("The procedural sky's own cube lights a surface with the sky's ambient colour", "[rhi][gpu][sky]")
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
            SkyLitPlane lit;
            SkyMakePlane(rig, &lit, 0.5f, true);
            SkySettings sky;
            sky.mode = SkyMode::Procedural;
            SkyLighting sl;
            sl.cube = MakeSkyEnvironmentCube(device.Get(), nullptr, sky);
            REQUIRE(sl.cube);
            sl.ambient = 1.0f;
            const Camera cam = HorizontalCamera();
            SkySettings none;
            none.mode = SkyMode::None;
            const SkyFrame frame = RenderSky(rig, lit.scene, cam, none, sl);
            REQUIRE(frame.ok);
            // The ambient light on an upward normal is the sky around the zenith, blurred: between the horizon and the
            // zenith colours, per channel (and not the flat 0.5 ambient light, which the sky replaced).
            const f32 albedo[3] = {0.6f, 0.5f, 0.4f};
            const f32 low[3] = {sky.zenith.x, sky.zenith.y, sky.zenith.z};
            const f32 high[3] = {sky.horizon.x, sky.horizon.y, sky.horizon.z};
            for (u32 c = 0; c < 3; ++c)
            {
                const f32 lit_value = frame.Accum(32, 60, c) / albedo[c];
                const f32 lo = low[c] < high[c] ? low[c] : high[c];
                const f32 hi = low[c] < high[c] ? high[c] : low[c];
                INFO("channel " << c << " value " << lit_value << " between " << lo << " and " << hi);
                CHECK(lit_value >= lo - 0.02f);
                CHECK(lit_value <= hi + 0.02f);
            }
            CHECK(Abs(frame.Accum(32, 60, 0) / 0.6f - 0.5f) > 0.05f);
            device->WaitIdle();
        }
        device->WaitIdle();
        device.Reset();
    }
}
