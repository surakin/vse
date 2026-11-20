// Phase 22 Day 338: decoding PNG and JPEG images (asset_import/image_decode.h).

#include <catch2/catch_test_macros.hpp>

#include "asset_import/image_decode.h"
#include "build_defs.h"
#include "cstr/mem_ops.h"
#include "jpeg_samples.h"
#include "png_writer.h"

namespace vse::asset
{
namespace
{

using test::MakePng;
using test::PngSpec;

ByteSpan AsSpan(const Vector<byte>& v)
{
    return ByteSpan(v.Data(), v.Size());
}

Vector<byte> Bytes(const u8* data, usize size)
{
    Vector<byte> out;
    for (usize i = 0; i < size; ++i)
    {
        out.Push(static_cast<byte>(data[i]));
    }
    return out;
}

template <usize N> void PushAll(Vector<byte>* out, const int (&values)[N])
{
    for (usize i = 0; i < N; ++i)
    {
        out->Push(static_cast<byte>(values[i]));
    }
}

bool Pixel(const DecodedImage& image, u32 x, u32 y, int r, int g, int b, int a, int tolerance = 0)
{
    const byte* p = image.rgba.Data() + (static_cast<usize>(y) * image.width + x) * 4;
    const int want[4] = {r, g, b, a};
    for (int c = 0; c < 4; ++c)
    {
        const int d = static_cast<int>(p[c]) - want[c];
        if (d > tolerance || -d > tolerance)
        {
            return false;
        }
    }
    return true;
}

Status Decode(const Vector<byte>& bytes, DecodedImage* image, GltfError* error, u64 max_pixels = u64{1} << 26)
{
    return DecodePixels(AsSpan(bytes), max_pixels, image, error);
}

}  // namespace

TEST_CASE("image: the formats are told apart by their first bytes", "[asset][gltf]")
{
    CHECK(SniffImageFormat(ByteSpan()) == ImageFormat::Unknown);
    CHECK(SniffImageFormat(AsSpan(MakePng(PngSpec()))) == ImageFormat::Png);
    const Vector<byte> jpeg = Bytes(test::kJpegBaseline444, sizeof(test::kJpegBaseline444));
    CHECK(SniffImageFormat(AsSpan(jpeg)) == ImageFormat::Jpeg);
    Vector<byte> text;
    PushAll(&text, {'G', 'I', 'F', '8', '9', 'a', 0, 0, 0, 0});
    CHECK(SniffImageFormat(AsSpan(text)) == ImageFormat::Unknown);
    DecodedImage image;
    GltfError error;
    CHECK(Decode(text, &image, &error).Is(ErrorCode::Invalid));
    CHECK(Decode(Vector<byte>(), &image, &error).Is(ErrorCode::Invalid));
}

TEST_CASE("image: every PNG color form decodes to RGBA8", "[asset][gltf]")
{
    GltfError error;
    DecodedImage image;
    {  // RGB8
        PngSpec spec;
        spec.width = 3;
        spec.height = 2;
        spec.color_type = 2;
        PushAll(&spec.rows, {255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30, 40, 50, 60, 70, 80, 90});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(image.width == 3);
        CHECK(image.height == 2);
        CHECK(Pixel(image, 0, 0, 255, 0, 0, 255));
        CHECK(Pixel(image, 2, 0, 0, 0, 255, 255));
        CHECK(Pixel(image, 1, 1, 40, 50, 60, 255));
        CHECK_FALSE(image.has_alpha);
        CHECK_FALSE(image.reduced_depth);
    }
    {  // RGBA8: the alpha stays straight
        PngSpec spec;
        spec.width = 2;
        spec.height = 1;
        PushAll(&spec.rows, {200, 100, 50, 128, 1, 2, 3, 0});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 200, 100, 50, 128));
        CHECK(Pixel(image, 1, 0, 1, 2, 3, 0));
        CHECK(image.has_alpha);
    }
    {  // gray8 and 1-bit gray
        PngSpec spec;
        spec.width = 2;
        spec.height = 1;
        spec.color_type = 0;
        PushAll(&spec.rows, {7, 250});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 7, 7, 7, 255));
        CHECK(Pixel(image, 1, 0, 250, 250, 250, 255));
        PngSpec bits;
        bits.width = 10;
        bits.height = 1;
        bits.color_type = 0;
        bits.bit_depth = 1;
        PushAll(&bits.rows, {0xA0, 0x40});  // 1010 0000 01..
        REQUIRE(Decode(MakePng(bits), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 255, 255, 255, 255));
        CHECK(Pixel(image, 1, 0, 0, 0, 0, 255));
        CHECK(Pixel(image, 2, 0, 255, 255, 255, 255));
        CHECK(Pixel(image, 9, 0, 255, 255, 255, 255));
        CHECK(Pixel(image, 8, 0, 0, 0, 0, 255));
    }
    {  // gray with alpha
        PngSpec spec;
        spec.width = 1;
        spec.height = 1;
        spec.color_type = 4;
        PushAll(&spec.rows, {90, 33});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 90, 90, 90, 33));
        CHECK(image.has_alpha);
    }
    {  // palette with a transparency chunk
        PngSpec spec;
        spec.width = 3;
        spec.height = 1;
        spec.color_type = 3;
        PushAll(&spec.palette, {255, 0, 0, 0, 255, 0, 0, 0, 255});
        PushAll(&spec.transparency, {64, 128});
        PushAll(&spec.rows, {0, 1, 2});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 255, 0, 0, 64));
        CHECK(Pixel(image, 1, 0, 0, 255, 0, 128));
        CHECK(Pixel(image, 2, 0, 0, 0, 255, 255));
        CHECK(image.has_alpha);
    }
    {  // 16 bits per channel reduce to 8, and say so
        PngSpec spec;
        spec.width = 1;
        spec.height = 1;
        spec.color_type = 2;
        spec.bit_depth = 16;
        PushAll(&spec.rows, {0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00});
        REQUIRE(Decode(MakePng(spec), &image, &error).Ok());
        CHECK(Pixel(image, 0, 0, 255, 128, 0, 255, 1));
        CHECK(image.reduced_depth);
    }
}

TEST_CASE("image: an interlaced PNG decodes to the same pixels as the plain one", "[asset][gltf]")
{
    PngSpec plain;
    plain.width = 11;
    plain.height = 9;
    for (u32 y = 0; y < plain.height; ++y)
    {
        for (u32 x = 0; x < plain.width; ++x)
        {
            PushAll(&plain.rows,
                    {static_cast<int>(x * 20), static_cast<int>(y * 25), static_cast<int>((x + y) * 7), 255});
        }
    }
    PngSpec woven;
    woven.width = plain.width;
    woven.height = plain.height;
    woven.interlaced = true;
    for (usize i = 0; i < plain.rows.Size(); ++i)
    {
        woven.rows.Push(plain.rows[i]);
    }
    DecodedImage a;
    DecodedImage b;
    GltfError error;
    REQUIRE(Decode(MakePng(plain), &a, &error).Ok());
    REQUIRE(Decode(MakePng(woven), &b, &error).Ok());
    REQUIRE(a.rgba.Size() == b.rgba.Size());
    CHECK(MemCompare(a.rgba.Data(), b.rgba.Data(), a.rgba.Size()) == 0);
    CHECK(Pixel(a, 10, 8, 200, 200, 126, 255));
}

TEST_CASE("image: JPEGs decode (baseline, subsampled, progressive and gray)", "[asset][gltf]")
{
    GltfError error;
    DecodedImage full;
    DecodedImage half;
    DecodedImage progressive;
    DecodedImage baseline75;
    DecodedImage gray;
    REQUIRE(Decode(Bytes(test::kJpegBaseline444, sizeof(test::kJpegBaseline444)), &full, &error).Ok());
    REQUIRE(Decode(Bytes(test::kJpegBaseline420, sizeof(test::kJpegBaseline420)), &half, &error).Ok());
    const Status pstatus = Decode(Bytes(test::kJpegProgressive75, sizeof(test::kJpegProgressive75)), &progressive,
                                  &error);
    INFO(error.message.Data());
    REQUIRE(pstatus.Ok());
    REQUIRE(Decode(Bytes(test::kJpegGray, sizeof(test::kJpegGray)), &gray, &error).Ok());
    CHECK(full.width == 24);
    CHECK(full.height == 16);
    CHECK_FALSE(full.has_alpha);
    // The gradient: red 10 x, green 16 y.
    CHECK(Pixel(full, 20, 3, 200, 48, 120, 255, 40));
    CHECK(Pixel(half, 20, 3, 200, 48, 120, 255, 40));
    REQUIRE(Decode(Bytes(test::kJpegBaseline75, sizeof(test::kJpegBaseline75)), &baseline75, &error).Ok());
    REQUIRE(progressive.rgba.Size() == baseline75.rgba.Size());
    // (the progressive file is 4:2:0, the baseline 4:4:4: they agree on the smooth gradient within a few steps)
    CHECK(Pixel(progressive, 20, 3, 200, 48, 120, 255, 70));
    CHECK(Pixel(baseline75, 20, 3, 200, 48, 120, 255, 70));
    CHECK(gray.width == 24);
    const byte* g = gray.rgba.Data() + (3 * 24 + 20) * 4;
    CHECK(g[0] == g[1]);
    CHECK(g[1] == g[2]);
    CHECK(g[3] == 255);
}

TEST_CASE("image: the limits are checked before any pixel is decoded", "[asset][gltf]")
{
#if ENGINE_SANITIZER_THREAD
    SKIP(
        "ThreadSanitizer cannot follow libpng's longjmp (KNOWN-ISSUES.md); the plain and AddressSanitizer runs cover it");
#endif
    GltfError error;
    DecodedImage image;
    // A PNG whose header says 16384 x 16384 and whose data is a few bytes: refused by its size, nothing allocated for
    // it.
    PngSpec big;
    big.width = 16384;
    big.height = 16384;
    big.color_type = 0;
    PushAll(&big.rows, {0});
    big.rows = Vector<byte>();
    Vector<byte> file = MakePng(big);
    CHECK(Decode(file, &image, &error).Is(ErrorCode::TooLarge));
    CHECK(image.rgba.Size() == 0);
    // Wider than 16384 is refused by the reader's own limit.
    PngSpec wide;
    wide.width = 20000;
    wide.height = 1;
    wide.color_type = 0;
    CHECK(Decode(MakePng(wide), &image, &error).Failed());
    // The pixel limit of the call.
    PngSpec small;
    small.width = 4;
    small.height = 4;
    small.color_type = 0;
    for (int i = 0; i < 16; ++i)
    {
        small.rows.Push(static_cast<byte>(i));
    }
    CHECK(Decode(MakePng(small), &image, &error, 15).Is(ErrorCode::TooLarge));
    CHECK(Decode(MakePng(small), &image, &error, 16).Ok());
    // A JPEG whose frame header says 16000 x 16000: refused before the decoder allocates.
    Vector<byte> jpeg = Bytes(test::kJpegBaseline444, sizeof(test::kJpegBaseline444));
    usize sof = 0;
    for (usize i = 0; i + 8 < jpeg.Size(); ++i)
    {
        if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC0)
        {
            sof = i;
            break;
        }
    }
    REQUIRE(sof != 0);
    jpeg[sof + 5] = 0x3E;  // 16000 = 0x3E80
    jpeg[sof + 6] = 0x80;
    jpeg[sof + 7] = 0x3E;
    jpeg[sof + 8] = 0x80;
    CHECK(Decode(jpeg, &image, &error).Is(ErrorCode::TooLarge));
}

TEST_CASE("image: truncated and mutated files fail cleanly", "[asset][gltf]")
{
#if ENGINE_SANITIZER_THREAD
    SKIP(
        "ThreadSanitizer cannot follow libpng's longjmp (KNOWN-ISSUES.md); the plain and AddressSanitizer runs cover it");
#endif
    PngSpec spec;
    spec.width = 9;
    spec.height = 7;
    for (u32 i = 0; i < 9 * 7 * 4; ++i)
    {
        spec.rows.Push(static_cast<byte>(i * 7));
    }
    const Vector<byte> png = MakePng(spec);
    const Vector<byte> jpeg = Bytes(test::kJpegBaseline420, sizeof(test::kJpegBaseline420));
    const Vector<byte> progressive = Bytes(test::kJpegProgressive75, sizeof(test::kJpegProgressive75));
    const Vector<byte>* files[3] = {&png, &jpeg, &progressive};
    GltfError error;
    DecodedImage image;
    for (const Vector<byte>* file : files)
    {
        for (usize cut = 0; cut < file->Size(); ++cut)
        {
            const Status status = DecodePixels(ByteSpan(file->Data(), cut), u64{1} << 20, &image, &error);
            if (status.Ok())
            {
                CHECK(image.rgba.Size() ==
                      static_cast<usize>(image.width) * image.height * 4);  // a decoder may accept a short JPEG
            }
        }
        u64 state = 77;
        for (u32 n = 0; n < 2000; ++n)
        {
            Vector<byte> mutated;
            for (usize i = 0; i < file->Size(); ++i)
            {
                mutated.Push((*file)[i]);
            }
            for (u32 flips = 0; flips < 1 + n % 4; ++flips)
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                mutated[static_cast<usize>((state >> 33) % mutated.Size())] = static_cast<byte>(state >> 17);
            }
            const Status status = DecodePixels(AsSpan(mutated), u64{1} << 20, &image, &error);
            if (status.Ok())
            {
                CHECK(image.rgba.Size() == static_cast<usize>(image.width) * image.height * 4);
                CHECK(static_cast<u64>(image.width) * image.height <= (u64{1} << 20));
            }
        }
    }
}

}  // namespace vse::asset
