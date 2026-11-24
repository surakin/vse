// Cube maps as assets (Phase 28 Day 480): the face geometry, the equirectangular conversion, minting a cube from six
// half-float faces or one latitude-longitude image through the texture source, hostile input, determinism, and the
// loaded GPU cube.

#include <catch2/catch_test_macros.hpp>

#include "asset/cube_map.h"
#include "asset/texture.h"
#include "asset/texture_mint.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "support.h"
#include "texture_source.h"

namespace vse::asset
{

namespace
{

using test::AsBytes;

constexpr f32 kCmPi = 3.14159265358979f;

u32 CmHalf(f32 value)
{
    return FloatToHalfBits(value);
}

f32 CmFloat(const byte* p)
{
    f32 value = 0.0f;
    REQUIRE(HalfBitsToFloat(static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8), &value));
    return value;
}

void CmPut(Vector<byte>* out, usize texel, f32 r, f32 g, f32 b, f32 a)
{
    const f32 channels[4] = {r, g, b, a};
    for (u32 c = 0; c < 4; ++c)
    {
        const u32 half = CmHalf(channels[c]);
        (*out)[texel * 8 + c * 2] = static_cast<byte>(half & 255);
        (*out)[texel * 8 + c * 2 + 1] = static_cast<byte>(half >> 8);
    }
}

// An equirectangular image whose color encodes its own direction: rgb = direction * 0.5 + 0.5.
Vector<byte> CmDirectionImage(u32 width, u32 height)
{
    Vector<byte> image;
    image.Resize(static_cast<usize>(width) * height * 8);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const f32 lon = ((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width) - 0.5f) * 2.0f * kCmPi;
            const f32 colat = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(height) * kCmPi;
            const f32 dx = Sin(colat) * Sin(lon);
            const f32 dy = Cos(colat);
            const f32 dz = -Sin(colat) * Cos(lon);
            CmPut(&image, static_cast<usize>(y) * width + x, dx * 0.5f + 0.5f, dy * 0.5f + 0.5f, dz * 0.5f + 0.5f,
                  1.0f);
        }
    }
    return image;
}

// An image from integer arithmetic only, so its bytes (and the pinned hash of a cube made from it) do not depend on the
// machine's math library.
Vector<byte> CmPatternImage(u32 width, u32 height)
{
    Vector<byte> image;
    image.Resize(static_cast<usize>(width) * height * 8);
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            CmPut(&image, static_cast<usize>(y) * width + x, static_cast<f32>((x * 3 + y * 5) % 17) * 0.25f,
                  static_cast<f32>((x * 7 + y) % 13) * 0.5f, static_cast<f32>((x + y * 11) % 5), 1.0f);
        }
    }
    return image;
}

Vector<byte> CmRawHalf(u32 w, u32 h, const Vector<byte>& rgba16f)
{
    Vector<byte> out;
    REQUIRE(BuildRawHalfImage(w, h, ByteSpan(rgba16f.Data(), rgba16f.Size()), 0, &out).Ok());
    return out;
}

Status CmMint(const char* text, const Vector<Vector<byte>>& images, Vector<byte>* out, TextureSourceError* error)
{
    TypeRegistry types;
    Vector<ByteSpan> spans;
    for (usize i = 0; i < images.Size(); ++i)
    {
        spans.Push(ByteSpan(images[i].Data(), images[i].Size()));
    }
    return MintTexture(StringView(text, CStrLength(text)), Span<const ByteSpan>(spans.Data(), spans.Size()), types, out,
                       error, 0);
}

u64 CmFnv(const Vector<byte>& bytes)
{
    u64 h = 0xcbf29ce484222325ull;
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        h = (h ^ bytes[i]) * 0x100000001b3ull;
    }
    return h;
}

}  // namespace

TEST_CASE("cube map: every face looks along its own axis and the corners stay on the face", "[asset][cube]")
{
    const Vec3 axes[6] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};
    for (u32 face = 0; face < 6; ++face)
    {
        const Vec3 center = CubeFaceDirection(face, 0.0f, 0.0f);
        CHECK(center.x == axes[face].x);
        CHECK(center.y == axes[face].y);
        CHECK(center.z == axes[face].z);
        const f32 us[3] = {-1.0f, 0.0f, 1.0f};
        for (f32 u : us)
        {
            for (f32 v : us)
            {
                const Vec3 d = CubeFaceDirection(face, u, v);
                const f32 along = d.x * axes[face].x + d.y * axes[face].y + d.z * axes[face].z;
                CHECK(along == 1.0f);  // the major axis component is 1, the others stay within [-1, 1]
                CHECK(Abs(d.x) <= 1.0f);
                CHECK(Abs(d.y) <= 1.0f);
                CHECK(Abs(d.z) <= 1.0f);
            }
        }
    }
    // The faces' u and v directions: +Z looks along +Z with u to +X and v down (-Y); +Y has v toward +Z.
    CHECK(CubeFaceDirection(4, 1.0f, 0.0f).x == 1.0f);
    CHECK(CubeFaceDirection(4, 0.0f, 1.0f).y == -1.0f);
    CHECK(CubeFaceDirection(2, 0.0f, 1.0f).z == 1.0f);
    CHECK(CubeFaceDirection(0, 1.0f, 0.0f).z == -1.0f);
}

TEST_CASE("cube map: the polynomial atan2 stays within 2e-5 radians of the library one", "[asset][cube]")
{
    f32 worst = 0.0f;
    for (i32 yi = -20; yi <= 20; ++yi)
    {
        for (i32 xi = -20; xi <= 20; ++xi)
        {
            const f32 y = static_cast<f32>(yi) * 0.37f;
            const f32 x = static_cast<f32>(xi) * 0.41f;
            if (x == 0.0f && y == 0.0f)
            {
                CHECK(DeterministicAtan2(y, x) == 0.0f);
                continue;
            }
            const f32 diff = Abs(DeterministicAtan2(y, x) - Atan2(y, x));
            worst = diff > worst ? diff : worst;
        }
    }
    CHECK(worst < 2e-5f);
}

TEST_CASE("cube map: half floats convert exactly and round to nearest even", "[asset][cube]")
{
    f32 value = 0.0f;
    CHECK(HalfBitsToFloat(0x3c00, &value));
    CHECK(value == 1.0f);
    CHECK(HalfBitsToFloat(0xc000, &value));
    CHECK(value == -2.0f);
    CHECK(HalfBitsToFloat(0x0001, &value));
    CHECK(value == 1.0f / 16777216.0f);
    CHECK_FALSE(HalfBitsToFloat(0x7c00, &value));  // infinity
    CHECK_FALSE(HalfBitsToFloat(0x7e00, &value));  // NaN
    CHECK(FloatToHalfBits(1.0f) == 0x3c00);
    CHECK(FloatToHalfBits(-2.0f) == 0xc000);
    CHECK(FloatToHalfBits(65504.0f) == 0x7bff);
    CHECK(FloatToHalfBits(1.0e9f) == 0x7bff);  // clamped, never infinite
    CHECK(FloatToHalfBits(0.0f) == 0);
    CHECK(FloatToHalfBits(1.0f + 1.0f / 2048.0f) == 0x3c00);  // a tie rounds to even (down)
    CHECK(FloatToHalfBits(1.0f + 3.0f / 2048.0f) == 0x3c02);  // a tie rounds to even (up)
    for (u32 half = 0; half < 0x7c00; half += 7)              // every finite positive half survives the round trip
    {
        REQUIRE(HalfBitsToFloat(half, &value));
        CHECK(FloatToHalfBits(value) == half);
    }
}

TEST_CASE("cube map: an equirectangular image becomes six faces that look the right way", "[asset][cube]")
{
    const Vector<byte> image = CmDirectionImage(256, 128);
    Vector<Vector<byte>> faces;
    const u32 size = 32;
    REQUIRE(EquirectangularToCubeFaces(ByteSpan(image.Data(), image.Size()), 256, 128, size, &faces).Ok());
    REQUIRE(faces.Size() == 6);
    for (u32 face = 0; face < 6; ++face)
    {
        REQUIRE(faces[face].Size() == static_cast<usize>(size) * size * 8);
        for (u32 y = 0; y < size; y += 5)
        {
            for (u32 x = 0; x < size; x += 5)
            {
                const f32 u = 2.0f * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size) - 1.0f;
                const f32 v = 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size) - 1.0f;
                const Vec3 d = CubeFaceDirection(face, u, v);
                const f32 length = Sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                const byte* texel = faces[face].Data() + (static_cast<usize>(y) * size + x) * 8;
                INFO("face " << face << " texel " << x << "," << y);
                CHECK(Abs(CmFloat(texel) - (d.x / length * 0.5f + 0.5f)) < 0.04f);
                CHECK(Abs(CmFloat(texel + 2) - (d.y / length * 0.5f + 0.5f)) < 0.04f);
                CHECK(Abs(CmFloat(texel + 4) - (d.z / length * 0.5f + 0.5f)) < 0.04f);
            }
        }
    }
    // The same input gives the same bytes.
    Vector<Vector<byte>> again;
    REQUIRE(EquirectangularToCubeFaces(ByteSpan(image.Data(), image.Size()), 256, 128, size, &again).Ok());
    for (u32 face = 0; face < 6; ++face)
    {
        CHECK(MemCompare(faces[face].Data(), again[face].Data(), faces[face].Size()) == 0);
    }
}

TEST_CASE("cube map: the equirectangular conversion refuses bad sizes and non-finite texels", "[asset][cube]")
{
    Vector<byte> image = CmDirectionImage(16, 8);
    Vector<Vector<byte>> faces;
    const ByteSpan all(image.Data(), image.Size());
    CHECK(EquirectangularToCubeFaces(all, 16, 8, 0, &faces).Code() == ErrorCode::Invalid);
    CHECK(EquirectangularToCubeFaces(all, 16, 8, kMaxCubeFaceSize + 1, &faces).Code() == ErrorCode::Invalid);
    CHECK(EquirectangularToCubeFaces(all, 1, 8, 4, &faces).Code() == ErrorCode::Invalid);
    CHECK(EquirectangularToCubeFaces(all, 16, 9, 4, &faces).Code() == ErrorCode::Invalid);  // wrong byte count
    image[0] = 0x00;
    image[1] = 0x7c;  // infinity in the first texel's red
    CHECK(EquirectangularToCubeFaces(all, 16, 8, 4, &faces).Code() == ErrorCode::Corrupt);
}

TEST_CASE("cube map: six half-float faces mint a cube with a full chain and keep their colors", "[asset][cube]")
{
    Vector<Vector<byte>> images;
    for (u32 face = 0; face < 6; ++face)
    {
        Vector<byte> pixels;
        pixels.Resize(8 * 8 * 8);
        for (usize i = 0; i < 64; ++i)
        {
            CmPut(&pixels, i, static_cast<f32>(face) * 4.0f, 0.5f, 100.0f, 1.0f);  // above 1: high dynamic range
        }
        images.Push(CmRawHalf(8, 8, pixels));
    }
    Vector<byte> blob;
    TextureSourceError error;
    REQUIRE(
        CmMint("id 0000000000000c01\nkind cube\ninput rgba16f\nformat rgba16f\nsource a\nsource b\nsource c\nsource d\n"
               "source e\nsource f\n",
               images, &blob, &error)
            .Ok());
    TypeRegistry types;
    TextureView view;
    REQUIRE(view.Open(AsBytes(blob), types).Ok());
    CHECK(view.Kind() == TextureKind::Cube);
    CHECK(view.Format() == rhi::Format::RGBA16_Float);
    CHECK(view.Layers() == 6);
    CHECK(view.MipCount() == 4);
    const rhi::TextureDesc desc = view.Desc(rhi::TextureUsage::Sampled);
    CHECK(desc.dimension == rhi::TextureDimension::Cube);
    CHECK(desc.array_layers == 6);
    CHECK(desc.Validate() == nullptr);
    for (u32 face = 0; face < 6; ++face)
    {
        for (u32 mip = 0; mip < 4; ++mip)
        {
            const TextureLevel level = view.Level(face, mip);
            CHECK(CmFloat(level.bytes.Data()) == static_cast<f32>(face) * 4.0f);  // a flat face stays flat in every mip
            CHECK(CmFloat(level.bytes.Data() + 4) == 100.0f);
        }
    }
}

TEST_CASE("cube map: an equirect source mints a cube and the same bytes every time", "[asset][cube]")
{
    Vector<Vector<byte>> images;
    const Vector<byte> equirect = CmPatternImage(64, 32);
    images.Push(CmRawHalf(64, 32, equirect));
    const char*
        text = "id 0000000000000c02\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 16\n"
               "source sky\n";
    Vector<byte> first;
    Vector<byte> second;
    TextureSourceError error;
    REQUIRE(CmMint(text, images, &first, &error).Ok());
    REQUIRE(CmMint(text, images, &second, &error).Ok());
    CHECK(first.Size() == second.Size());
    CHECK(CmFnv(first) == CmFnv(second));
    TypeRegistry types;
    TextureView view;
    REQUIRE(view.Open(AsBytes(first), types).Ok());
    CHECK(view.Kind() == TextureKind::Cube);
    CHECK(view.Width() == 16);
    CHECK(view.MipCount() == 5);
    // The bytes of the minted cube are pinned: a change in the conversion or the filter shows here.
    CHECK(CmFnv(first) == 0x7dfec67009bd6217ull);
}

TEST_CASE("cube map: the texture source refuses what a cube cannot be made from", "[asset][cube]")
{
    Vector<Vector<byte>> one;
    one.Push(CmRawHalf(8, 4, CmDirectionImage(8, 4)));
    Vector<Vector<byte>> eight_bit;
    {
        Vector<byte> rgba;
        rgba.Resize(8 * 4 * 4);
        Vector<byte> raw;
        REQUIRE(BuildRawImage(8, 4, ByteSpan(rgba.Data(), rgba.Size()), 0, &raw).Ok());
        eight_bit.Push(Move(raw));
    }
    Vector<byte> out;
    TextureSourceError error;
    // equirect needs input rgba16f, a face size and kind cube
    CHECK(CmMint("id 1\nkind cube\nformat rgba16f\nprojection equirect\nface_size 8\nsource a\n", one, &out, &error)
              .Failed());
    CHECK(CmMint("id 1\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nsource a\n", one, &out, &error)
              .Failed());
    CHECK(CmMint("id 1\nkind 2d\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 8\nsource a\n", one,
                 &out, &error)
              .Failed());
    CHECK(CmMint("id 1\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 5000\nsource a\n", one,
                 &out, &error)
              .Failed());
    CHECK(
        CmMint("id 1\nkind cube\ninput rgba16f\nformat rgba16f\nface_size 8\nsource a\n", one, &out, &error).Failed());
    // input rgba16f needs the half-float format, and the raw header must match the input
    CHECK(CmMint("id 1\ninput rgba16f\nformat rgba8\nsource a\n", one, &out, &error).Failed());
    CHECK(CmMint("id 1\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 8\nsource a\n",
                 eight_bit, &out, &error)
              .Failed());
    CHECK(CmMint("id 1\nformat rgba16f\nsource a\n", one, &out, &error).Failed());  // a RAWH file with input rgba8
    // a valid equirect source works on the same image
    CHECK(CmMint("id 0000000000000c03\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 8\n"
                 "source a\n",
                 one, &out, &error)
              .Ok());
}

}  // namespace vse::asset

namespace vse::asset
{

namespace
{

// Six 16 x 16 faces of a dim sky with one very bright patch on the +Z face.
Vector<Vector<byte>> CmSunCube()
{
    Vector<Vector<byte>> images;
    for (u32 face = 0; face < 6; ++face)
    {
        Vector<byte> pixels;
        pixels.Resize(16 * 16 * 8);
        for (usize i = 0; i < 256; ++i)
        {
            CmPut(&pixels, i, 0.25f, 0.25f, 0.25f, 1.0f);
        }
        if (face == 4)
        {
            for (u32 y = 7; y < 9; ++y)
            {
                for (u32 x = 7; x < 9; ++x)
                {
                    CmPut(&pixels, y * 16 + x, 200.0f, 150.0f, 100.0f, 1.0f);
                }
            }
        }
        images.Push(CmRawHalf(16, 16, pixels));
    }
    return images;
}

const char* const kCmEnvironmentText =
    "id 0000000000000c03\nkind cube\ninput rgba16f\nformat rgba16f\nenvironment yes\nsource a\nsource b\nsource c\n"
    "source d\nsource e\nsource f\n";
const char* const kCmPlainText =
    "id 0000000000000c03\nkind cube\ninput rgba16f\nformat rgba16f\nsource a\nsource b\nsource c\nsource d\n"
    "source e\nsource f\n";

f32 CmLevelPeak(const TextureView& view, u32 face, u32 mip)
{
    const TextureLevel level = view.Level(face, mip);
    f32 peak = 0.0f;
    for (usize i = 0; i + 8 <= level.bytes.Size(); i += 8)
    {
        const f32 v = CmFloat(level.bytes.Data() + i);
        peak = v > peak ? v : peak;
    }
    return peak;
}

f32 CmLevelMean(const TextureView& view, u32 face, u32 mip)
{
    const TextureLevel level = view.Level(face, mip);
    f32 sum = 0.0f;
    usize n = 0;
    for (usize i = 0; i + 8 <= level.bytes.Size(); i += 8)
    {
        sum += CmFloat(level.bytes.Data() + i);
        ++n;
    }
    return sum / static_cast<f32>(n);
}

}  // namespace

TEST_CASE("cube map: a direction finds the face and the texel the face table gave it", "[asset][cube]")
{
    for (u32 face = 0; face < 6; ++face)
    {
        for (f32 u : {-0.9f, -0.3f, 0.0f, 0.7f})
        {
            for (f32 v : {-0.8f, 0.1f, 0.95f})
            {
                u32 found = 9;
                f32 fu = 0.0f;
                f32 fv = 0.0f;
                CubeDirectionToFace(CubeFaceDirection(face, u, v) * 3.0f, &found, &fu, &fv);
                CHECK(found == face);
                CHECK(Abs(fu - u) < 1e-5f);
                CHECK(Abs(fv - v) < 1e-5f);
            }
        }
    }
}

TEST_CASE("cube map: the polynomial sine and cosine stay within 2e-6 of the library ones", "[asset][cube]")
{
    f32 worst = 0.0f;
    for (u32 i = 0; i <= 2000; ++i)
    {
        const f32 x = static_cast<f32>(i) * (2.0f * kCmPi / 2000.0f);
        f32 s = 0.0f;
        f32 c = 0.0f;
        DeterministicSinCos(x, &s, &c);
        worst = FMax(worst, FMax(Abs(s - Sin(x)), Abs(c - Cos(x))));
    }
    CHECK(worst < 2e-6f);
}

TEST_CASE("cube map: an environment cube keeps its sharp level and blurs the others by roughness", "[asset][cube]")
{
    const Vector<Vector<byte>> images = CmSunCube();
    Vector<byte> env;
    Vector<byte> plain;
    TextureSourceError error;
    REQUIRE(CmMint(kCmEnvironmentText, images, &env, &error).Ok());
    REQUIRE(CmMint(kCmPlainText, images, &plain, &error).Ok());
    TypeRegistry types;
    TextureView ev;
    TextureView pv;
    REQUIRE(ev.Open(AsBytes(env), types).Ok());
    REQUIRE(pv.Open(AsBytes(plain), types).Ok());
    REQUIRE(ev.MipCount() == 5);
    // Level 0 is the image itself, the same as without the option.
    for (u32 face = 0; face < 6; ++face)
    {
        const TextureLevel a = ev.Level(face, 0);
        const TextureLevel b = pv.Level(face, 0);
        REQUIRE(a.bytes.Size() == b.bytes.Size());
        CHECK(MemCompare(a.bytes.Data(), b.bytes.Data(), a.bytes.Size()) == 0);
    }
    // The bright patch spreads: the peak of its face falls through the levels 1 to 3 (the 1 x 1 level is a lobe of the
    // whole sphere around the face centre, and may sit a little above the 2 x 2 one) and stays below the source.
    f32 previous = CmLevelPeak(ev, 4, 0);
    for (u32 mip = 1; mip < 4; ++mip)
    {
        const f32 peak = CmLevelPeak(ev, 4, mip);
        INFO("mip " << mip << " peak " << peak << " previous " << previous);
        CHECK(peak < previous);
        previous = peak;
    }
    CHECK(CmLevelPeak(ev, 4, 4) < CmLevelPeak(ev, 4, 1));
    // A face that held no patch gets some of its light from the neighbours at a high roughness: brighter than the dim
    // sky it started as, and darker than the patch.
    CHECK(CmLevelMean(ev, 0, 3) > 0.25f);
    CHECK(CmLevelMean(ev, 0, 3) < CmLevelPeak(ev, 4, 0));
}

TEST_CASE("cube map: the environment chain keeps the energy of a smooth sky", "[asset][cube]")
{
    Vector<Vector<byte>> images;
    images.Push(CmRawHalf(64, 32, CmPatternImage(64, 32)));
    const char* plain_text =
        "id 0000000000000c05\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 16\nsource s\n";
    const char*
        env_text = "id 0000000000000c05\nkind cube\ninput rgba16f\nformat rgba16f\nprojection equirect\nface_size 16\n"
                   "environment yes\nsource s\n";
    Vector<byte> env;
    Vector<byte> plain;
    TextureSourceError error;
    REQUIRE(CmMint(env_text, images, &env, &error).Ok());
    REQUIRE(CmMint(plain_text, images, &plain, &error).Ok());
    TypeRegistry types;
    TextureView ev;
    TextureView pv;
    REQUIRE(ev.Open(AsBytes(env), types).Ok());
    REQUIRE(pv.Open(AsBytes(plain), types).Ok());
    for (u32 mip = 1; mip < 4; ++mip)
    {
        f32 env_mean = 0.0f;
        f32 plain_mean = 0.0f;
        for (u32 face = 0; face < 6; ++face)
        {
            env_mean += CmLevelMean(ev, face, mip);
            plain_mean += CmLevelMean(pv, face, mip);
        }
        INFO("mip " << mip << " env " << env_mean << " plain " << plain_mean);
        CHECK(Abs(env_mean - plain_mean) < 0.12f * plain_mean);
    }
}

TEST_CASE("cube map: a uniform environment stays uniform at every level, and the minted bytes never change",
          "[asset][cube]")
{
    Vector<Vector<byte>> images;
    for (u32 face = 0; face < 6; ++face)
    {
        Vector<byte> pixels;
        pixels.Resize(16 * 16 * 8);
        for (usize i = 0; i < 256; ++i)
        {
            CmPut(&pixels, i, 2.0f, 1.0f, 0.5f, 1.0f);
        }
        images.Push(CmRawHalf(16, 16, pixels));
    }
    Vector<byte> blob;
    TextureSourceError error;
    REQUIRE(CmMint(kCmEnvironmentText, images, &blob, &error).Ok());
    TypeRegistry types;
    TextureView view;
    REQUIRE(view.Open(AsBytes(blob), types).Ok());
    for (u32 face = 0; face < 6; ++face)
    {
        for (u32 mip = 0; mip < 5; ++mip)
        {
            const TextureLevel level = view.Level(face, mip);
            for (usize i = 0; i + 8 <= level.bytes.Size(); i += 8)
            {
                CHECK(Abs(CmFloat(level.bytes.Data() + i) - 2.0f) < 0.01f);
                CHECK(Abs(CmFloat(level.bytes.Data() + i + 2) - 1.0f) < 0.005f);
                CHECK(Abs(CmFloat(level.bytes.Data() + i + 4) - 0.5f) < 0.003f);
            }
        }
    }
    Vector<byte> sun;
    REQUIRE(CmMint(kCmEnvironmentText, CmSunCube(), &sun, &error).Ok());
    Vector<byte> again;
    REQUIRE(CmMint(kCmEnvironmentText, CmSunCube(), &again, &error).Ok());
    CHECK(CmFnv(sun) == CmFnv(again));
    CHECK(CmFnv(sun) == 0x6e8e6b8e4104a2cfull);  // pinned: a change in the prefilter shows here
}

TEST_CASE("cube map: environment needs a half-float cube", "[asset][cube]")
{
    Vector<Vector<byte>> images;
    Vector<byte> pixels;
    pixels.Resize(4 * 4 * 8);
    images.Push(CmRawHalf(4, 4, pixels));
    Vector<byte> blob;
    TextureSourceError error;
    CHECK(CmMint("id 0000000000000c04\nkind 2d\ninput rgba16f\nformat rgba16f\nenvironment yes\nsource a\n", images,
                 &blob, &error)
              .Failed());
    CHECK(CmMint("id 0000000000000c04\nkind cube\nenvironment yes\nsource a\nsource b\nsource c\nsource d\n"
                 "source e\nsource f\n",
                 images, &blob, &error)
              .Failed());
    CHECK(CmMint("id 0000000000000c04\nkind cube\ninput rgba16f\nformat rgba16f\nenvironment maybe\nsource a\n"
                 "source b\nsource c\nsource d\nsource e\nsource f\n",
                 images, &blob, &error)
              .Failed());
}

}  // namespace vse::asset

namespace vse::asset
{

TEST_CASE(
    "cube map: half-float faces with one NaN or infinite texel are refused, with or without the environment chain",
    "[asset][cube]")
{
    for (u32 bad = 0; bad < 2; ++bad)
    {
        for (u32 environment = 0; environment < 2; ++environment)
        {
            Vector<Vector<byte>> images;
            for (u32 face = 0; face < 6; ++face)
            {
                Vector<byte> pixels;
                pixels.Resize(8 * 8 * 8);
                for (usize i = 0; i < 64; ++i)
                {
                    CmPut(&pixels, i, 0.25f, 0.5f, 0.75f, 1.0f);
                }
                if (face == 3)
                {
                    pixels[17 * 8 + 2] = 0;                       // texel 17, green: exponent bits all set
                    pixels[17 * 8 + 3] = bad == 0 ? 0x7e : 0x7c;  // a NaN (0x7e00), then an infinity (0x7c00)
                }
                images.Push(Move(pixels));
            }
            Vector<ByteSpan> spans;
            for (usize i = 0; i < images.Size(); ++i)
            {
                spans.Push(ByteSpan(images[i].Data(), images[i].Size()));
            }
            TextureMintSpec spec;
            spec.kind = TextureKind::Cube;
            spec.format = rhi::Format::RGBA16_Float;
            spec.mips = environment != 0 ? 0 : 1;
            spec.environment = environment != 0;
            TypeRegistry types;
            Vector<byte> blob;
            const Status status = MintTextureFromHalfPixels(types, ResourceId::FromValue(1), spec, 8, 8,
                                                            Span<const ByteSpan>(spans.Data(), spans.Size()), &blob, 0);
            CHECK(status.Failed());
        }
    }
}

}  // namespace vse::asset
