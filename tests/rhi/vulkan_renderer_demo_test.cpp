// Phase 4 Day 82: the renderer demo's scene rendered offscreen and compared with a golden image, and depth correctness: the
// picture must not depend on the order objects were added, and intersecting surfaces must resolve where the geometry says.
// Validation and synchronization validation are on and must stay silent.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "demo_frame.h"
#include "demo_scene.h"
#include "gbuffer_rig.h"
#include "gpu_fixture.h"
#include "image_compare.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kShotW = 320;
constexpr u32 kShotH = 180;

// What one offscreen render of the demo scene produced (`ok` is false when the frame or any object failed).
struct DemoShot {
    gpu_test::Image image;
    GBufferStats stats;
    LightSummary lights;
    u32 volume_draws = 0;
    bool ok = false;
    std::string error;
};

DemoShot RenderDemo(rhi::Device* device, const renderer_demo::SceneOptions& options, int gbuffer_layer = -1) {
    DemoShot shot;
    rhi::TextureDesc td;
    td.width = kShotW;
    td.height = kShotH;
    td.format = rhi::Format::RGBA8_SRGB;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
    const rhi::TextureHandle target = device->CreateTexture(td);
    REQUIRE(target.IsValid());
    {
        renderer_demo::DemoFrame frame;
        renderer_demo::DemoScene scene;
        renderer_demo::FrameSetup fs;
        fs.device = device;
        fs.max_objects = options.objects + 16;
        fs.max_lights = options.lights + 2;
        fs.gbuffer_layer = gbuffer_layer;
        REQUIRE(frame.Create(fs));
        REQUIRE(scene.Create(device, frame.Materials(), frame.Templates(), options));
        scene.Animate(0.5f);
        Camera camera = scene.OverviewCamera(static_cast<f32>(kShotW) / static_cast<f32>(kShotH));
        FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
        FrameTarget ft;
        ft.texture = target;
        ft.width = kShotW;
        ft.height = kShotH;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.initial = rhi::ResourceState::Undefined;
        ft.final_state = rhi::ResourceState::CopySrc;
        frame.SetTime(0.5f, 0.016f);
        const FrameStatus status = frame.Render(&scene.Scene(), &camera, &ft);
        shot.error = frame.LastError();
        shot.stats = frame.GBufferStatistics();
        shot.lights = frame.Lights();
        shot.volume_draws = frame.PointDraws();
        shot.ok = status == FrameStatus::Ok && frame.LastGBufferOk();
        device->WaitIdle();
        shot.image.width = kShotW;
        shot.image.height = kShotH;
        shot.image.rgba.resize(static_cast<size_t>(kShotW) * kShotH * 4);
        REQUIRE(device->ReadbackTexture(target, 0, 0, shot.image.rgba.data(), shot.image.rgba.size(), rhi::ResourceState::CopySrc));
    }
    device->DestroyTexture(target);
    return shot;
}

renderer_demo::SceneOptions DefaultOptions() {
    renderer_demo::SceneOptions so;
    so.asset_dir = ENGINE_RENDERER_DEMO_ASSET_DIR;
    return so;
}

} // namespace

TEST_CASE("The demo scene renders offscreen and matches its golden image", "[rhi][vulkan][gpu][golden][demo]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::DevicePtr device = gbuffer_rig::MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        const DemoShot shot = RenderDemo(device.Get(), DefaultOptions());
        INFO(shot.error);
        REQUIRE(shot.ok);
        // Independent of the golden: the scene is as large as advertised and everything that should be drawn is.
        CHECK(shot.stats.visible > 100);
        CHECK(shot.stats.rejected == 0);
        CHECK(shot.stats.draws < shot.stats.drawn_objects); // instanced runs: fewer draws than objects
        CHECK(shot.lights.point_count > 50);
        CHECK(shot.lights.dropped == 0);
        CHECK(shot.volume_draws == shot.lights.point_count);
        // The sky stays black (nothing there to light), the ground is lit: the top row is black, the bottom row is not.
        int top_lit = 0, bottom_lit = 0;
        for (u32 x = 0; x < kShotW; ++x) {
            const unsigned char* t = shot.image.Pixel(x, 2);
            const unsigned char* b = shot.image.Pixel(x, kShotH - 3);
            top_lit += (t[0] + t[1] + t[2]) > 0;
            bottom_lit += (b[0] + b[1] + b[2]) > 30;
        }
        CHECK(top_lit == 0);
        CHECK(bottom_lit > static_cast<int>(kShotW) * 9 / 10);

        if (gpu_test::UpdatingGoldens()) {
            REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("renderer_demo.ppm").c_str(), shot.image));
            WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/renderer_demo.ppm was rewritten; inspect it before committing");
        } else {
            gpu_test::Image golden;
            REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("renderer_demo.ppm").c_str(), &golden));
            gpu_test::CompareOptions co;
            co.tolerance = 6;          // GGX highlights and 300 summed lights: a few levels of rounding difference
            co.edge_tolerance = 90;
            co.max_edge_miss = 0.3;
            co.max_interior_miss = 0.002;
            const gpu_test::CompareResult result = gpu_test::Compare(golden, shot.image, co, "renderer_demo");
            INFO(result.message);
            CHECK(result.same);
        }
        device->WaitIdle();
    }
    device->WaitIdle();
    device.Reset();
}

TEST_CASE("The picture does not depend on the order objects were added", "[rhi][vulkan][gpu][demo][depth]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::DevicePtr device = gbuffer_rig::MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        renderer_demo::SceneOptions so = DefaultOptions();
        so.objects = 200; // smaller than the milestone scene: this test renders it several times
        so.lights = 60;
        const DemoShot natural = RenderDemo(device.Get(), so);
        REQUIRE(natural.ok);
        for (u32 seed : {3u, 77u, 12345u}) {
            so.add_order_seed = seed;
            const DemoShot shuffled = RenderDemo(device.Get(), so);
            REQUIRE(shuffled.ok);
            // The same draws (same visible count) and exactly the same picture, pixel for pixel.
            CHECK(shuffled.stats.visible == natural.stats.visible);
            size_t differing = 0;
            for (size_t i = 0; i < natural.image.rgba.size(); i += 4) {
                if (std::memcmp(&natural.image.rgba[i], &shuffled.image.rgba[i], 3) != 0) ++differing;
            }
            INFO("seed " << seed << ": " << differing << " of " << kShotW * kShotH << " pixels differ");
            CHECK(differing == 0);
        }
        device->WaitIdle();
    }
    device->WaitIdle();
    device.Reset();
}

namespace {

using namespace gbuffer_rig;

// A mesh of `size` x `size` in the XZ plane, placed with the given basis.
Mat4 PlaneAt(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis) { return Mat4::FromBasis(x_axis, y_axis, z_axis, Vec3(0, 0, 0)); }

} // namespace

TEST_CASE("Two crossing planes resolve visibility along their line of intersection, in either draw order", "[rhi][vulkan][gpu][demo][depth]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    rhi::DevicePtr device = MakeDevice();
    if (device == nullptr) SKIP("no suitable Vulkan device");
    fx.ExpectErrors(0);
    {
        Rig rig;
        REQUIRE(rig.Create(device.Get()));
        MeshData d;
        REQUIRE(MakePlaneMesh(10.0f, 10.0f, 1, 1, &d) == nullptr);
        GpuMesh plane;
        REQUIRE(plane.Create(device.Get(), d, "plane"));
        rig.Prepare(&plane);
        const MaterialId mat = CreateGBufferMaterial(rig.sys, rig.templates, GBufferOptions{}, "m");
        REQUIRE(mat.IsValid());

        // A: the floor, normal +Y, at y = 0. B: a wall in the plane x = 0, normal +X (the local Y axis maps to +X).
        const Mat4 floor = PlaneAt(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1));
        const Mat4 wall = PlaneAt(Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1));
        Camera cam = MakeLookAtCamera(Vec3(3, 4, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
        cam.aspect = 1.0f;
        cam.near_plane = 0.5f;
        cam.far_plane = 30.0f;

        Layers results[2];
        for (int order = 0; order < 2; ++order) {
            RenderScene scene(RenderSceneDesc{});
            RenderObjectDesc od;
            od.mesh = &plane;
            od.material = mat.index;
            od.transform = order == 0 ? floor : wall;
            scene.AddObject(od);
            od.transform = order == 0 ? wall : floor;
            scene.AddObject(od);
            results[order] = rig.Render(scene, cam);
            REQUIRE(results[order].ok);
        }

        int probes = 0;
        for (const Layers& L : results) {
            for (f32 z = -3.0f; z <= 3.0f; z += 1.0f) {
                struct Probe {
                    Vec3 point;
                    Vec3 normal; // what the pixel must show
                    Vec3 surface_point; // where that surface is hit by the pixel's ray
                };
                // Visible floor beside the line, visible wall above it, the floor hidden behind the wall, the wall's lower half
                // hidden under the floor.
                const Probe list[] = {
                    {Vec3(0.6f, 0.0f, z), Vec3(0, 1, 0), Vec3(0.6f, 0.0f, z)},
                    {Vec3(0.0f, 0.6f, z), Vec3(1, 0, 0), Vec3(0.0f, 0.6f, z)},
                };
                for (const Probe& p : list) {
                    const Pixel px = Project(cam, p.point);
                    const Vec3 n = L.Normal(px.x, px.y);
                    INFO("probe " << p.point.x << " " << p.point.y << " " << p.point.z << " normal " << n.x << " " << n.y << " " << n.z);
                    CHECK(std::fabs(n.x - p.normal.x) < 0.02f);
                    CHECK(std::fabs(n.y - p.normal.y) < 0.02f);
                    CHECK(std::fabs(n.z - p.normal.z) < 0.02f);
                    // The stored depth is the depth of that surface point to within a pixel's worth of slope.
                    const Pixel on_surface = Project(cam, p.surface_point);
                    CHECK(std::fabs(L.Depth(px.x, px.y) - on_surface.depth) < 2e-3f);
                    ++probes;
                }
                // Occlusion: the floor point behind the wall (x < 0) shows the wall; the wall point under the floor (y < 0) shows
                // the floor.
                const Pixel behind = Project(cam, Vec3(-0.6f, 0.0f, z));
                const Vec3 nb = L.Normal(behind.x, behind.y);
                INFO("behind the wall, z " << z << " normal " << nb.x << " " << nb.y << " " << nb.z);
                CHECK(nb.x > 0.98f);
                const Pixel under = Project(cam, Vec3(0.0f, -0.6f, z));
                const Vec3 nu = L.Normal(under.x, under.y);
                INFO("under the floor, z " << z << " normal " << nu.x << " " << nu.y << " " << nu.z);
                CHECK(nu.y > 0.98f);
                ++probes;
            }
        }
        CHECK(probes >= 40);
        // Either draw order gives exactly the same depth and normals.
        CHECK(results[0].depth == results[1].depth);
        CHECK(results[0].normal == results[1].normal);
        device->WaitIdle();
    }
    device->WaitIdle();
    device.Reset();
}

#include "demo_assets.h"
#include "rhi/null/null_device.h"

namespace {
// Recorded from the generator (a deterministic LCG): a change here means the scene changed, which also changes the golden image.
constexpr f32 kExpectedShapeX = -33.3885f, kExpectedShapeY = 0.422003f, kExpectedShapeZ = -19.2028f;
constexpr f32 kExpectedLightX = -27.8331f, kExpectedLightY = 1.72851f, kExpectedLightZ = 9.54387f, kExpectedLightRadius = 4.86283f;
} // namespace

TEST_CASE("The bump normal map holds unit normals that tilt with the distance from a bump's center", "[rhi][demo]") {
    const demo::Pixels p = renderer_demo::MakeBumpNormalPixels();
    REQUIRE(p.width == 256);
    auto normal = [&p](u32 x, u32 y, f32* n) {
        const unsigned char* px = &p.rgba[(static_cast<size_t>(y) * p.width + x) * 4];
        for (int c = 0; c < 3; ++c) n[c] = static_cast<f32>(px[c]) / 255.0f * 2.0f - 1.0f;
    };
    // Every texel decodes to a unit vector facing the viewer (z >= 0), to within the 8-bit rounding.
    for (u32 y = 0; y < p.height; y += 7) {
        for (u32 x = 0; x < p.width; x += 5) {
            f32 n[3];
            normal(x, y, n);
            CHECK(std::fabs(std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) - 1.0f) < 0.02f);
            CHECK(n[2] >= -0.01f);
        }
    }
    // The cell is 64 texels. Texel 46 is at u = 0.453 of the bump's half-width; the bump is a sphere of radius 0.9 cell units, so
    // the normal's x is u / 0.9 = 0.503 and its z is sqrt(1 - x^2) (y is zero on the middle row).
    f32 n[3];
    normal(46, 32, &n[0]);
    CHECK(std::fabs(n[0] - 0.5035f) < 0.015f);
    CHECK(std::fabs(n[2] - std::sqrt(1.0f - 0.5035f * 0.5035f)) < 0.015f);
    // Between bumps (the corner of a cell, outside the radius) the normal is flat.
    normal(0, 0, &n[0]);
    CHECK(std::fabs(n[0]) < 0.02f);
    CHECK(n[2] > 0.98f);
}

TEST_CASE("The demo scene is the same on every compiler: fixed positions for given seeds", "[rhi][demo]") {
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    {
        renderer_demo::DemoFrame frame;
        renderer_demo::DemoScene scene;
        renderer_demo::FrameSetup fs;
        fs.device = device.Get();
        fs.max_objects = 600;
        fs.max_lights = 400;
        REQUIRE(frame.Create(fs));
        renderer_demo::SceneOptions so;
        so.asset_dir = nullptr; // procedural textures
        REQUIRE(scene.Create(device.Get(), frame.Materials(), frame.Templates(), so));
        CHECK(scene.ObjectCount() == 540);
        CHECK(scene.LightCount() == 302); // 300 point lights, a sun and ambient light
        // The first shape (object 49 after the 49 ground tiles) and the first point light (light 2).
        const RenderObject& shape = scene.Scene().ObjectAt(49);
        const Light& light = scene.Scene().LightAt(2);
        INFO("shape " << shape.transform.col[3].x << " " << shape.transform.col[3].y << " " << shape.transform.col[3].z);
        INFO("light " << light.position.x << " " << light.position.y << " " << light.position.z << " radius " << light.radius);
        CHECK(std::fabs(shape.transform.col[3].x - kExpectedShapeX) < 1e-3f);
        CHECK(std::fabs(shape.transform.col[3].y - kExpectedShapeY) < 1e-3f);
        CHECK(std::fabs(shape.transform.col[3].z - kExpectedShapeZ) < 1e-3f);
        CHECK(std::fabs(light.position.x - kExpectedLightX) < 1e-3f);
        CHECK(std::fabs(light.position.y - kExpectedLightY) < 1e-3f);
        CHECK(std::fabs(light.position.z - kExpectedLightZ) < 1e-3f);
        CHECK(std::fabs(light.radius - kExpectedLightRadius) < 1e-3f);
        // The same seed twice gives the same scene; another seed does not.
        renderer_demo::DemoScene again;
        REQUIRE(again.Create(device.Get(), frame.Materials(), frame.Templates(), so));
        CHECK(again.Scene().ObjectAt(49).transform.col[3].x == shape.transform.col[3].x);
        renderer_demo::DemoScene other;
        so.seed = 2;
        REQUIRE(other.Create(device.Get(), frame.Materials(), frame.Templates(), so));
        CHECK(other.Scene().ObjectAt(49).transform.col[3].x != shape.transform.col[3].x);
    }
    device.Reset();
}
