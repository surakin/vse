// Phase 4 Day 80: the CPU twins of the lighting shaders, the light buffer, and the passes' construction on the null device.
// The real-device tests (tests/rhi/vulkan_lighting_test.cpp) compare the shaders against these functions.
#include <cmath>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "renderer/camera.h"
#include "renderer/frame_data.h"
#include "renderer/lighting.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

Vec3 Unit(f32 x, f32 y, f32 z) {
    const f32 len = std::sqrt(x * x + y * y + z * z);
    return Vec3(x / len, y / len, z / len);
}

bool Near(f32 a, f32 b, f32 tol = 1e-5f) { return std::fabs(a - b) <= tol; }

} // namespace

TEST_CASE("A dielectric facing the light gets its albedo times the radiance plus a small specular lobe", "[renderer][lighting]") {
    SurfacePoint s;
    s.albedo = Vec3(0.5f, 0.25f, 1.0f);
    s.roughness = 1.0f;
    s.normal = Vec3(0, 1, 0);
    const Vec3 up(0, 1, 0);
    const Vec3 c = ShadeLight(s, up, up, Vec3(2, 2, 2));
    // Diffuse: albedo * 2. The lobe adds pi * D * Vis * F: for roughness 1, D = 1/pi, Vis = 0.5 / (1 + 1), F = 0.04, so 0.01 * 2.
    CHECK(Near(c.x, 1.0f + 0.02f, 1e-4f));
    CHECK(Near(c.y, 0.5f + 0.02f, 1e-4f));
    CHECK(Near(c.z, 2.0f + 0.02f, 1e-4f));
    // Away from the light: nothing.
    const Vec3 away = ShadeLight(s, up, Vec3(0, -1, 0), Vec3(5, 5, 5));
    CHECK(away.x == 0.0f);
    CHECK(away.y == 0.0f);
    // Grazing light follows N.L: the same surface with the light at 60 degrees gets about half.
    const Vec3 sixty = ShadeLight(s, up, Vec3(std::sin(1.0471976f), std::cos(1.0471976f), 0), Vec3(2, 2, 2));
    CHECK(sixty.x > 0.45f);
    CHECK(sixty.x < 0.56f);
}

TEST_CASE("A metal has no diffuse term and takes its color from the albedo through Fresnel", "[renderer][lighting]") {
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

TEST_CASE("Ambient and emissive terms follow occlusion and metallic", "[renderer][lighting]") {
    SurfacePoint s;
    s.albedo = Vec3(0.8f, 0.4f, 0.2f);
    s.occlusion = 0.5f;
    s.emissive_scale = 0.1f;
    const Vec3 a = ShadeAmbient(s, Vec3(0.2f, 0.2f, 0.2f));
    CHECK(Near(a.x, 0.8f * (0.5f * 0.2f + 0.1f)));
    CHECK(Near(a.y, 0.4f * (0.5f * 0.2f + 0.1f)));
    s.metallic = 1.0f;
    const Vec3 m = ShadeAmbient(s, Vec3(0.2f, 0.2f, 0.2f));
    CHECK(Near(m.x, 0.8f * 0.1f)); // only the emission is left
}

TEST_CASE("The tonemap curve is monotonic, bounded and has the ACES fit's known values", "[renderer][lighting]") {
    CHECK(TonemapAces(0.0f) == 0.0f);
    CHECK(TonemapAces(1000.0f) <= 1.0f);
    CHECK(TonemapAces(1000.0f) > 0.99f);
    CHECK(Near(TonemapAces(1.0f), 0.8038f, 1e-3f));  // (2.54) / (3.16)
    CHECK(Near(TonemapAces(0.18f), 0.2672f, 2e-3f)); // mid grey lifts a little
    f32 previous = -1.0f;
    for (int i = 0; i <= 200; ++i) {
        const f32 v = TonemapAces(static_cast<f32>(i) * 0.05f);
        CHECK(v >= previous);
        CHECK(v <= 1.0f);
        previous = v;
    }
}

TEST_CASE("World positions rebuilt from depth match the geometry in both pixel axes", "[renderer][lighting]") {
    Camera cam = MakeLookAtCamera(Vec3(2, 10, 3), Vec3(0, 0, 0), Vec3(0, 0, -1));
    cam.aspect = 1.5f;
    cam.near_plane = 0.5f;
    cam.far_plane = 40.0f;
    const u32 w = 96, h = 64;
    const FrameData fd = MakeFrameData(cam, w, h, 0, 0, 0);
    // Choose the points as pixel centers on the ground plane y = 0 by intersecting the camera ray, so the answer does not
    // depend on the code under test: forward = (-2, -10, -3) and so on is replaced by projecting and back-projecting by hand.
    for (const Vec3& truth : {Vec3(0, 0, 0), Vec3(3, 0, 2), Vec3(-4, 0, -3), Vec3(5, 1, 4)}) {
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
        // At this distance a pixel spans about 0.16 world units; the error is bounded by half a pixel plus the depth slope.
        INFO("truth " << truth.x << " " << truth.y << " " << truth.z << " got " << got.x << " " << got.y << " " << got.z);
        CHECK(std::fabs(got.x - truth.x) < 0.25f);
        CHECK(std::fabs(got.y - truth.y) < 0.35f);
        CHECK(std::fabs(got.z - truth.z) < 0.25f);
    }
    // A mirrored row would put the point on the other side of the view axis: the top row looks toward -Z for this camera.
    const Vec3 top = ReconstructWorldPosition(48, 0, 0.9f, w, h, fd.inverse_view_projection);
    const Vec3 bottom = ReconstructWorldPosition(48, h - 1, 0.9f, w, h, fd.inverse_view_projection);
    CHECK(top.z < bottom.z);
    const Vec3 left = ReconstructWorldPosition(0, 32, 0.9f, w, h, fd.inverse_view_projection);
    const Vec3 right = ReconstructWorldPosition(w - 1, 32, 0.9f, w, h, fd.inverse_view_projection);
    CHECK(left.x < right.x);
}

TEST_CASE("The light buffer holds directional lights first and sums the ambient ones", "[renderer][lighting]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        FrameResources frame(device, 4, 3);
        REQUIRE(frame.Valid());
        CHECK(frame.MaxLights() == 3);
        RenderScene scene(RenderSceneDesc{});
        Light point;
        point.type = LightType::Point;
        point.position = Vec3(1, 2, 3);
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
        const LightSummary summary = frame.WriteLights(scene);
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
        for (int i = 0; i < 5; ++i) many.AddLight(sun);
        device->BeginFrame();
        frame.BeginFrame(1);
        const LightSummary capped = frame.WriteLights(many);
        CHECK(capped.directional_count == 3);
        device->EndFrame();
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("The lighting and tonemap passes build their pipelines on the null device", "[renderer][lighting]") {
    rhi::NullDevice* device = rhi::CreateNullDevice();
    {
        MaterialSystem sys(device, MaterialSystemDesc{});
        FrameResources frame(device, 4);
        LightingPass lighting(device, &sys, &frame);
        TonemapPass tonemap(device);
        CHECK(lighting.Valid());
        CHECK(tonemap.Valid());
    }
    rhi::DestroyDevice(device);
}

TEST_CASE("Opposite view and light vectors do not produce NaN", "[renderer][lighting]") {
    SurfacePoint s;
    s.normal = Vec3(0, 1, 0);
    s.roughness = 0.3f;
    // V = -L: a surface lit from behind the viewer, with the normal facing away from the viewer; V + L is the zero vector.
    const Vec3 c = ShadeLight(s, Vec3(0, -1, 0), Vec3(0, 1, 0), Vec3(1, 1, 1));
    CHECK(std::isfinite(c.x));
    CHECK(std::isfinite(c.y));
    CHECK(std::isfinite(c.z));
}
