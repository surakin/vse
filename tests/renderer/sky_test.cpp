// The sky: the CPU model of the shader (SkyColor), the settings' validation, and the pass on the null device. The
// real-device test (tests/rhi/sky_gpu_test.cpp) compares the shader with this model.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "renderer/frame_data.h"
#include "renderer/lighting.h"
#include "containers/shared_bytes.h"
#include "renderer/render_graph.h"
#include "renderer/sky.h"
#include "rhi/null/null_device.h"

using namespace vse;
using namespace vse::renderer;

namespace
{

bool SkyNear(f32 a, f32 b, f32 tol = 1e-5f)
{
    return Abs(a - b) <= tol;
}

bool SkyNear(const Vec3& a, const Vec3& b, f32 tol = 1e-5f)
{
    return SkyNear(a.x, b.x, tol) && SkyNear(a.y, b.y, tol) && SkyNear(a.z, b.z, tol);
}

SkySettings Procedural()
{
    SkySettings s;
    s.mode = SkyMode::Procedural;
    s.sun_direction = Vec3(1, 0.3f, 0).Normalized();
    return s;
}

}  // namespace

TEST_CASE("The default sky is the flat colour mode and is valid", "[renderer][sky]")
{
    const SkySettings s;
    CHECK(s.mode == SkyMode::Color);
    CHECK(SkySettingsValid(s));
    CHECK(SkyNear(SkyColor(s, Vec3(0, 1, 0)), s.color));
    SkySettings none;
    none.mode = SkyMode::None;
    CHECK(SkyNear(SkyColor(none, Vec3(1, 0, 0)), Vec3(0, 0, 0)));
}

TEST_CASE("The gradient is the zenith colour up, the horizon colour at the horizon and the ground colour below",
          "[renderer][sky]")
{
    SkySettings s = Procedural();
    s.sun_color = Vec3(0, 0, 0);
    CHECK(SkyNear(SkyColor(s, Vec3(0, 1, 0)), s.zenith));
    CHECK(SkyNear(SkyColor(s, Vec3(0, 3, 0)), s.zenith));  // not unit: the direction is normalized
    CHECK(SkyNear(SkyColor(s, Vec3(0, 0, -1)), s.horizon));
    CHECK(SkyNear(SkyColor(s, Vec3(0, -1, 0)), s.ground));
    // The ground colour is reached at `horizon_blend` below the horizon and not before.
    const Vec3 at_blend(Sqrt(1.0f - s.horizon_blend * s.horizon_blend), -s.horizon_blend, 0.0f);
    CHECK(SkyNear(SkyColor(s, at_blend), s.ground, 1e-4f));
    const Vec3 halfway(Sqrt(1.0f - 0.0025f), -0.05f, 0.0f);
    const Vec3 mid = SkyColor(s, halfway);
    CHECK(SkyNear(mid.x, (s.horizon.x + s.ground.x) * 0.5f, 1e-4f));  // the smooth step is 0.5 at the middle
    // Exposure scales the whole sky.
    SkySettings bright = s;
    bright.exposure = 2.0f;
    const Vec3 up = SkyColor(bright, Vec3(0, 1, 0));
    CHECK(SkyNear(up, s.zenith * 2.0f));
}

TEST_CASE("The sun adds its colour inside the disc and nothing outside the soft edge", "[renderer][sky]")
{
    SkySettings s = Procedural();
    s.sun_direction = Vec3(0, 0, -1);
    s.sun_size = 0.1f;
    s.sun_color = Vec3(10, 10, 10);
    const Vec3 sky_only = SkyColor(s, Vec3(0, 0, 1));  // behind the viewer: the horizon colour, no sun
    CHECK(SkyNear(sky_only, s.horizon));
    CHECK(SkyNear(SkyColor(s, Vec3(0, 0, -1)), s.horizon + Vec3(10, 10, 10)));
    // Just inside the disc: full sun; well outside the soft edge (1.25 * size): none.
    const Vec3 inside(Sin(0.09f), 0.0f, -Cos(0.09f));
    CHECK(SkyNear(SkyColor(s, inside), s.horizon + Vec3(10, 10, 10), 1e-3f));
    const Vec3 outside(Sin(0.13f), 0.0f, -Cos(0.13f));
    CHECK(SkyNear(SkyColor(s, outside), s.horizon, 1e-4f));
    // In the soft edge it is between the two.
    const Vec3 edge(Sin(0.11f), 0.0f, -Cos(0.11f));
    const f32 v = SkyColor(s, edge).x - s.horizon.x;
    CHECK(v > 0.5f);
    CHECK(v < 9.5f);
}

TEST_CASE("Settings with a bad number are refused", "[renderer][sky]")
{
    SkySettings s = Procedural();
    CHECK(SkySettingsValid(s));
    SkySettings bad = s;
    bad.zenith = Vec3(-0.1f, 0, 0);
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.sun_direction = Vec3(0, 0, 0);
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.sun_size = 0.0f;
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.sun_size = 2.0f;
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.horizon_blend = 0.0f;
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.exposure = 0.0f;
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.horizon = Vec3(NAN, 0, 0);
    CHECK_FALSE(SkySettingsValid(bad));
    bad = s;
    bad.rotation = INFINITY;
    CHECK_FALSE(SkySettingsValid(bad));
}

TEST_CASE("The sky pass joins the graph only for the procedural mode and survives a release and a recreate",
          "[renderer][sky]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        MaterialSystem sys(device.Get(), MaterialSystemDesc{});
        FrameResources frame(device.Get(), 4);
        SkyPass sky(device.Get(), &sys, &frame);
        REQUIRE(sky.Valid());
        CHECK(sky.Live());
        frame.BeginFrame(0);

        ArenaAllocator arena(1 << 16);
        RenderGraph graph(&arena, RenderGraphDesc{});
        const GBufferTargets targets{};
        const GraphResource accum{};
        sky.Prepare(SkySettings{});  // Color
        CHECK_FALSE(sky.AddToGraph(&graph, targets, accum));
        SkySettings none;
        none.mode = SkyMode::None;
        sky.Prepare(none);
        CHECK_FALSE(sky.AddToGraph(&graph, targets, accum));

        sky.Release();
        CHECK_FALSE(sky.Live());
        CHECK_FALSE(sky.Valid());
        CHECK(sky.Recreate(device.Get()));
        CHECK(sky.Live());
        CHECK(sky.Valid());
    }
    device.Reset();
}

TEST_CASE("The cube direction turns the view by minus the rotation about +Y", "[renderer][sky]")
{
    SkySettings s;
    CHECK(SkyNear(SkyCubeDirection(s, Vec3(1, 2, 3)), Vec3(1, 2, 3)));
    s.rotation = 1.57079632679f;
    // A quarter turn: the sky's +X now looks along -Z, so the view direction -Z reads the cube's +X.
    CHECK(SkyNear(SkyCubeDirection(s, Vec3(0, 0, -1)), Vec3(1, 0, 0), 1e-5f));
    CHECK(SkyNear(SkyCubeDirection(s, Vec3(0, 1, 0)), Vec3(0, 1, 0), 1e-5f));
    CHECK(SkyNear(SkyCubeDirection(s, Vec3(1, 0, 0)), Vec3(0, 0, 1), 1e-5f));
    s.tint = Vec3(-1, 0, 0);
    CHECK_FALSE(SkySettingsValid(s));
}

TEST_CASE("A cube sky is drawn from a cube texture and falls back without one", "[renderer][sky]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        MaterialSystem sys(device.Get(), MaterialSystemDesc{});
        FrameResources frame(device.Get(), 4);
        SkyPass pass(device.Get(), &sys, &frame);
        REQUIRE(pass.Valid());
        frame.BeginFrame(0);

        rhi::TextureDesc desc;
        desc.dimension = rhi::TextureDimension::Cube;
        desc.width = 4;
        desc.height = 4;
        desc.array_layers = 6;
        desc.format = rhi::Format::RGBA8_UNorm;
        desc.usage = rhi::TextureUsage::Sampled;
        Vector<byte> bytes;
        bytes.Resize(6 * 4 * 4 * 4);
        Vector<TextureLevelSpan> levels;
        for (u32 face = 0; face < 6; ++face)
        {
            TextureLevelSpan span;
            span.offset = face * 64;
            span.bytes = 64;
            span.row_pitch = 16;
            span.layer = face;
            levels.Push(span);
        }
        GpuTexturePtr cube = MakeShared<GpuTexture>();
        REQUIRE(cube->CreateFromSharedLevels(device.Get(), desc, MakeShared<OwnedBytes>(Move(bytes)),
                                             Span<const TextureLevelSpan>(levels.Data(), levels.Size()), nullptr,
                                             "cube"));

        SkySettings sky;
        sky.mode = SkyMode::Cube;
        sky.cube = cube;
        ArenaAllocator arena(1 << 16);
        {
            RenderGraph graph(&arena, RenderGraphDesc{});
            const GBufferTargets targets{};
            const GraphResource accum{};
            pass.Prepare(sky);
            CHECK(pass.AddToGraph(&graph, targets, accum));  // a cube draws
            sky.cube.Reset();
            pass.Prepare(sky);
            CHECK(pass.AddToGraph(&graph, targets, accum));  // no cube: the procedural sky draws
        }
        // The pass survives a release and a recreate with the cube still assigned.
        sky.cube = cube;
        pass.Release();
        CHECK(pass.Recreate(device.Get()));
        CHECK(pass.Valid());
        pass.Prepare(sky);
        RenderGraph graph(&arena, RenderGraphDesc{});
        CHECK(pass.AddToGraph(&graph, GBufferTargets{}, GraphResource{}));
    }
    device.Reset();
}

TEST_CASE("The procedural sky makes a small environment cube, again only when its gradient changes", "[renderer][sky]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    SkySettings sky = Procedural();
    GpuTexturePtr cube = MakeSkyEnvironmentCube(device.Get(), nullptr, sky);
    REQUIRE(cube);
    CHECK(cube->Valid());
    CHECK(cube->Desc().dimension == rhi::TextureDimension::Cube);
    CHECK(cube->Desc().format == rhi::Format::RGBA16_Float);
    CHECK(cube->Desc().array_layers == 6);
    CHECK(cube->Desc().mip_levels == 5);

    // The sun and the sun-only settings do not change the cube; the gradient and the exposure do.
    SkySettings other = sky;
    other.sun_color = Vec3(1, 1, 1);
    other.sun_direction = Vec3(0, 1, 0);
    other.rotation = 1.0f;
    CHECK(SkyEnvironmentSame(sky, other));
    other.zenith = Vec3(0.9f, 0.9f, 0.9f);
    CHECK_FALSE(SkyEnvironmentSame(sky, other));
    other = sky;
    other.exposure = 2.0f;
    CHECK_FALSE(SkyEnvironmentSame(sky, other));

    rhi::NullDeviceOptions options;
    options.caps.cube_textures = false;
    rhi::NullDevicePtr without = rhi::CreateNullDevice(options);
    CHECK_FALSE(MakeSkyEnvironmentCube(without.Get(), nullptr, sky));
    cube.Reset();
    device.Reset();
}
