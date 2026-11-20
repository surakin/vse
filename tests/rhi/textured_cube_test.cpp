#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "io/file.h"
#include "cube_fixture.h"
#include "cube_scene.h"
#include "image_compare.h"
#include "test_text.h"
#include "rhi/null/null_device.h"
#include "textures.h"
#include "cstr/mem_ops.h"

using namespace vse;
using namespace vse::rhi;
using namespace cube_test;
using gpu_test::CompareOptions;
using gpu_test::Image;

namespace
{

// A face of the textured cube shows the texture times the face color (the normal mapped to 0..1). The +Z face
// the camera looks at has the tint (0.5, 0.5, 1).
int Tinted(int channel_value, int channel)
{
    return channel == 2 ? channel_value : (channel_value + 1) / 2;
}

struct TexturedScene : CubeScene
{
    demo::CubeRenderer cube;

    explicit TexturedScene(Backend backend, const Vec3& eye = Vec3(0, 0, 3), u32 size = 128)
        : CubeScene(backend, eye, size)
    {
        demo::CubeOptions options;
        options.depth_format = DepthFormat();
        options.textured = true;
        REQUIRE(cube.Create(device.Get(), options));
    }
    ~TexturedScene()
    {
        if (device)
        {
            device->WaitIdle();
        }
    }

    Image RenderWith(TextureHandle texture, SamplerHandle sampler, const Mat4& model = Mat4::Identity())
    {
        cube.SetTexture(texture, sampler);
        return Render({{&cube, model}});
    }

    // The pixel at the center of the +Z face's texture coordinates (u, v).
    void FacePixel(f32 u, f32 v, u32* x, u32* y) const
    {
        Project(Mat4::Identity(), Vec3(-0.5f + u, -0.5f + v, 0.5f), x, y);
    }
};

SamplerHandle NearestSampler(Device* device)
{
    SamplerDesc sd;
    sd.mag_filter = sd.min_filter = sd.mip_filter = FilterMode::Nearest;
    sd.address_u = sd.address_v = AddressMode::ClampToEdge;
    return device->CreateSampler(sd);
}

}  // namespace

TEST_CASE("A 2x2 texture with nearest filtering shows its four colors on a face", "[rhi][gpu][textured]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        TexturedScene scene(backend);
        // Texel (x, y) of the image is at u = (x + 0.5) / 2, v = (y + 0.5) / 2.
        demo::Pixels px;
        px.width = px.height = 2;
        const demo::Rgba colors[4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
        for (const demo::Rgba& c : colors)
        {
            px.rgba.Push(c.r);
            px.rgba.Push(c.g);
            px.rgba.Push(c.b);
            px.rgba.Push(c.a);
        }
        const TextureHandle texture = demo::CreateSampledTexture(scene.device.Get(), px, Format::RGBA8_UNorm, false,
                                                                 "quadrants");
        REQUIRE(texture.IsValid());
        const SamplerHandle sampler = NearestSampler(scene.device.Get());
        REQUIRE(sampler.IsValid());
        const Image image = scene.RenderWith(texture, sampler);
        for (u32 i = 0; i < 4; ++i)
        {
            u32 x = 0, y = 0;
            const f32 column = static_cast<f32>(i % 2);
            const u32 grid_row = i / 2;  // the integer division is the point: a row of the 2x2 grid
            const f32 row = static_cast<f32>(grid_row);
            scene.FacePixel((column + 0.5f) / 2.0f, (row + 0.5f) / 2.0f, &x, &y);
            INFO("quadrant " << i << " at pixel " << x << "," << y);
            CHECK(PixelIs(image, x, y, Tinted(colors[i].r, 0), Tinted(colors[i].g, 1), Tinted(colors[i].b, 2)));
        }
        scene.device->WaitIdle();
        scene.device->DestroySampler(sampler);
        scene.device->DestroyTexture(texture);
    }
}

TEST_CASE("An sRGB texture is decoded to linear when sampled and a UNORM one is not", "[rhi][gpu][textured]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        TexturedScene scene(backend);
        const demo::Pixels gray = demo::Solid(4, 4, {128, 128, 128, 255});
        const TextureHandle linear = demo::CreateSampledTexture(scene.device.Get(), gray, Format::RGBA8_UNorm, false,
                                                                "gray unorm");
        const TextureHandle srgb = demo::CreateSampledTexture(scene.device.Get(), gray, Format::RGBA8_SRGB, false,
                                                              "gray srgb");
        REQUIRE(linear.IsValid());
        REQUIRE(srgb.IsValid());
        const SamplerHandle sampler = NearestSampler(scene.device.Get());
        // The color target is UNORM, so what the shader computes is what is stored. Mid-gray 128/255 is 0.502 as UNORM
        // and 0.2158 once sRGB-decoded; times the +Z face tint (0.5, 0.5, 1).
        const Image a = scene.RenderWith(linear, sampler);
        CHECK(PixelIs(a, 64, 64, 64, 64, 128));
        const Image b = scene.RenderWith(srgb, sampler);
        CHECK(PixelIs(b, 64, 64, 27, 27, 55));
        scene.device->WaitIdle();
        scene.device->DestroySampler(sampler);
        scene.device->DestroyTexture(linear);
        scene.device->DestroyTexture(srgb);
    }
}

TEST_CASE("Generated mips average a minified checkerboard and a single level does not", "[rhi][gpu][textured]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        TexturedScene scene(backend);
        const demo::Pixels checker = demo::Checkerboard(256, 1, {0, 0, 0, 255}, {255, 255, 255, 255});
        const TextureHandle mipped = demo::CreateSampledTexture(scene.device.Get(), checker, Format::RGBA8_UNorm, true,
                                                                "checker mips");
        const TextureHandle single = demo::CreateSampledTexture(scene.device.Get(), checker, Format::RGBA8_UNorm, false,
                                                                "checker single");
        REQUIRE(mipped.IsValid());
        REQUIRE(single.IsValid());
        SamplerDesc sd;  // trilinear, repeat
        const SamplerHandle sampler = scene.device->CreateSampler(sd);

        // Over a patch of the face (about five texels per pixel at this size and distance), compare the brightest and
        // darkest red values.
        auto spread = [&](const Image& image, int* low, int* high)
        {
            *low = 255;
            *high = 0;
            for (u32 y = 56; y < 72; ++y)
            {
                for (u32 x = 56; x < 72; ++x)
                {
                    const int red = image.Pixel(x, y)[0] * 2;  // undo the 0.5 face tint
                    *low = red < *low ? red : *low;
                    *high = red > *high ? red : *high;
                }
            }
        };
        int low = 0, high = 0;
        spread(scene.RenderWith(mipped, sampler), &low, &high);
        INFO("with mips: " << low << ".." << high);
        CHECK(low >= 110);  // every pixel is near mid-gray (127.5)
        CHECK(high <= 145);
        spread(scene.RenderWith(single, sampler), &low, &high);
        INFO("without mips: " << low << ".." << high);
        CHECK(high - low >= 100);  // aliased: black and white pixels remain

        scene.device->WaitIdle();
        scene.device->DestroySampler(sampler);
        scene.device->DestroyTexture(mipped);
        scene.device->DestroyTexture(single);
    }
}

TEST_CASE("The textured cube renders from a fixed angle and matches its golden image", "[rhi][gpu][textured][golden]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        TexturedScene scene(backend, Vec3(1.8f, 1.5f, 3.0f));
        const demo::Pixels checker = demo::Checkerboard(64, 8, {230, 60, 60, 255}, {60, 60, 230, 255});
        const TextureHandle texture = demo::CreateSampledTexture(scene.device.Get(), checker, Format::RGBA8_SRGB, true,
                                                                 "checker");
        REQUIRE(texture.IsValid());
        SamplerDesc sd;  // trilinear with anisotropy
        sd.max_anisotropy = 16.0f;
        const SamplerHandle sampler = scene.device->CreateSampler(sd);
        const Image image = scene.RenderWith(texture, sampler);

        // The +Z face shows texture cell (0,0) = A and (1,0) = B at these coordinates; the sRGB decode and the 0.5,
        // 0.5, 1 face tint are applied by hand: sRGB(230) = 0.791, sRGB(60) = 0.045.
        auto decode = [](int c)
        {
            const f32 v = static_cast<f32>(c) / 255.0f;
            return v <= 0.04045f ? v / 12.92f : Pow((v + 0.055f) / 1.055f, 2.4f);
        };
        auto expect = [&](f32 u, f32 v, const demo::Rgba& c)
        {
            u32 x = 0, y = 0;
            scene.FacePixel(u, v, &x, &y);
            const u8* p = image.Pixel(x, y);
            INFO("texture at " << u << "," << v << " -> pixel " << x << "," << y);
            const f32 want[3] = {decode(c.r) * 0.5f, decode(c.g) * 0.5f, decode(c.b) * 1.0f};
            for (int k = 0; k < 3; ++k)
            {
                CHECK(Abs(p[k] - static_cast<int>(want[k] * 255.0f + 0.5f)) <= 8);
            }
        };
        expect(1.0f / 16.0f, 1.0f / 16.0f, {230, 60, 60, 255});
        expect(3.0f / 16.0f, 1.0f / 16.0f, {60, 60, 230, 255});
        expect(3.0f / 16.0f, 3.0f / 16.0f, {230, 60, 60, 255});

        // Shading and texture filtering round differently between GPU vendors (the goldens come from NVIDIA; AMD
        // differs by 3-10 levels on a handful of pixels), so a small fraction of interior pixels may miss `tolerance`.
        CompareOptions options;
        options.max_interior_miss = 0.005;
        options.interior_miss_limit = 16;  // AMD's worst was 10 levels; a real shading error is larger
        CheckGolden(image, "textured_cube.ppm", "textured_cube", backend, options);
        scene.device->WaitIdle();
        scene.device->DestroySampler(sampler);
        scene.device->DestroyTexture(texture);
    }
}

// ---- the .rtex container and the procedural textures (no GPU) ----

TEST_CASE("Procedural textures have the documented content", "[rhi][textured][image]")
{
    const demo::Pixels checker = demo::Checkerboard(4, 2, {10, 20, 30, 255}, {200, 210, 220, 255});
    REQUIRE(checker.rgba.Size() == 4 * 4 * 4);
    CHECK(checker.rgba[0] == 10);                 // (0,0) is the first color
    CHECK(checker.rgba[(0 * 4 + 2) * 4] == 200);  // two texels right: the second color
    CHECK(checker.rgba[(2 * 4 + 0) * 4] == 200);  // two rows down: the second color
    CHECK(checker.rgba[(2 * 4 + 2) * 4] == 10);
    const demo::Pixels gradient = demo::UvGradient(3, 2);
    CHECK(gradient.rgba[0] == 0);
    CHECK(gradient.rgba[(0 * 3 + 2) * 4] == 255);      // red reaches 255 at the last column
    CHECK(gradient.rgba[(1 * 3 + 0) * 4 + 1] == 255);  // green reaches 255 at the last row
    CHECK(demo::FullMipCount(256, 64) == 9);
}

TEST_CASE("sRGB mips are averaged as linear light and one-level images need no mip pass", "[rhi][textured][image]")
{
    // Black and white average to 50% linear light, which is 188 once sRGB-encoded (not 128).
    const demo::RtexImage srgb = demo::BuildRtex(demo::Checkerboard(4, 1, {0, 0, 0, 255}, {255, 255, 255, 255}),
                                                 Format::RGBA8_SRGB);
    CHECK(Abs(static_cast<int>(srgb.data[4 * 4 * 4]) - 188) <= 1);
    CHECK(srgb.data[4 * 4 * 4 + 3] == 255);  // alpha is averaged as it is
    const demo::RtexImage linear = demo::BuildRtex(demo::Checkerboard(4, 1, {0, 0, 0, 255}, {255, 255, 255, 255}),
                                                   Format::RGBA8_UNorm);
    CHECK(linear.data[4 * 4 * 4] == 128);

    // A 1x1 image has no mips to generate: asking for them must not abort.
    NullDevicePtr device = CreateNullDevice();
    const TextureHandle one = demo::CreateSampledTexture(device.Get(), demo::Solid(1, 1, {10, 20, 30, 255}),
                                                         Format::RGBA8_UNorm, true, "1x1");
    CHECK(one.IsValid());
    // Formats the helpers cannot treat as four 8-bit channels are refused.
    CHECK_FALSE(
        demo::CreateSampledTexture(device.Get(), demo::Solid(1, 1, {0, 0, 0, 0}), Format::R32_Float, false, "float")
            .IsValid());
    CHECK(device->ErrorCount() == 0);
    device.Reset();
}

TEST_CASE("An rtex file round-trips and damaged ones are refused", "[rhi][textured][image]")
{
    const demo::Pixels checker = demo::Checkerboard(16, 1, {0, 0, 0, 255}, {255, 255, 255, 255});
    const demo::RtexImage built = demo::BuildRtex(checker, Format::RGBA8_UNorm);
    CHECK(built.mip_count == 5);
    CHECK(built.data.Size() == (16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1) * 4);
    // A 1-texel checker averages to mid-gray from the first mip on.
    CHECK(built.data[16 * 16 * 4] == 128);
    const engine_test::Text path_text = engine_test::MakeText(StringView(gpu_test::TempPath("roundtrip.rtex").CStr()));
    const char* path = path_text.CStr();
    REQUIRE(demo::WriteRtex(io::Path(StringView(path)), built).Ok());
    demo::RtexImage back;
    REQUIRE(demo::ReadRtex(io::Path(StringView(path)), &back).Ok());
    CHECK(back.width == 16);
    CHECK(back.mip_count == 5);
    CHECK(back.format == Format::RGBA8_UNorm);
    REQUIRE(back.data.Size() == built.data.Size());
    CHECK(MemCompare(back.data.Data(), built.data.Data(), back.data.Size()) == 0);

    auto write = [](const char* p, const u8* bytes, usize n)
    { REQUIRE(io::WriteFile(io::Path(StringView(p)), bytes, n).Ok()); };
    // Truncated payload.
    Vector<u8> good;
    REQUIRE(io::ReadFile(io::Path(StringView(path)), &good).Ok());
    write(gpu_test::TempPath("rhi_test_short.rtex").CStr(), good.Data(), good.Size() - 1);
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_test_short.rtex").CStr())), &back).Failed());
    // A wrong magic, an unknown format, a mip count beyond the chain, and a huge size over a tiny file.
    Vector<u8> bad;
    auto reset_bad = [&]()
    {
        bad.Resize(good.Size());
        MemCopy(bad.Data(), good.Data(), good.Size());
    };
    reset_bad();
    bad[0] = 'X';
    write(gpu_test::TempPath("rhi_test_magic.rtex").CStr(), bad.Data(), bad.Size());
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_test_magic.rtex").CStr())), &back).Failed());
    reset_bad();
    bad[16] = 0xff;
    write(gpu_test::TempPath("rhi_test_format.rtex").CStr(), bad.Data(), bad.Size());
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_test_format.rtex").CStr())), &back).Failed());
    reset_bad();
    bad[20] = 40;
    write(gpu_test::TempPath("rhi_test_mips.rtex").CStr(), bad.Data(), bad.Size());
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_test_mips.rtex").CStr())), &back).Failed());
    reset_bad();
    bad[9] = 0x40;  // width 16 + 0x4000
    write(gpu_test::TempPath("rhi_test_huge.rtex").CStr(), bad.Data(), 24);
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_test_huge.rtex").CStr())), &back).Failed());
    CHECK(demo::ReadRtex(io::Path(StringView(gpu_test::TempPath("rhi_no_such.rtex").CStr())), &back).Failed());
}

TEST_CASE("The demo asset is a valid rtex file", "[rhi][textured][image]")
{
    demo::RtexImage asset;
    const engine_test::Text path = engine_test::MakeText(StringView(ENGINE_RHI_DEMO_ASSET_DIR "/checker.rtex"));
    if (gpu_test::UpdatingGoldens())
    {
        // The same switch that regenerates goldens regenerates the demo asset: a 256x256 sRGB checkerboard.
        const demo::Pixels checker = demo::Checkerboard(256, 32, {235, 200, 90, 255}, {40, 60, 120, 255});
        REQUIRE(demo::WriteRtex(io::Path(path.View()), demo::BuildRtex(checker, Format::RGBA8_SRGB)).Ok());
        WARN("ENGINE_RHI_UPDATE_GOLDEN=1: game/rhi_demo/assets/checker.rtex was rewritten");
    }
    REQUIRE(demo::ReadRtex(io::Path(path.View()), &asset).Ok());
    CHECK(asset.width == 256);
    CHECK(asset.format == Format::RGBA8_SRGB);
    CHECK(asset.mip_count == 9);
}
