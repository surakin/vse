// The sky: the CPU model of the shader (SkyColor), the settings' validation, and the pass on the null device. The
// real-device test (tests/rhi/sky_gpu_test.cpp) compares the shader with this model.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "renderer/frame_data.h"
#include "renderer/lighting.h"
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
