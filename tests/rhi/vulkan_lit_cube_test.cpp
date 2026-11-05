#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "cube_fixture.h"
#include "cube_scene.h"
#include "image_compare.h"
#include "textures.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace cube_test;
using gpu_test::CompareOptions;
using gpu_test::Image;

namespace {

struct LitScene : CubeScene {
    demo::CubeRenderer cube;
    TextureHandle white;
    SamplerHandle sampler;
    TextureHandle extra_texture; // a second albedo the test installs, owned (and destroyed) by the scene
    SamplerHandle extra_sampler;

    explicit LitScene(const Vec3& eye = Vec3(1.8f, 1.5f, 3.0f)) : CubeScene(eye) {
        demo::CubeOptions options;
        options.depth_format = DepthFormat();
        options.lit = true;
        REQUIRE(cube.Create(device.Get(), options));
        // A white albedo makes the lit color equal the lighting term itself.
        white = demo::CreateSampledTexture(device.Get(), demo::Solid(4, 4, {255, 255, 255, 255}), Format::RGBA8_UNorm, false, "white");
        REQUIRE(white.IsValid());
        sampler = device->CreateSampler(SamplerDesc{});
        REQUIRE(sampler.IsValid());
        cube.SetTexture(white, sampler);
    }
    ~LitScene() {
        if (device == nullptr) return;
        device->WaitIdle();
        if (extra_sampler.IsValid()) device->DestroySampler(extra_sampler);
        if (extra_texture.IsValid()) device->DestroyTexture(extra_texture);
        if (sampler.IsValid()) device->DestroySampler(sampler);
        if (white.IsValid()) device->DestroyTexture(white);
    }

    // Replaces the albedo; the scene destroys both objects.
    void UseAlbedo(TextureHandle texture, SamplerHandle new_sampler) {
        extra_texture = texture;
        extra_sampler = new_sampler;
        REQUIRE(extra_texture.IsValid());
        REQUIRE(extra_sampler.IsValid());
        cube.SetTexture(extra_texture, extra_sampler);
    }

    Image RenderLit(const demo::Lighting& lighting, const Mat4& model = Mat4::Identity()) { return Render({{&cube, model, &lighting}}); }

    void FaceCenter(const Vec3& p, u32* x, u32* y) const { Project(Mat4::Identity(), p, x, y); }
};

int Level(f32 v) { return static_cast<int>((v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v) * 255.0f + 0.5f); }

} // namespace

TEST_CASE("NormalMatrix keeps normals perpendicular under non-uniform scale", "[rhi][lit][math]") {
    using namespace demo;
    // Scale x by 2: a surface with normal (1,1,0) keeps a normal that is perpendicular to its tangent (1,-2,0)/(...).
    const Mat4 model = Mat4::FromBasis(Vec3(2, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(5, 6, 7));
    const Mat4 n = NormalMatrix(model);
    const Vec4 normal = n * Vec4(1, 1, 0, 0);
    const Vec4 tangent = model * Vec4(1, -1, 0, 0); // (1,-1,0) is perpendicular to (1,1,0) before scaling
    CHECK(std::abs(normal.x * tangent.x + normal.y * tangent.y + normal.z * tangent.z) < 1e-5f);
    // A mirroring model (negative determinant) must not turn normals inside out: the +X normal still points to +X after
    // mirroring Y, and a normal on the mirrored axis keeps its sense.
    const Mat4 mirror = Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, 0));
    const Mat4 mn = NormalMatrix(mirror);
    CHECK(mn.col[0].x > 0.0f);
    CHECK((mn * Vec4(0, 1, 0, 0)).y < 0.0f); // the surface was mirrored in Y, so its normal is mirrored too
    CHECK((mn * Vec4(1, 0, 0, 0)).x > 0.0f);
    // For a rotation it is the rotation itself (the translation is ignored).
    const Mat4 rotation = ModelMatrix(Vec3(1, 2, 3), 1.0f, 0.7f);
    const Mat4 rn = NormalMatrix(rotation);
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(rn.col[c].x - rotation.col[c].x) < 1e-5f);
        CHECK(std::abs(rn.col[c].y - rotation.col[c].y) < 1e-5f);
        CHECK(std::abs(rn.col[c].z - rotation.col[c].z) < 1e-5f);
    }
}

TEST_CASE("Visible faces are lit by the Lambert ratio of their normals to the light", "[rhi][vulkan][gpu][lit]") {
    LitScene scene;
    demo::Lighting lighting;
    lighting.to_light = Vec3(0.3f, 0.9f, 0.5f);
    lighting.intensity = 0.8f;
    lighting.ambient = Vec3(0.1f, 0.1f, 0.1f);
    const Image image = scene.RenderLit(lighting);

    const Vec3 l = lighting.to_light.Normalized();
    struct FaceProbe {
        Vec3 center;
        Vec3 normal;
    };
    const FaceProbe faces[] = {{Vec3(0, 0, 0.5f), Vec3(0, 0, 1)}, {Vec3(0.5f, 0, 0), Vec3(1, 0, 0)}, {Vec3(0, 0.5f, 0), Vec3(0, 1, 0)}};
    int levels[3] = {};
    for (int i = 0; i < 3; ++i) {
        const f32 lambert = faces[i].normal.Dot(l);
        const int want = Level(0.1f + 0.8f * (lambert > 0.0f ? lambert : 0.0f));
        u32 x = 0, y = 0;
        scene.FaceCenter(faces[i].center, &x, &y);
        const unsigned char* p = image.Pixel(x, y);
        INFO("face " << i << " expects " << want << ", got " << int(p[0]) << "," << int(p[1]) << "," << int(p[2]));
        CHECK(std::abs(p[0] - want) <= 2);
        CHECK(std::abs(p[1] - want) <= 2);
        CHECK(std::abs(p[2] - want) <= 2);
        levels[i] = p[0];
    }
    // The top face (normal +Y) points closest to this light, the +Z face is next, the +X face is dimmest.
    CHECK(levels[2] > levels[0]);
    CHECK(levels[0] > levels[1]);
    // The Lambert ratio between two faces: (level - ambient) is proportional to N.L.
    const f32 ratio = static_cast<f32>(levels[2] - Level(0.1f)) / static_cast<f32>(levels[0] - Level(0.1f));
    CHECK(std::abs(ratio - faces[2].normal.Dot(l) / faces[0].normal.Dot(l)) < 0.1f);
}

TEST_CASE("A face pointing away from the light gets only the ambient term, and zero intensity leaves ambient everywhere", "[rhi][vulkan][gpu][lit]") {
    LitScene scene;
    demo::Lighting away;
    away.to_light = Vec3(-1, -1, -1); // behind every visible face
    away.ambient = Vec3(0.2f, 0.1f, 0.3f);
    const Image dark = scene.RenderLit(away);
    demo::Lighting off;
    off.to_light = Vec3(0.3f, 0.9f, 0.5f);
    off.intensity = 0.0f;
    off.ambient = Vec3(0.2f, 0.1f, 0.3f);
    const Image ambient_only = scene.RenderLit(off);
    const Vec3 centers[] = {Vec3(0, 0, 0.5f), Vec3(0.5f, 0, 0), Vec3(0, 0.5f, 0)};
    for (const Vec3& c : centers) {
        u32 x = 0, y = 0;
        scene.FaceCenter(c, &x, &y);
        CHECK(PixelIs(dark, x, y, Level(0.2f), Level(0.1f), Level(0.3f)));
        CHECK(PixelIs(ambient_only, x, y, Level(0.2f), Level(0.1f), Level(0.3f)));
    }
}

TEST_CASE("A zero light direction means no direct light and does not poison the image", "[rhi][vulkan][gpu][lit]") {
    LitScene scene;
    demo::Lighting none;
    none.to_light = Vec3(0, 0, 0);
    none.ambient = Vec3(0.2f, 0.2f, 0.2f);
    const Image image = scene.RenderLit(none);
    u32 x = 0, y = 0;
    scene.FaceCenter(Vec3(0, 0, 0.5f), &x, &y);
    CHECK(PixelIs(image, x, y, Level(0.2f), Level(0.2f), Level(0.2f)));
}

TEST_CASE("The lit cube renders from a fixed angle and matches its golden image", "[rhi][vulkan][gpu][lit][golden]") {
    LitScene scene;
    // The textured, lit cube: a checkerboard albedo under the same fixed light as the analytic test.
    const demo::Pixels checker = demo::Checkerboard(64, 8, {235, 200, 90, 255}, {40, 60, 120, 255});
    SamplerDesc sd;
    sd.max_anisotropy = 16.0f;
    scene.UseAlbedo(demo::CreateSampledTexture(scene.device.Get(), checker, Format::RGBA8_SRGB, true, "checker"), scene.device->CreateSampler(sd));
    demo::Lighting lighting;
    lighting.to_light = Vec3(0.3f, 0.9f, 0.5f);
    lighting.intensity = 0.9f;
    const Image image = scene.RenderLit(lighting, demo::ModelMatrix(Vec3(0, 0, 0), 1.0f, 0.0f));

    // Independent of the golden: the corner is the clear color, and a texel of the +Z face's first cell (the sRGB-decoded
    // color 235,200,90) shows that color times the Lambert term of the +Z normal.
    CHECK(PixelIs(image, 2, 2, 51, 51, 51));
    u32 x = 0, y = 0;
    scene.FaceCenter(Vec3(-0.4375f, -0.4375f, 0.5f), &x, &y);
    const Vec3 l = lighting.to_light.Normalized();
    const f32 light = 0.1f + 0.9f * l.z;
    auto decode = [](int c) {
        const f32 v = static_cast<f32>(c) / 255.0f;
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    const unsigned char* p = image.Pixel(x, y);
    CHECK(std::abs(p[0] - Level(decode(235) * light)) <= 6);
    CHECK(std::abs(p[1] - Level(decode(200) * light)) <= 6);
    CHECK(std::abs(p[2] - Level(decode(90) * light)) <= 6);

    if (gpu_test::UpdatingGoldens()) {
        REQUIRE(gpu_test::WritePpm(gpu_test::GoldenPath("lit_cube.ppm").c_str(), image));
        WARN("ENGINE_RHI_UPDATE_GOLDEN=1: tests/rhi/golden/lit_cube.ppm was rewritten; inspect it before committing");
    } else {
        Image golden;
        REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("lit_cube.ppm").c_str(), &golden));
        const gpu_test::CompareResult result = gpu_test::Compare(golden, image, CompareOptions{}, "lit_cube");
        INFO(result.message);
        CHECK(result.same);
    }
}
