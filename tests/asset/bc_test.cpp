// Block compression: the decoders, the BC1, BC4 and BC5 encoders and the minter's compressed formats (Phase 22 Day 324).

#include <catch2/catch_test_macros.hpp>

#include "asset/block_compress.h"
#include "asset/texture.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "jobs/scheduler.h"
#include "renderer/device_resources.h"
#include "renderer/renderer_log.h"
#include "renderer/texture.h"
#include "rhi/null/null_device.h"
#include "support.h"
#include "texture_source.h"

namespace engine_placeholder::asset
{

namespace
{

Vector<byte> BtBytes(const byte* data, usize size)
{
    Vector<byte> out;
    out.Resize(size);
    MemCopy(out.Data(), data, size);
    return out;
}

Vector<byte> BtImage(u32 w, u32 h)
{
    Vector<byte> out;
    out.Resize(static_cast<usize>(w) * h * 4);
    return out;
}

void BtSet(Vector<byte>* image, u32 w, u32 x, u32 y, u32 r, u32 g, u32 b, u32 a = 255)
{
    byte* p = image->Data() + (static_cast<usize>(y) * w + x) * 4;
    p[0] = static_cast<byte>(r);
    p[1] = static_cast<byte>(g);
    p[2] = static_cast<byte>(b);
    p[3] = static_cast<byte>(a);
}

Vector<byte> BtSolid(u32 w, u32 h, u32 r, u32 g, u32 b)
{
    Vector<byte> img = BtImage(w, h);
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            BtSet(&img, w, x, y, r, g, b);
        }
    }
    return img;
}

Vector<byte> BtGradient(u32 w, u32 h)
{
    Vector<byte> img = BtImage(w, h);
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            BtSet(&img, w, x, y, x * 255 / (w - 1), y * 255 / (h - 1), (x + y) * 255 / (w + h - 2));
        }
    }
    return img;
}

Vector<byte> BtChecker(u32 w, u32 h, u32 cell)
{
    Vector<byte> img = BtImage(w, h);
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            BtSet(&img, w, x, y, on ? 230 : 20, on ? 200 : 40, on ? 10 : 180);
        }
    }
    return img;
}

Vector<byte> BtNoise(u32 w, u32 h, u64 seed)
{
    test::Rng rng(seed);
    Vector<byte> img = BtImage(w, h);
    for (usize i = 0; i < img.Size(); ++i)
    {
        img[i] = static_cast<byte>(i % 4 == 3 ? 255 : rng.Next());
    }
    return img;
}

// A tangent-space normal map of rolling bumps: x and y in red and green, z implied.
Vector<byte> BtNormals(u32 w, u32 h)
{
    Vector<byte> img = BtImage(w, h);
    for (u32 y = 0; y < h; ++y)
    {
        for (u32 x = 0; x < w; ++x)
        {
            const i32 nx = static_cast<i32>((x * 7 + y * 3) % 61) - 30;
            const i32 ny = static_cast<i32>((x * 2 + y * 9) % 53) - 26;
            BtSet(&img, w, x, y, static_cast<u32>(128 + nx * 3), static_cast<u32>(128 + ny * 3), 255);
        }
    }
    return img;
}

// The mean squared error over the first `channels` channels of two RGBA8 images, times 1000 (integers only).
u64 BtMse1000(const Vector<byte>& a, const Vector<byte>& b, u32 channels)
{
    REQUIRE(a.Size() == b.Size());
    u64 sum = 0;
    u64 n = 0;
    for (usize i = 0; i < a.Size(); i += 4)
    {
        for (u32 c = 0; c < channels; ++c)
        {
            const i64 d = static_cast<i64>(a[i + c]) - b[i + c];
            sum += static_cast<u64>(d * d);
            ++n;
        }
    }
    return sum * 1000 / n;
}

Vector<byte> BtRoundTrip(BcKind kind, u32 w, u32 h, const Vector<byte>& img, const BcEncodeOptions& options,
                         jobs::Scheduler* scheduler = nullptr)
{
    Vector<byte> blocks;
    const Status e = EncodeImage(kind, w, h, ByteSpan(img.Data(), img.Size()), options, scheduler, &blocks);
    INFO(e.Message());
    REQUIRE(e.Ok());
    REQUIRE(blocks.Size() == static_cast<usize>((w + 3) / 4) * ((h + 3) / 4) * BcBlockBytes(kind));
    Vector<byte> decoded;
    const Status d = DecodeImage(kind, w, h, ByteSpan(blocks.Data(), blocks.Size()), &decoded);
    INFO(d.Message());
    REQUIRE(d.Ok());
    return decoded;
}

}  // namespace

TEST_CASE("bc: the decoders read the formats' worked examples", "[asset][bc]")
{
    // BC1 four-color form: c0 = red (0xF800), c1 = blue (0x001F), c0 > c1. Codes 0, 1, 2, 3 in the first row.
    const byte bc1[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};  // codes 0,1,2,3 then 0
    byte px[64];
    DecodeBlock(BcKind::Bc1, bc1, px);
    CHECK(px[0] == 255);
    CHECK(px[1] == 0);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);
    CHECK(px[4 + 2] == 255);  // code 1 = blue
    CHECK(px[4] == 0);
    CHECK(px[8] == 170);  // code 2 = (2 * red + blue) / 3
    CHECK(px[8 + 2] == 85);
    CHECK(px[12] == 85);  // code 3
    CHECK(px[12 + 2] == 170);
    CHECK(px[16] == 255);  // the second row starts again at code 0

    // BC1 three-color form: c0 < c1; code 2 is the middle, code 3 is transparent black.
    const byte bc1_three[8] = {0x1F, 0x00, 0x00, 0xF8, 0xB4, 0x00, 0x00, 0x00};  // codes 0,1,3,2 in the first row
    DecodeBlock(BcKind::Bc1, bc1_three, px);
    CHECK(px[2] == 255);
    CHECK(px[4] == 255);
    CHECK(px[8 + 3] == 0);  // code 3: alpha 0
    CHECK(px[8] == 0);
    CHECK(px[12] == 127);  // code 2 = (0 + 255) / 2
    CHECK(px[12 + 2] == 127);
    CHECK(px[12 + 3] == 255);

    // BC4 eight-step form: a0 = 255 > a1 = 0, codes 0..7 in the first two texels and so on.
    // Indices (3 bits each, little endian): texels 0..7 = 0,1,2,3,4,5,6,7.
    const byte bc4[8] = {255, 0, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00};
    DecodeBlock(BcKind::Bc4, bc4, px);
    const u32 expect8[8] = {255, 0, 218, 182, 145, 109, 72, 36};
    for (u32 i = 0; i < 8; ++i)
    {
        CHECK(px[i * 4] == expect8[i]);
    }
    CHECK(px[3] == 255);
    CHECK(px[1] == 0);

    // BC4 six-step form: a0 = 0 <= a1 = 255; codes 6 and 7 are 0 and 255.
    const byte bc4_six[8] = {0, 255, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00};
    DecodeBlock(BcKind::Bc4, bc4_six, px);
    const u32 expect6[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    for (u32 i = 0; i < 8; ++i)
    {
        CHECK(px[i * 4] == expect6[i]);
    }

    // BC5: red from the first half, green from the second.
    byte bc5[16] = {255, 0, 0x88, 0xC6, 0xFA, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 0};
    DecodeBlock(BcKind::Bc5, bc5, px);
    CHECK(px[0] == 255);
    CHECK(px[1] == 100);
    CHECK(px[2] == 0);
    CHECK(px[3] == 255);

    // BC2: the alpha is the explicit nibbles, the color half is always the four-color form.
    byte bc2[16];
    MemSet(bc2, 0, 16);
    bc2[0] = 0xF1;  // texel 0 = 1 (17), texel 1 = 15 (255)
    bc2[8] = 0x1F;  // c0 = 0x001F
    bc2[10] = 0x00;  // c1 = 0x0000: c0 > c1
    DecodeBlock(BcKind::Bc2, bc2, px);
    CHECK(px[3] == 17);
    CHECK(px[7] == 255);
    CHECK(px[2] == 255);
    // BC3 with c0 < c1 still decodes four colors: code 2 = (2 * c0 + c1) / 3.
    byte bc3[16];
    MemSet(bc3, 0, 16);
    bc3[0] = 200;  // alpha endpoints 200, 0: every texel code 0 = 200
    bc3[8] = 0x00;
    bc3[9] = 0xF8;  // c0 = red
    bc3[10] = 0xFF;
    bc3[11] = 0xFF;  // c1 = white: c0 < c1
    bc3[12] = 0x02;  // texel 0 = code 2
    DecodeBlock(BcKind::Bc3, bc3, px);
    CHECK(px[3] == 200);
    CHECK(px[0] == 255);
    CHECK(px[1] == 85);  // (2 * 0 + 255) / 3
    CHECK(px[2] == 85);
}

TEST_CASE("bc: an image decodes with partial blocks and wrong sizes are refused", "[asset][bc]")
{
    // 5x3: two block columns, one block row. The second block's visible texels are its first column only.
    const Vector<byte> img = BtSolid(5, 3, 255, 0, 0);
    Vector<byte> out;
    BcEncodeOptions options;
    Vector<byte> blocks;
    REQUIRE(EncodeImage(BcKind::Bc1, 5, 3, ByteSpan(img.Data(), img.Size()), options, nullptr, &blocks).Ok());
    CHECK(blocks.Size() == 16);
    REQUIRE(DecodeImage(BcKind::Bc1, 5, 3, ByteSpan(blocks.Data(), blocks.Size()), &out).Ok());
    CHECK(out.Size() == 5 * 3 * 4);
    for (usize i = 0; i < out.Size(); i += 4)
    {
        CHECK(out[i] == 255);
        CHECK(out[i + 1] == 0);
        CHECK(out[i + 3] == 255);
    }
    CHECK(DecodeImage(BcKind::Bc1, 5, 3, ByteSpan(blocks.Data(), 8), &out).Failed());
    CHECK(DecodeImage(BcKind::Bc1, 0, 3, ByteSpan(blocks.Data(), blocks.Size()), &out).Failed());
    CHECK(EncodeImage(BcKind::Bc1, 5, 3, ByteSpan(img.Data(), img.Size() - 4), options, nullptr, &blocks).Failed());
    CHECK(EncodeImage(BcKind::Bc3, 5, 3, ByteSpan(img.Data(), img.Size()), options, nullptr, &blocks).Failed());
    CHECK(EncodeImage(BcKind::Bc2, 5, 3, ByteSpan(img.Data(), img.Size()), options, nullptr, &blocks).Failed());
}

TEST_CASE("bc: an all-equal block encodes exactly", "[asset][bc]")
{
    BcEncodeOptions options;
    // A color the 5:6:5 grid holds exactly (every channel an expanded value) decodes back to itself in BC1.
    test::Rng rng(11);
    for (u32 trial = 0; trial < 200; ++trial)
    {
        const u32 r5 = rng.Below(32);
        const u32 g6 = rng.Below(64);
        const u32 b5 = rng.Below(32);
        const u32 r = (r5 << 3) | (r5 >> 2);
        const u32 g = (g6 << 2) | (g6 >> 4);
        const u32 b = (b5 << 3) | (b5 >> 2);
        const Vector<byte> img = BtSolid(4, 4, r, g, b);
        const Vector<byte> back = BtRoundTrip(BcKind::Bc1, 4, 4, img, options);
        bool same = back.Size() == img.Size() && MemCompare(back.Data(), img.Data(), img.Size()) == 0;
        if (!same)
        {
            INFO("trial " << trial << " color " << r << " " << g << " " << b);
        }
        CHECK(same);
    }
    // Any single value is exact in BC4 and BC5.
    for (u32 v = 0; v < 256; ++v)
    {
        Vector<byte> img = BtSolid(4, 4, v, 255 - v, 0);
        const Vector<byte> four = BtRoundTrip(BcKind::Bc4, 4, 4, img, options);
        const Vector<byte> five = BtRoundTrip(BcKind::Bc5, 4, 4, img, options);
        bool ok = true;
        for (usize i = 0; i < img.Size(); i += 4)
        {
            ok = ok && four[i] == v && five[i] == v && five[i + 1] == 255 - v;
        }
        CHECK(ok);
    }
    // Every quality gives the same exact result.
    const Vector<byte> img = BtSolid(4, 4, 255, 0, 0);
    for (u32 q = 0; q < 3; ++q)
    {
        options.quality = static_cast<BcQuality>(q);
        const Vector<byte> back = BtRoundTrip(BcKind::Bc1, 4, 4, img, options);
        CHECK(MemCompare(back.Data(), img.Data(), img.Size()) == 0);
    }
}

TEST_CASE("bc: the encoders' blocks decode and are never worse than a flat block", "[asset][bc]")
{
    // An exhaustive-by-generation set: random blocks, two-valued blocks, ramps, and the extremes.
    test::Rng rng(21);
    BcEncodeOptions options;
    u32 failures = 0;
    for (u32 trial = 0; trial < 4000; ++trial)
    {
        byte px[64];
        const u32 style = trial % 5;
        for (u32 i = 0; i < 16; ++i)
        {
            for (u32 c = 0; c < 3; ++c)
            {
                u32 v;
                if (style == 0)
                {
                    v = rng.Below(256);
                }
                else if (style == 1)
                {
                    v = (i % 3 == 0) ? 0 : 255;
                }
                else if (style == 2)
                {
                    v = (i * 17 + c * 5) & 255;
                }
                else if (style == 3)
                {
                    v = 100 + rng.Below(4);
                }
                else
                {
                    v = rng.Below(2) == 0 ? rng.Below(8) : 248 + rng.Below(8);
                }
                px[i * 4 + c] = static_cast<byte>(v);
            }
            px[i * 4 + 3] = 255;
        }
        for (u32 q = 0; q < 3; ++q)
        {
            options.quality = static_cast<BcQuality>(q);
            for (u32 k = 0; k < 3; ++k)
            {
                const BcKind kind = k == 0 ? BcKind::Bc1 : (k == 1 ? BcKind::Bc4 : BcKind::Bc5);
                byte block[16];
                EncodeBlock(kind, px, options, block);
                byte again[16];
                EncodeBlock(kind, px, options, again);
                byte decoded[64];
                DecodeBlock(kind, block, decoded);
                const u32 channels = kind == BcKind::Bc1 ? 3 : (kind == BcKind::Bc4 ? 1 : 2);
                u64 error = 0;
                u64 flat = 0;
                for (u32 c = 0; c < channels; ++c)
                {
                    i64 sum = 0;
                    for (u32 i = 0; i < 16; ++i)
                    {
                        sum += px[i * 4 + c];
                    }
                    const i64 mean = (sum + 8) / 16;
                    for (u32 i = 0; i < 16; ++i)
                    {
                        const i64 d = static_cast<i64>(px[i * 4 + c]) - decoded[i * 4 + c];
                        const i64 f = static_cast<i64>(px[i * 4 + c]) - mean;
                        error += static_cast<u64>(d * d);
                        flat += static_cast<u64>(f * f);
                    }
                }
                // BC1 endpoints sit on the 5:6:5 grid, so allow its quantization on top of the flat error.
                const u64 slack = kind == BcKind::Bc1 ? 16 * 3 * 16 : 0;
                const bool same = MemCompare(block, again, BcBlockBytes(kind)) == 0;
                // The range fit alone (Fast) may lose to a flat block on pure noise; the refined qualities may not.
                if (!same || (q > 0 && error > flat + slack))
                {
                    ++failures;

                }
            }
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("bc: the quality setting never raises the error of a block", "[asset][bc]")
{
    test::Rng rng(31);
    u32 worse = 0;
    u64 total[3] = {0, 0, 0};
    for (u32 trial = 0; trial < 1500; ++trial)
    {
        byte px[64];
        for (u32 i = 0; i < 16; ++i)
        {
            const u32 base = rng.Below(256);
            for (u32 c = 0; c < 3; ++c)
            {
                px[i * 4 + c] = static_cast<byte>((base + rng.Below(60)) & 255);
            }
            px[i * 4 + 3] = 255;
        }
        u64 error[3];
        for (u32 q = 0; q < 3; ++q)
        {
            BcEncodeOptions options;
            options.quality = static_cast<BcQuality>(q);
            byte block[8];
            EncodeBlock(BcKind::Bc1, px, options, block);
            byte decoded[64];
            DecodeBlock(BcKind::Bc1, block, decoded);
            error[q] = 0;
            for (u32 i = 0; i < 16; ++i)
            {
                for (u32 c = 0; c < 3; ++c)
                {
                    const i64 d = static_cast<i64>(px[i * 4 + c]) - decoded[i * 4 + c];
                    error[q] += static_cast<u64>(d * d);
                }
            }
            total[q] += error[q];
        }
        worse += (error[1] > error[0] || error[2] > error[1]) ? 1 : 0;
    }
    CHECK(worse == 0);
    CHECK(total[2] <= total[1]);
    CHECK(total[1] <= total[0]);
}

TEST_CASE("bc1: transparent texels use the three-color form and keep their alpha", "[asset][bc]")
{
    BcEncodeOptions options;
    options.punchthrough_alpha = true;
    byte px[64];
    for (u32 i = 0; i < 16; ++i)
    {
        const bool clear = i % 4 == 0;
        px[i * 4] = static_cast<byte>(clear ? 0 : 200);
        px[i * 4 + 1] = static_cast<byte>(clear ? 0 : 100);
        px[i * 4 + 2] = static_cast<byte>(clear ? 0 : 50);
        px[i * 4 + 3] = static_cast<byte>(clear ? 0 : 255);
    }
    byte block[8];
    EncodeBlock(BcKind::Bc1, px, options, block);
    const u32 c0 = static_cast<u32>(block[0] | (block[1] << 8));
    const u32 c1 = static_cast<u32>(block[2] | (block[3] << 8));
    CHECK(c0 <= c1);
    byte decoded[64];
    DecodeBlock(BcKind::Bc1, block, decoded);
    for (u32 i = 0; i < 16; ++i)
    {
        CHECK(decoded[i * 4 + 3] == (i % 4 == 0 ? 0 : 255));
        if (i % 4 != 0)
        {
            const i32 d = static_cast<i32>(decoded[i * 4]) - 200;
            CHECK(d * d < 100);
        }
    }
    // A fully transparent block, and the same block without the rule (alpha is then ignored).
    for (u32 i = 0; i < 16; ++i)
    {
        px[i * 4 + 3] = 0;
    }
    EncodeBlock(BcKind::Bc1, px, options, block);
    DecodeBlock(BcKind::Bc1, block, decoded);
    for (u32 i = 0; i < 16; ++i)
    {
        CHECK(decoded[i * 4 + 3] == 0);
    }
    options.punchthrough_alpha = false;
    EncodeBlock(BcKind::Bc1, px, options, block);
    DecodeBlock(BcKind::Bc1, block, decoded);
    for (u32 i = 0; i < 16; ++i)
    {
        CHECK(decoded[i * 4 + 3] == 255);
    }
}

TEST_CASE("bc: image quality bounds for solid, gradient, checker, noise and normal maps", "[asset][bc]")
{
    BcEncodeOptions options;
    const u32 w = 64;
    const u32 h = 64;
    // The mean squared error per channel, times 1000 (30 dB is 65000).
    const Vector<byte> solid = BtSolid(w, h, 77, 150, 220);
    const Vector<byte> gradient = BtGradient(w, h);
    const Vector<byte> checker = BtChecker(w, h, 4);
    const Vector<byte> noise = BtNoise(w, h, 5);
    const Vector<byte> normals = BtNormals(w, h);

    const u64 solid_mse = BtMse1000(solid, BtRoundTrip(BcKind::Bc1, w, h, solid, options), 3);
    const u64 gradient_mse = BtMse1000(gradient, BtRoundTrip(BcKind::Bc1, w, h, gradient, options), 3);
    const u64 checker_mse = BtMse1000(checker, BtRoundTrip(BcKind::Bc1, w, h, checker, options), 3);
    const u64 noise_mse = BtMse1000(noise, BtRoundTrip(BcKind::Bc1, w, h, noise, options), 3);
    const u64 gradient4_mse = BtMse1000(gradient, BtRoundTrip(BcKind::Bc4, w, h, gradient, options), 1);
    const u64 noise4_mse = BtMse1000(noise, BtRoundTrip(BcKind::Bc4, w, h, noise, options), 1);
    const u64 normals5_mse = BtMse1000(normals, BtRoundTrip(BcKind::Bc5, w, h, normals, options), 2);
    const u64 noise5_mse = BtMse1000(noise, BtRoundTrip(BcKind::Bc5, w, h, noise, options), 2);
    os::Print("bc mse*1000: solid %llu gradient %llu checker %llu noise %llu | bc4 gradient %llu noise %llu | bc5 normals "
              "%llu noise %llu\n",
              static_cast<unsigned long long>(solid_mse), static_cast<unsigned long long>(gradient_mse),
              static_cast<unsigned long long>(checker_mse), static_cast<unsigned long long>(noise_mse),
              static_cast<unsigned long long>(gradient4_mse), static_cast<unsigned long long>(noise4_mse),
              static_cast<unsigned long long>(normals5_mse), static_cast<unsigned long long>(noise5_mse));
    CHECK(solid_mse < 8000);        // within the 5:6:5 grid's own step
    CHECK(gradient_mse < 20000);   // measured 8.8 per channel (38.7 dB)
    CHECK(checker_mse < 8000);     // measured 3.8: block-aligned cells, only the 5:6:5 grid
    CHECK(noise_mse < 3200000);        // uniform noise has no line to fit: about 13.6 dB
    CHECK(gradient4_mse < 2000);   // measured 0.2
    CHECK(noise4_mse < 100000);    // measured 62.5 (30.2 dB)
    CHECK(normals5_mse < 50000);   // measured 25.1 (34.1 dB)
    CHECK(noise5_mse < 100000);    // measured 61.4
}

TEST_CASE("bc: the output does not depend on the worker count", "[asset][bc]")
{
    const u32 w = 83;
    const u32 h = 61;
    const Vector<byte> img = BtNoise(w, h, 9);
    BcEncodeOptions options;
    options.punchthrough_alpha = true;
    for (u32 k = 0; k < 3; ++k)
    {
        const BcKind kind = k == 0 ? BcKind::Bc1 : (k == 1 ? BcKind::Bc4 : BcKind::Bc5);
        Vector<byte> serial;
        REQUIRE(EncodeImage(kind, w, h, ByteSpan(img.Data(), img.Size()), options, nullptr, &serial).Ok());
        const usize workers[3] = {1, 3, 8};
        for (u32 run = 0; run < 3; ++run)
        {
            jobs::Scheduler::Config config;
            config.participants = workers[run];
            config.pin = false;
            jobs::Scheduler scheduler(config);
            Vector<byte> parallel;
            REQUIRE(EncodeImage(kind, w, h, ByteSpan(img.Data(), img.Size()), options, &scheduler, &parallel).Ok());
            REQUIRE(parallel.Size() == serial.Size());
            CHECK(MemCompare(parallel.Data(), serial.Data(), serial.Size()) == 0);
        }
    }
}

namespace
{

Vector<byte> BtMintText(const char* format_line, u32 w, u32 h, const Vector<byte>& rgba, u32 target = 0,
                        jobs::Scheduler* scheduler = nullptr)
{
    Vector<char> text;
    const char* head = "id 00000000000000dd\nsource a.raw\n";
    for (; *head != '\0'; ++head)
    {
        text.Push(*head);
    }
    for (; *format_line != '\0'; ++format_line)
    {
        text.Push(*format_line);
    }
    Vector<byte> raw;
    REQUIRE(BuildRawImage(w, h, ByteSpan(rgba.Data(), rgba.Size()), 0, &raw).Ok());
    TypeRegistry types;
    ByteSpan image(raw.Data(), raw.Size());
    TextureSourceError error;
    Vector<byte> blob;
    const Status s = MintTexture(StringView(text.Data(), text.Size()), Span<const ByteSpan>(&image, 1), types, &blob,
                                 &error, target, scheduler);
    INFO(s.Message());
    INFO(error.message.Data());
    REQUIRE(s.Ok());
    return blob;
}

}  // namespace

TEST_CASE("bc minter: bc1, bc4 and bc5 sources mint compressed levels the view returns as they are", "[asset][bc]")
{
    TypeRegistry types;
    const u32 w = 21;
    const u32 h = 10;
    const Vector<byte> img = BtGradient(w, h);
    struct Case
    {
        const char* line;
        rhi::Format format;
        u32 block_bytes;
        u32 channels;
    };
    const Case cases[4] = {{"format bc1\n", rhi::Format::BC1_UNorm, 8, 3},
                           {"format bc1\nsrgb true\n", rhi::Format::BC1_SRGB, 8, 3},
                           {"format bc4\n", rhi::Format::BC4_UNorm, 8, 1},
                           {"format bc5\n", rhi::Format::BC5_UNorm, 16, 2}};
    for (u32 c = 0; c < 4; ++c)
    {
        INFO(cases[c].line);
        const Vector<byte> blob = BtMintText(cases[c].line, w, h, img);
        TextureView view;
        REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
        CHECK(view.Format() == cases[c].format);
        CHECK(view.Srgb() == (cases[c].format == rhi::Format::BC1_SRGB));
        CHECK(view.MipCount() == 5);  // 21x10, 10x5, 5x2, 2x1, 1x1
        u32 lw = w;
        u32 lh = h;
        for (u32 mip = 0; mip < view.MipCount(); ++mip)
        {
            const TextureLevel level = view.Level(0, mip);
            const u32 bw = (lw + 3) / 4;
            const u32 bh = (lh + 3) / 4;
            CHECK(level.width == lw);
            CHECK(level.height == lh);
            CHECK(level.row_pitch >= bw * cases[c].block_bytes);
            CHECK(level.row_pitch % cases[c].block_bytes == 0);
            CHECK(level.bytes.Size() == static_cast<usize>(level.row_pitch) * bh);
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
        // Level 0 decodes close to the source.
        const TextureLevel top = view.Level(0, 0);
        Vector<byte> tight;
        const u32 row = ((w + 3) / 4) * cases[c].block_bytes;
        for (u32 by = 0; by < (h + 3) / 4; ++by)
        {
            for (u32 i = 0; i < row; ++i)
            {
                tight.Push(top.bytes[static_cast<usize>(by) * top.row_pitch + i]);
            }
        }
        Vector<byte> decoded;
        const BcKind kind = BcKindOf(cases[c].format);
        REQUIRE(DecodeImage(kind, w, h, ByteSpan(tight.Data(), tight.Size()), &decoded).Ok());
        CHECK(BtMse1000(img, decoded, cases[c].channels) < 100000);
        const rhi::TextureDesc desc = view.Desc(rhi::TextureUsage::Sampled);
        CHECK(desc.Validate() == nullptr);
    }
    // bc4 and bc5 are linear: srgb is refused, and so is a format name that is not one of them.
    Vector<char> text;
    const char* bad = "id 00000000000000dd\nsource a.raw\nformat bc4\nsrgb true\n";
    for (; *bad != '\0'; ++bad)
    {
        text.Push(*bad);
    }
    TextureSource source;
    TextureSourceError error;
    CHECK(ParseTextureSource(StringView(text.Data(), text.Size()), &source, &error).Failed());
}

TEST_CASE("bc minter: the same source mints the same bytes on any worker count, and retargeting equals minting",
          "[asset][bc]")
{
    TypeRegistry types;
    const Vector<byte> img = BtNoise(37, 29, 4);
    const Vector<byte> serial = BtMintText("format bc5\n", 37, 29, img);
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    const Vector<byte> parallel = BtMintText("format bc5\n", 37, 29, img, 0, &scheduler);
    REQUIRE(parallel.Size() == serial.Size());
    CHECK(MemCompare(parallel.Data(), serial.Data(), serial.Size()) == 0);

    // The Direct3D 12 layout (target 1): the rows are 256-byte aligned block rows, and a retarget equals a mint.
    const Vector<byte> d3d = BtMintText("format bc1\n", 37, 29, img, 1);
    TextureView view;
    REQUIRE(view.Open(ByteSpan(d3d.Data(), d3d.Size()), types).Ok());
    CHECK(view.Level(0, 0).row_pitch == 256);
    Vector<byte> dev = BtMintText("format bc1\n", 37, 29, img, 0);
    Vector<byte> retargeted;
    REQUIRE(RetargetTextureBlob(types, ByteSpan(dev.Data(), dev.Size()), 1, &retargeted).Ok());
    REQUIRE(retargeted.Size() == d3d.Size());
    CHECK(MemCompare(retargeted.Data(), d3d.Data(), d3d.Size()) == 0);
}

TEST_CASE("bc: a compressed blob with a mismatched format is refused and one uploads to the device", "[asset][bc][gpu]")
{
    TypeRegistry types;
    const Vector<byte> img = BtGradient(16, 16);
    const Vector<byte> blob = BtMintText("format bc1\n", 16, 16, img);

    // The same bytes under an RGBA8 format name do not have the pitches of RGBA8 levels.
    TextureView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan header;
    REQUIRE(reader.FindSection(16, &header));
    Vector<byte> changed = BtBytes(header.Data(), header.Size());
    changed[4] = static_cast<byte>(static_cast<u32>(rhi::Format::RGBA8_UNorm));
    changed[5] = changed[6] = changed[7] = 0;
    BlobWriter writer(types, types::kTexture, view.Id(), 0);
    ByteSpan levels;
    ByteSpan data;
    REQUIRE(reader.FindSection(17, &levels));
    REQUIRE(reader.FindSection(18, &data));
    REQUIRE(writer.AddSection(16, ByteSpan(changed.Data(), changed.Size())).Ok());
    REQUIRE(writer.AddSection(17, levels).Ok());
    REQUIRE(writer.AddSection(18, data).Ok());
    Vector<byte> hostile;
    REQUIRE(writer.Finish(&hostile).Ok());
    TextureView refused;
    CHECK(refused.Open(ByteSpan(hostile.Data(), hostile.Size()), types).Failed());

    // The upload: every level goes up with its block-row pitch.
    Vector<renderer::TextureLevelRef> refs;
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        const TextureLevel level = view.Level(0, mip);
        renderer::TextureLevelRef ref;
        ref.data = level.bytes.Data();
        ref.row_pitch = level.row_pitch;
        ref.mip = mip;
        ref.layer = 0;
        refs.Push(ref);
    }
    const bool was = LogRender.Enabled();
    LogRender.SetEnabled(false);
    renderer::DeviceResources registry;
    rhi::NullDevicePtr device = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(registry.OnDeviceCreated(device.Get(), device->Epoch()));
    renderer::GpuTexture texture;
    CHECK(texture.CreateFromLevels(device.Get(), view.Desc(rhi::TextureUsage::Sampled),
                                   Span<const renderer::TextureLevelRef>(refs.Data(), refs.Size()), &registry, "bc"));
    CHECK(texture.Desc().format == rhi::Format::BC1_UNorm);
    texture.Destroy();
    LogRender.SetEnabled(was);
}

}  // namespace engine_placeholder::asset
