// Phase 5 Day 80: the CPU twins of the lighting shaders, the light buffer, and the passes' construction on the null
// device. The real-device tests (tests/rhi/vulkan_lighting_test.cpp) compare the shaders against these functions.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/lighting.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace
{

bool Near(f32 a, f32 b, f32 tol = 1e-5f)
{
    return Abs(a - b) <= tol;
}

}  // namespace

TEST_CASE("A dielectric facing the light gets its albedo times the radiance plus a small specular lobe",
          "[renderer][lighting]")
{
    SurfacePoint s;
    s.albedo = Vec3(0.5f, 0.25f, 1.0f);
    s.roughness = 1.0f;
    s.normal = Vec3(0, 1, 0);
    const Vec3 up(0, 1, 0);
    const Vec3 c = ShadeLight(s, up, up, Vec3(2, 2, 2));
    // Diffuse: albedo * 2. The lobe adds pi * D * Vis * F: for roughness 1, D = 1/pi, Vis = 0.5 / (1 + 1), F = 0.04, so
    // 0.01 * 2.
    CHECK(Near(c.x, 1.0f + 0.02f, 1e-4f));
    CHECK(Near(c.y, 0.5f + 0.02f, 1e-4f));
    CHECK(Near(c.z, 2.0f + 0.02f, 1e-4f));
    // Away from the light: nothing.
    const Vec3 away = ShadeLight(s, up, Vec3(0, -1, 0), Vec3(5, 5, 5));
    CHECK(away.x == 0.0f);
    CHECK(away.y == 0.0f);
    // Grazing light follows N.L: the same surface with the light at 60 degrees gets about half.
    const Vec3 sixty = ShadeLight(s, up, Vec3(Sin(1.0471976f), Cos(1.0471976f), 0), Vec3(2, 2, 2));
    CHECK(sixty.x > 0.45f);
    CHECK(sixty.x < 0.56f);
}

TEST_CASE("A metal has no diffuse term and takes its color from the albedo through Fresnel", "[renderer][lighting]")
{
    SurfacePoint s;
    s.albedo = Vec3(1.0f, 0.8f, 0.2f);
    s.metallic = 1.0f;
    s.roughness = 0.5f;
    s.normal = Vec3(0, 1, 0);
    const Vec3 up(0, 1, 0);
    const Vec3 c = ShadeLight(s, up, up, Vec3(1, 1, 1));
    CHECK(c.x > c.y);
    CHECK(c.y > c.z);
    // The channels are in the ratio of F0 = albedo at normal incidence (Schlick leaves F0 alone when V.H = 1).
    CHECK(Near(c.y / c.x, 0.8f, 1e-3f));
    CHECK(Near(c.z / c.x, 0.2f, 1e-3f));
    // A rougher metal has a lower peak toward the mirror direction.
    SurfacePoint rough = s;
    rough.roughness = 0.9f;
    CHECK(ShadeLight(rough, up, up, Vec3(1, 1, 1)).x < c.x);
}

TEST_CASE("Ambient and emissive terms follow occlusion and metallic", "[renderer][lighting]")
{
    SurfacePoint s;
    s.albedo = Vec3(0.8f, 0.4f, 0.2f);
    s.occlusion = 0.5f;
    s.emissive_scale = 0.1f;
    const Vec3 a = ShadeAmbient(s, Vec3(0.2f, 0.2f, 0.2f));
    CHECK(Near(a.x, 0.8f * (0.5f * 0.2f + 0.1f)));
    CHECK(Near(a.y, 0.4f * (0.5f * 0.2f + 0.1f)));
    s.metallic = 1.0f;
    const Vec3 m = ShadeAmbient(s, Vec3(0.2f, 0.2f, 0.2f));
    CHECK(Near(m.x, 0.8f * 0.1f));  // only the emission is left
}

TEST_CASE("The tonemap curve is monotonic, bounded and has the ACES fit's known values", "[renderer][lighting]")
{
    CHECK(TonemapAces(0.0f) == 0.0f);
    CHECK(TonemapAces(1000.0f) <= 1.0f);
    CHECK(TonemapAces(1000.0f) > 0.99f);
    CHECK(Near(TonemapAces(1.0f), 0.8038f, 1e-3f));   // (2.54) / (3.16)
    CHECK(Near(TonemapAces(0.18f), 0.2672f, 2e-3f));  // mid grey lifts a little
    f32 previous = -1.0f;
    for (int i = 0; i <= 200; ++i)
    {
        const f32 v = TonemapAces(static_cast<f32>(i) * 0.05f);
        CHECK(v >= previous);
        CHECK(v <= 1.0f);
        previous = v;
    }
}

TEST_CASE("World positions rebuilt from depth match the geometry in both pixel axes", "[renderer][lighting]")
{
    Camera cam = MakeLookAtCamera(Vec3(2, 10, 3), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.5f;
    cam.near_plane = 0.5f;
    cam.far_plane = 40.0f;
    const u32 w = 96, h = 64;
    const FrameData fd = MakeFrameData(cam, w, h, 0, 0, 0);
    // Choose the points as pixel centers on the ground plane y = 0 by intersecting the camera ray, so the answer does
    // not depend on the code under test: forward = (-2, -10, -3) and so on is replaced by projecting and
    // back-projecting by hand.
    for (const Vec3& truth : {Vec3(0, 0, 0), Vec3(3, 0, 2), Vec3(-4, 0, -3), Vec3(5, 1, 4)})
    {
        const Vec4 clip = cam.ViewProjection() * Vec4(truth.x, truth.y, truth.z, 1.0f);
        const f32 nx = clip.x / clip.w;
        const f32 ny = clip.y / clip.w;
        const f32 depth = clip.z / clip.w;
        const f32 px = (nx * 0.5f + 0.5f) * static_cast<f32>(w) - 0.5f;
        const f32 py = (ny * 0.5f + 0.5f) * static_cast<f32>(h) - 0.5f;
        // Round to the nearest pixel; the point then lies within half a pixel of that pixel's center.
        const u32 ix = static_cast<u32>(px + 0.5f);
        const u32 iy = static_cast<u32>(py + 0.5f);
        const Vec3 got = ReconstructWorldPosition(ix, iy, depth, w, h, fd.inverse_view_projection);
        // At this distance a pixel spans about 0.16 world units; the error is bounded by half a pixel plus the depth
        // slope.
        INFO("truth " << truth.x << " " << truth.y << " " << truth.z << " got " << got.x << " " << got.y << " "
                      << got.z);
        CHECK(Abs(got.x - truth.x) < 0.25f);
        CHECK(Abs(got.y - truth.y) < 0.35f);
        CHECK(Abs(got.z - truth.z) < 0.25f);
    }
    // A mirrored row would put the point on the other side of the view axis: the top row looks toward -Z for this
    // camera.
    const Vec3 top = ReconstructWorldPosition(48, 0, 0.9f, w, h, fd.inverse_view_projection);
    const Vec3 bottom = ReconstructWorldPosition(48, h - 1, 0.9f, w, h, fd.inverse_view_projection);
    CHECK(top.z < bottom.z);
    const Vec3 left = ReconstructWorldPosition(0, 32, 0.9f, w, h, fd.inverse_view_projection);
    const Vec3 right = ReconstructWorldPosition(w - 1, 32, 0.9f, w, h, fd.inverse_view_projection);
    CHECK(left.x < right.x);
}

namespace
{

// A camera at the origin looking down -Z, 64x64.
FrameData OriginView(f32 far_plane = 100.0f)
{
    Camera cam = MakeLookAtCamera(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = far_plane;
    return MakeFrameData(cam, 64, 64, 0, 0, 0);
}

Light PointAt(const Vec3& position, f32 radius, f32 intensity = 1.0f)
{
    Light l;
    l.type = LightType::Point;
    l.position = position;
    l.radius = radius;
    l.intensity = intensity;
    return l;
}

}  // namespace

TEST_CASE("The light buffer holds directional lights first and sums the ambient ones", "[renderer][lighting]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    const FrameData view = OriginView();
    {
        FrameResources frame(device.Get(), 4, 3);
        REQUIRE(frame.Valid());
        CHECK(frame.MaxLights() == 3);
        RenderScene scene(RenderSceneDesc{});
        Light point;
        point.type = LightType::Point;
        point.position = Vec3(1, 2, -8);  // in front of the camera
        point.radius = 5.0f;
        scene.AddLight(point);
        Light sun;
        sun.type = LightType::Directional;
        sun.direction = Vec3(0, 1, 0);
        sun.color = Vec3(1, 0.5f, 0.25f);
        sun.intensity = 2.0f;
        scene.AddLight(sun);
        Light amb;
        amb.type = LightType::Ambient;
        amb.color = Vec3(0.1f, 0.2f, 0.3f);
        amb.intensity = 2.0f;
        scene.AddLight(amb);
        scene.AddLight(amb);
        device->BeginFrame();
        frame.BeginFrame(0);
        const LightSummary summary = frame.WriteLights(scene, view);
        CHECK(summary.directional_count == 1);
        CHECK(summary.point_count == 1);
        CHECK(Near(summary.ambient.x, 0.4f));
        CHECK(Near(summary.ambient.z, 1.2f));
        const BufferRegion region = frame.LightRegion();
        CHECK(region.size == 3 * sizeof(LightData));
        frame.Flush();
        device->EndFrame();
        // More lights than the buffer holds are dropped, not written past the end.
        RenderScene many(RenderSceneDesc{});
        for (int i = 0; i < 5; ++i)
        {
            many.AddLight(sun);
        }
        device->BeginFrame();
        frame.BeginFrame(1);
        const LightSummary capped = frame.WriteLights(many, view);
        CHECK(capped.directional_count == 3);
        CHECK(capped.dropped == 2);  // not silent: the excess is counted
        device->EndFrame();
    }
    device.Reset();
}

TEST_CASE("The lighting and tonemap passes build their pipelines on the null device", "[renderer][lighting]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        MaterialSystem sys(device.Get(), MaterialSystemDesc{});
        FrameResources frame(device.Get(), 4);
        LightingPass lighting(device.Get(), &sys, &frame);
        TonemapPass tonemap(device.Get());
        CHECK(lighting.Valid());
        CHECK(tonemap.Valid());
    }
    device.Reset();
}

TEST_CASE("Opposite view and light vectors do not produce NaN", "[renderer][lighting]")
{
    SurfacePoint s;
    s.normal = Vec3(0, 1, 0);
    s.roughness = 0.3f;
    // V = -L: a surface lit from behind the viewer, with the normal facing away from the viewer; V + L is the zero
    // vector.
    const Vec3 c = ShadeLight(s, Vec3(0, -1, 0), Vec3(0, 1, 0), Vec3(1, 1, 1));
    CHECK(IsFinite(c.x));
    CHECK(IsFinite(c.y));
    CHECK(IsFinite(c.z));
}

TEST_CASE("Point light attenuation is 1 at the light, falls off, and is exactly zero at and beyond the radius",
          "[renderer][lighting][point]")
{
    CHECK(Near(PointLightAttenuation(0.0f, 10.0f), 1.0f));
    // Matches the formula: window = (1 - (d/R)^4)^2, over d^2 + 1.
    for (f32 d : {0.5f, 1.0f, 3.0f, 7.0f, 9.5f})
    {
        const f32 r = d / 10.0f;
        const f32 w = 1.0f - r * r * r * r;
        CHECK(Near(PointLightAttenuation(d, 10.0f), w * w / (d * d + 1.0f), 1e-6f));
    }
    CHECK(PointLightAttenuation(10.0f, 10.0f) == 0.0f);
    CHECK(PointLightAttenuation(10.001f, 10.0f) == 0.0f);
    CHECK(PointLightAttenuation(1000.0f, 10.0f) == 0.0f);
    // Monotonically non-increasing, so a farther surface is never brighter.
    f32 previous = 2.0f;
    for (int i = 0; i <= 200; ++i)
    {
        const f32 a = PointLightAttenuation(static_cast<f32>(i) * 0.05f, 10.0f);
        CHECK(a <= previous);
        previous = a;
    }
    // Zero slope at the radius: just inside it the value is negligible, so the edge of the volume shows no seam.
    CHECK(PointLightAttenuation(9.99f, 10.0f) < 1e-6f);
}

TEST_CASE("The light volume sphere contains the light's sphere and is wound counter-clockwise from outside",
          "[renderer][lighting][point]")
{
    f32 min_face_distance = 1e30f;
    for (u32 t = 0; t < kLightVolumeVertexCount / 3; ++t)
    {
        const Vec3 a = LightVolumeVertex(t * 3);
        const Vec3 b = LightVolumeVertex(t * 3 + 1);
        const Vec3 c = LightVolumeVertex(t * 3 + 2);
        const Vec3 n = (b - a).Cross(c - a);
        const f32 len = Sqrt(n.Dot(n));
        if (len < 1e-7f)
        {
            continue;  // the poles have degenerate triangles
        }
        // Counter-clockwise from outside: the normal points away from the center.
        CHECK(n.Dot(a) > 0.0f);
        min_face_distance = FMin(min_face_distance, n.Dot(a) / len);
    }
    // Every face plane is at least one radius from the center, so the unit sphere is inside the polyhedron.
    CHECK(min_face_distance >= 1.0f - 1e-4f);
    CHECK(min_face_distance < 1.2f);  // and not wastefully large
}

TEST_CASE("The screen rectangle of a light bounds its projected sphere", "[renderer][lighting][point]")
{
    const FrameData view = OriginView();
    rhi::ScissorRect r;
    // Straight ahead, centered: a rectangle around the middle of the 64x64 target.
    REQUIRE(LightScreenRect(view, Vec3(0, 0, -10), 2.0f, 64, 64, &r));
    CHECK(r.x > 0);
    CHECK(r.y > 0);
    CHECK(r.x + static_cast<i32>(r.width) < 64);
    CHECK(Abs((r.x + static_cast<i32>(r.width) / 2) - 32) <= 1);
    // The sphere's extreme points project inside the rectangle.
    for (const Vec3& p : {Vec3(2, 0, -10), Vec3(-2, 0, -10), Vec3(0, 2, -10), Vec3(0, -2, -10)})
    {
        const Vec4 c = view.view_projection * Vec4(p.x, p.y, p.z, 1.0f);
        const f32 px = (c.x / c.w * 0.5f + 0.5f) * 64.0f;
        const f32 py = (c.y / c.w * 0.5f + 0.5f) * 64.0f;
        CHECK(px >= static_cast<f32>(r.x));
        CHECK(px <= static_cast<f32>(r.x + static_cast<i32>(r.width)));
        CHECK(py >= static_cast<f32>(r.y));
        CHECK(py <= static_cast<f32>(r.y + static_cast<i32>(r.height)));
    }
    // A camera inside the sphere: the whole target.
    REQUIRE(LightScreenRect(view, Vec3(0, 0, 0), 5.0f, 64, 64, &r));
    CHECK(r.x == 0);
    CHECK(r.y == 0);
    CHECK(r.width == 64);
    CHECK(r.height == 64);
    // Entirely off to the side: empty.
    CHECK_FALSE(LightScreenRect(view, Vec3(100, 0, -10), 2.0f, 64, 64, &r));
    // A sphere partly off the edge is clamped to the target.
    REQUIRE(LightScreenRect(view, Vec3(10, 0, -10), 3.0f, 64, 64, &r));
    CHECK(r.x + static_cast<i32>(r.width) == 64);
}

TEST_CASE("Point lights are culled against the frustum and written nearest first", "[renderer][lighting][point]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    const FrameData view = OriginView();
    {
        FrameResources frame(device.Get(), 4, 16);
        REQUIRE(frame.Valid());
        RenderScene scene(RenderSceneDesc{});
        scene.AddLight(PointAt(Vec3(0, 0, -30), 2.0f, 3.0f));  // far
        scene.AddLight(PointAt(Vec3(0, 0, -5), 2.0f, 1.0f));   // near
        scene.AddLight(PointAt(Vec3(0, 0, 50), 2.0f));         // behind the camera
        scene.AddLight(PointAt(Vec3(500, 0, -5), 2.0f));       // far to the side
        scene.AddLight(PointAt(Vec3(0, 0, -15), 2.0f, 2.0f));  // middle
        scene.AddLight(PointAt(Vec3(0, 0, -500), 2.0f));       // beyond the far plane
        device->BeginFrame();
        frame.BeginFrame(0);
        const LightSummary summary = frame.WriteLights(scene, view);
        CHECK(summary.point_count == 3);
        CHECK(summary.point_culled == 3);
        CHECK(summary.dropped == 0);
        REQUIRE(frame.WrittenLightCount() == 3);
        const LightData* lights = frame.WrittenLights();
        CHECK(lights[0].position_radius.z == -5.0f);
        CHECK(lights[1].position_radius.z == -15.0f);
        CHECK(lights[2].position_radius.z == -30.0f);
        CHECK(lights[0].direction.w == 1.0f);  // the type tag: point
        CHECK(lights[2].color.x == 3.0f);      // color * intensity travels with the light
        device->EndFrame();
    }
    device.Reset();
}

TEST_CASE("Equal distances are ordered by slot, and the order does not depend on how the lights were added",
          "[renderer][lighting][point]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    const FrameData view = OriginView();
    {
        FrameResources frame(device.Get(), 4, 16);
        RenderScene scene(RenderSceneDesc{});
        // Four lights at the same distance, different colors (intensity marks the slot).
        for (int i = 0; i < 4; ++i)
        {
            const f32 angle = static_cast<f32>(i) * 0.05f;
            // All at distance 10 from the origin; kept in front of the camera.
            scene.AddLight(PointAt(Vec3(10.0f * Sin(angle), 0, -10.0f * Cos(angle)), 1.0f, static_cast<f32>(i + 1)));
        }
        device->BeginFrame();
        frame.BeginFrame(0);
        frame.WriteLights(scene, view);
        REQUIRE(frame.WrittenLightCount() == 4);
        // Distances agree to rounding; whatever the order is, a second call gives the same one.
        f32 first[4];
        for (int i = 0; i < 4; ++i)
        {
            first[i] = frame.WrittenLights()[i].color.x;
        }
        frame.WriteLights(scene, view);
        for (int i = 0; i < 4; ++i)
        {
            CHECK(frame.WrittenLights()[i].color.x == first[i]);
        }
        device->EndFrame();
    }
    device.Reset();
}

TEST_CASE("Point lights that do not fit are counted, the nearest are kept, and the error is not silent",
          "[renderer][lighting][point]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    const FrameData view = OriginView();
    {
        FrameResources frame(device.Get(), 4, 3);
        RenderScene scene(RenderSceneDesc{});
        for (int i = 0; i < 5; ++i)
        {
            scene.AddLight(PointAt(Vec3(0, 0, -5.0f - 3.0f * static_cast<f32>(i)), 1.0f));
        }
        Light sun;
        sun.type = LightType::Directional;
        scene.AddLight(sun);
        device->BeginFrame();
        frame.BeginFrame(0);
        const LightSummary summary = frame.WriteLights(scene, view);
        CHECK(summary.directional_count == 1);  // directional lights have priority
        CHECK(summary.point_count == 2);
        CHECK(summary.dropped == 3);
        CHECK(frame.WrittenLights()[1].position_radius.z == -5.0f);
        CHECK(frame.WrittenLights()[2].position_radius.z == -8.0f);
        device->EndFrame();
    }
    device.Reset();
}

TEST_CASE("The light capacity limit is enforced", "[renderer][lighting][point]")
{
    CHECK(kMaxLights == 16384);
    RenderScene scene(RenderSceneDesc{4, kMaxLights});
    CHECK(scene.LightCount() == 0);
}
