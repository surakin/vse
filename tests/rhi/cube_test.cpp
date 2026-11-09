#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "cube_fixture.h"
#include "cube_scene.h"
#include "backend_fixture.h"
#include "image_compare.h"
#include "render_target.h"
#include "rhi/rhi.h"
#include "rhi/uniform_ring.h"
#include "cstr/mem_ops.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using gpu_test::CompareOptions;
using gpu_test::Image;
using namespace cube_test;


TEST_CASE("Perspective and look-at follow the documented conventions", "[rhi][cube][math]") {
    using namespace demo;
    const Mat4 projection = PerspectiveVulkan(1.5707964f, 2.0f, 1.0f, 5.0f); // 90 degrees: f = 1
    // A point on the near plane maps to depth 0, on the far plane to depth 1.
    Vec4 near_clip = projection * Vec4(0, 0, -1, 1);
    Vec4 far_clip = projection * Vec4(0, 0, -5, 1);
    CHECK(Abs(near_clip.z / near_clip.w) < 1e-5f);
    CHECK(Abs(far_clip.z / far_clip.w - 1.0f) < 1e-5f);
    // Up in view space is negative clip Y (Vulkan's Y points down); right is positive X, scaled by the aspect.
    Vec4 up = projection * Vec4(0, 1, -2, 1);
    Vec4 right = projection * Vec4(2, 0, -2, 1);
    CHECK(up.y / up.w < 0.0f);
    CHECK(Abs(right.x / right.w - 0.5f) < 1e-5f); // x / (aspect * depth) = 2 / (2 * 2)
    // The camera looks down -Z of view space: the target lies on the negative Z axis at its distance.
    const Mat4 view = LookAt(Vec3(1, 2, 3), Vec3(1, 2, -1), Vec3(0, 1, 0));
    const Vec4 target = view * Vec4(1, 2, -1, 1);
    CHECK(Abs(target.x) < 1e-5f);
    CHECK(Abs(target.y) < 1e-5f);
    CHECK(Abs(target.z + 4.0f) < 1e-5f);
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

TEST_CASE("The cube renders from a fixed angle and matches its golden image", "[rhi][gpu][cube][golden]") {
    FOR_EACH_GPU_BACKEND(backend) {
        CubeScene scene(backend, Vec3(1.8f, 1.5f, 3.0f));
        demo::CubeOptions options;
        options.depth_format = scene.DepthFormat();
        demo::CubeRenderer cube;
        REQUIRE(cube.Create(scene.device.Get(), options));
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

        CheckGolden(image, "cube.ppm", "cube", backend);
        scene.device->WaitIdle();
    }
}

TEST_CASE("The depth test hides a cube behind another and its absence does not", "[rhi][gpu][cube]") {
    FOR_EACH_GPU_BACKEND(backend) {
        CubeScene scene(backend);
        demo::CubeOptions with_depth;
        with_depth.depth_format = scene.DepthFormat();
        demo::CubeOptions no_depth = with_depth;
        no_depth.depth_test = false;
        demo::CubeRenderer tested;
        demo::CubeRenderer untested;
        REQUIRE(tested.Create(scene.device.Get(), with_depth));
        REQUIRE(untested.Create(scene.device.Get(), no_depth));

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
}

TEST_CASE("Indexed and non-indexed draws of the same cube give identical images", "[rhi][gpu][cube]") {
    FOR_EACH_GPU_BACKEND(backend) {
        CubeScene scene(backend, Vec3(1.8f, 1.5f, 3.0f));
        demo::CubeOptions options;
        options.depth_format = scene.DepthFormat();
        demo::CubeRenderer indexed;
        demo::CubeRenderer expanded;
        options.indexed = true;
        REQUIRE(indexed.Create(scene.device.Get(), options));
        options.indexed = false;
        REQUIRE(expanded.Create(scene.device.Get(), options));
        const Mat4 model = demo::ModelMatrix(Vec3(0, 0, 0), 1.0f, 0.4f);
        const Image a = scene.Render({{&indexed, model}});
        const Image b = scene.Render({{&expanded, model}});
        REQUIRE(a.rgba.size() == b.rgba.size());
        CHECK(MemCompare(a.rgba.data(), b.rgba.data(), a.rgba.size()) == 0);
        scene.device->WaitIdle();
    }
}

TEST_CASE("Back faces are culled and the winding follows the documented convention", "[rhi][gpu][cube]") {
    FOR_EACH_GPU_BACKEND(backend) {
        CubeScene scene(backend);
        demo::CubeOptions options;
        options.depth_format = scene.DepthFormat();
        demo::CubeRenderer back;
        demo::CubeRenderer front;
        demo::CubeRenderer none;
        options.cull = CullMode::Back;
        REQUIRE(back.Create(scene.device.Get(), options));
        options.cull = CullMode::Front;
        REQUIRE(front.Create(scene.device.Get(), options));
        options.cull = CullMode::None;
        REQUIRE(none.Create(scene.device.Get(), options));
        const Mat4 model = Mat4::Identity();
        // Culling back faces leaves the near face; culling front faces leaves only the inside of the far face; with
        // no culling the depth test still shows the near face.
        CHECK(PixelIs(scene.Render({{&back, model}}), 64, 64, 128, 128, 255));
        CHECK(PixelIs(scene.Render({{&front, model}}), 64, 64, 128, 128, 0));
        CHECK(PixelIs(scene.Render({{&none, model}}), 64, 64, 128, 128, 255));
        scene.device->WaitIdle();
    }
}
