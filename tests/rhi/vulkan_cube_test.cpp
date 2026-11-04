#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "cube_scene.h"
#include "gpu_fixture.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;
using gpu_test::CompareOptions;
using gpu_test::Image;

namespace {

constexpr u32 kSize = 128;

struct Draw {
    demo::CubeRenderer* cube;
    Mat4 model;
};

// A device with a 128x128 color target and a depth buffer, and a way to render cubes into them.
struct CubeScene {
    gpu_test::GpuFixture fx;
    VulkanDevice* device = nullptr;
    std::unique_ptr<gpu_test::RenderTarget> target;
    TextureHandle depth;
    std::unique_ptr<UniformRing> ring;
    Mat4 view_projection;

    CubeScene(const Vec3& eye = Vec3(0, 0, 3)) {
        fx.RequireValidation();
        DeviceCreateInfo info;
        info.validation = ValidationSetting::On;
        info.sync_validation = true;
        info.pipeline_cache = false;
        device = VulkanDevice::Create(info);
        if (device == nullptr) {
            SKIP("no suitable Vulkan device");
        }
        target = std::make_unique<gpu_test::RenderTarget>(device, kSize, kSize);
        TextureDesc td;
        td.width = kSize;
        td.height = kSize;
        td.format = device->Caps().depth_format;
        td.usage = TextureUsage::DepthAttachment;
        depth = device->CreateTexture(td);
        ring = std::make_unique<UniformRing>(device, 4096);
        SetCamera(eye);
    }
    ~CubeScene() {
        if (device == nullptr) {
            return;
        }
        device->WaitIdle();
        ring.reset();
        if (depth.IsValid()) device->DestroyTexture(depth);
        target.reset();
        DestroyDevice(device);
    }

    void SetCamera(const Vec3& eye) {
        view_projection = demo::PerspectiveVulkan(0.7853982f, 1.0f, 0.1f, 10.0f) * demo::LookAt(eye, Vec3(0, 0, 0), Vec3(0, 1, 0));
    }

    Format DepthFormat() const { return device->Caps().depth_format; }

    // Renders the draws in order over a gray background; returns the image.
    Image Render(const std::vector<Draw>& draws) {
        REQUIRE(target->Valid());
        REQUIRE(depth.IsValid());
        REQUIRE(ring->Valid());
        device->BeginFrame();
        ring->BeginFrame();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        // Each renderer is prepared once per list, however many cubes it draws.
        for (usize i = 0; i < draws.size(); ++i) {
            bool seen = false;
            for (usize j = 0; j < i; ++j) seen = seen || draws[j].cube == draws[i].cube;
            if (!seen) draws[i].cube->Prepare(list);
        }
        list->Barrier(target->Texture(), ResourceState::Undefined, ResourceState::ColorAttachment);
        list->Barrier(depth, ResourceState::Undefined, ResourceState::DepthAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = target->Texture();
        pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.2f;
        pass.depth.texture = depth;
        pass.depth.clear_depth = 1.0f;
        pass.depth.store = StoreOp::DontCare;
        list->BeginRendering(pass);
        for (const Draw& d : draws) {
            REQUIRE(d.cube->Draw(list, ring.get(), view_projection * d.model, kSize, kSize));
        }
        list->EndRendering();
        list->End();
        ring->Flush();
        CommandList* lists[] = {list};
        const bool submitted = device->Submit(lists, 1) != 0;
        device->EndFrame();
        REQUIRE(submitted);
        for (const Draw& d : draws) d.cube->Submitted();
        device->WaitIdle();
        Image image;
        REQUIRE(target->Readback(&image));
        return image;
    }

    // The pixel the view-projection puts the model-space point `p` (through `model`) on.
    void Project(const Mat4& model, const Vec3& p, u32* x, u32* y) const {
        const Vec4 clip = view_projection * (model * Vec4(p.x, p.y, p.z, 1.0f));
        REQUIRE(clip.w > 0.0f); // in front of the camera
        const f32 px = (clip.x / clip.w + 1.0f) * 0.5f * kSize;
        const f32 py = (clip.y / clip.w + 1.0f) * 0.5f * kSize;
        REQUIRE((px >= 0.0f && px < kSize && py >= 0.0f && py < kSize)); // on screen
        *x = static_cast<u32>(px);
        *y = static_cast<u32>(py);
    }
};

bool PixelIs(const Image& image, u32 x, u32 y, int r, int g, int b) {
    const unsigned char* p = image.Pixel(x, y);
    return std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2;
}

} // namespace

TEST_CASE("Perspective and look-at follow the documented conventions", "[rhi][cube][math]") {
    using namespace demo;
    const Mat4 projection = PerspectiveVulkan(1.5707964f, 2.0f, 1.0f, 5.0f); // 90 degrees: f = 1
    // A point on the near plane maps to depth 0, on the far plane to depth 1.
    Vec4 near_clip = projection * Vec4(0, 0, -1, 1);
    Vec4 far_clip = projection * Vec4(0, 0, -5, 1);
    CHECK(std::abs(near_clip.z / near_clip.w) < 1e-5f);
    CHECK(std::abs(far_clip.z / far_clip.w - 1.0f) < 1e-5f);
    // Up in view space is negative clip Y (Vulkan's Y points down); right is positive X, scaled by the aspect.
    Vec4 up = projection * Vec4(0, 1, -2, 1);
    Vec4 right = projection * Vec4(2, 0, -2, 1);
    CHECK(up.y / up.w < 0.0f);
    CHECK(std::abs(right.x / right.w - 0.5f) < 1e-5f); // x / (aspect * depth) = 2 / (2 * 2)
    // The camera looks down -Z of view space: the target lies on the negative Z axis at its distance.
    const Mat4 view = LookAt(Vec3(1, 2, 3), Vec3(1, 2, -1), Vec3(0, 1, 0));
    const Vec4 target = view * Vec4(1, 2, -1, 1);
    CHECK(std::abs(target.x) < 1e-5f);
    CHECK(std::abs(target.y) < 1e-5f);
    CHECK(std::abs(target.z + 4.0f) < 1e-5f);
    // The mesh: 24 vertices, 36 indices in range, every face CCW seen from outside (normal . (b-a)x(c-a) > 0).
    const CubeVertex* v = CubeVertices();
    const u16* idx = CubeIndices();
    for (u32 t = 0; t < kCubeIndexCount / 3; ++t) {
        const CubeVertex& a = v[idx[t * 3]];
        const CubeVertex& b = v[idx[t * 3 + 1]];
        const CubeVertex& c = v[idx[t * 3 + 2]];
        const Vec3 ab(b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]);
        const Vec3 ac(c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]);
        const Vec3 n = ab.Cross(ac);
        CHECK(n.x * a.normal[0] + n.y * a.normal[1] + n.z * a.normal[2] > 0.0f);
    }
}

TEST_CASE("The cube renders from a fixed angle and matches its golden image", "[rhi][vulkan][gpu][cube][golden]") {
    CubeScene scene(Vec3(1.8f, 1.5f, 3.0f));
    demo::CubeOptions options;
    options.depth_format = scene.DepthFormat();
    demo::CubeRenderer cube;
    REQUIRE(cube.Create(scene.device, options));
    const Mat4 model = Mat4::Identity();
    const Image image = scene.Render({{&cube, model}});

    // Independent of the golden: the corner is background; the center of each visible face shows that face's color
    // (the normal mapped to 0..1); the faces facing away are not drawn.
    CHECK(PixelIs(image, 2, 2, 51, 51, 51));
    struct FaceProbe {
        Vec3 center;
        int r, g, b;
    };
    const FaceProbe faces[] = {{Vec3(0, 0, 0.5f), 128, 128, 255}, {Vec3(0.5f, 0, 0), 255, 128, 128}, {Vec3(0, 0.5f, 0), 128, 255, 128}};
    for (const FaceProbe& f : faces) {
        u32 x = 0, y = 0;
        scene.Project(model, f.center, &x, &y);
        INFO("face at " << f.center.x << "," << f.center.y << "," << f.center.z << " projects to " << x << "," << y);
        CHECK(PixelIs(image, x, y, f.r, f.g, f.b));
    }

    if (gpu_test::UpdatingGoldens()) {
        REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("cube.ppm").c_str(), image));
        WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/cube.ppm was rewritten; inspect it before committing");
    } else {
        Image golden;
        REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("cube.ppm").c_str(), &golden));
        const gpu_test::CompareResult result = gpu_test::Compare(golden, image, CompareOptions{}, "cube");
        INFO(result.message);
        CHECK(result.same);
    }
    scene.device->WaitIdle();
}

TEST_CASE("The depth test hides a cube behind another and its absence does not", "[rhi][vulkan][gpu][cube]") {
    CubeScene scene;
    demo::CubeOptions with_depth;
    with_depth.depth_format = scene.DepthFormat();
    demo::CubeOptions no_depth = with_depth;
    no_depth.depth_test = false;
    demo::CubeRenderer tested;
    demo::CubeRenderer untested;
    REQUIRE(tested.Create(scene.device, with_depth));
    REQUIRE(untested.Create(scene.device, no_depth));

    // A is the unit cube facing the camera (+Z face, 128,128,255). B is a larger cube behind it, turned around so
    // that its -Z face (128,128,0) faces the camera; it fills the whole view around A.
    const Mat4 near_cube = Mat4::Identity();
    const Mat4 far_cube = demo::ModelMatrix(Vec3(0, 0, -2), 3.0f, 3.14159265f);

    const Image hidden = scene.Render({{&tested, near_cube}, {&tested, far_cube}});
    CHECK(PixelIs(hidden, 64, 64, 128, 128, 255)); // A is in front, B is occluded behind it
    CHECK(PixelIs(hidden, 4, 4, 128, 128, 0));     // B shows around A

    const Image overdrawn = scene.Render({{&untested, near_cube}, {&untested, far_cube}});
    CHECK(PixelIs(overdrawn, 64, 64, 128, 128, 0)); // without depth testing the later draw wins
    CHECK(PixelIs(overdrawn, 4, 4, 128, 128, 0));

    scene.device->WaitIdle();
}

TEST_CASE("Indexed and non-indexed draws of the same cube give identical images", "[rhi][vulkan][gpu][cube]") {
    CubeScene scene(Vec3(1.8f, 1.5f, 3.0f));
    demo::CubeOptions options;
    options.depth_format = scene.DepthFormat();
    demo::CubeRenderer indexed;
    demo::CubeRenderer expanded;
    options.indexed = true;
    REQUIRE(indexed.Create(scene.device, options));
    options.indexed = false;
    REQUIRE(expanded.Create(scene.device, options));
    const Mat4 model = demo::ModelMatrix(Vec3(0, 0, 0), 1.0f, 0.4f);
    const Image a = scene.Render({{&indexed, model}});
    const Image b = scene.Render({{&expanded, model}});
    REQUIRE(a.rgba.size() == b.rgba.size());
    CHECK(a.rgba == b.rgba);
    scene.device->WaitIdle();
}

TEST_CASE("Back faces are culled and the winding follows the documented convention", "[rhi][vulkan][gpu][cube]") {
    CubeScene scene;
    demo::CubeOptions options;
    options.depth_format = scene.DepthFormat();
    demo::CubeRenderer back;
    demo::CubeRenderer front;
    demo::CubeRenderer none;
    options.cull = CullMode::Back;
    REQUIRE(back.Create(scene.device, options));
    options.cull = CullMode::Front;
    REQUIRE(front.Create(scene.device, options));
    options.cull = CullMode::None;
    REQUIRE(none.Create(scene.device, options));
    const Mat4 model = Mat4::Identity();
    // Culling back faces leaves the near face; culling front faces leaves only the inside of the far face; with
    // no culling the depth test still shows the near face.
    CHECK(PixelIs(scene.Render({{&back, model}}), 64, 64, 128, 128, 255));
    CHECK(PixelIs(scene.Render({{&front, model}}), 64, 64, 128, 128, 0));
    CHECK(PixelIs(scene.Render({{&none, model}}), 64, 64, 128, 128, 255));
    scene.device->WaitIdle();
}
