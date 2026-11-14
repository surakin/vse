// The texture format, mip generation, the source form and the minter (Phase 22 Day 323).

#include <catch2/catch_test_macros.hpp>

#include "asset/texture.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "renderer/device_resources.h"
#include "renderer/renderer_log.h"
#include "renderer/texture.h"
#include "rhi/null/null_device.h"
#include "support.h"
#include "sync/thread.h"
#include "texture_source.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr const char* kTtId = "id 00000000000000cc\n";

Vector<byte> TtCopy(const Vector<byte>& bytes)
{
    Vector<byte> out;
    out.Resize(bytes.Size());
    MemCopy(out.Data(), bytes.Data(), bytes.Size());
    return out;
}

Vector<byte> TtBytes(const byte* data, usize size)
{
    Vector<byte> out;
    out.Resize(size);
    MemCopy(out.Data(), data, size);
    return out;
}

u64 TtFnv(const Vector<byte>& bytes)
{
    u64 h = 0xcbf29ce484222325ull;
    for (usize i = 0; i < bytes.Size(); ++i)
    {
        h = (h ^ bytes[i]) * 0x100000001b3ull;
    }
    return h;
}

// A pseudo-random RGBA8 image.
Vector<byte> TtNoise(u32 w, u32 h, u64 seed)
{
    test::Rng rng(seed);
    Vector<byte> out;
    out.Resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < out.Size(); ++i)
    {
        out[i] = static_cast<byte>(rng.Next());
    }
    return out;
}

Vector<byte> TtRaw(u32 w, u32 h, const Vector<byte>& rgba)
{
    Vector<byte> out;
    REQUIRE(BuildRawImage(w, h, ByteSpan(rgba.Data(), rgba.Size()), 0, &out).Ok());
    return out;
}

// The text of a texture source with one `source` record per name in `files` (the names are not read by MintTexture).
Vector<char> TtSource(const char* extra, u32 files = 1)
{
    Vector<char> text;
    const char* id = kTtId;
    for (; *id != '\0'; ++id)
    {
        text.Push(*id);
    }
    for (u32 i = 0; i < files; ++i)
    {
        const char* line = "source a.raw\n";
        for (; *line != '\0'; ++line)
        {
            text.Push(*line);
        }
    }
    for (; *extra != '\0'; ++extra)
    {
        text.Push(*extra);
    }
    return text;
}

StringView TtView(const Vector<char>& v)
{
    return StringView(v.Data(), v.Size());
}

Status TtMint(const Vector<char>& text, const Vector<Vector<byte>>& images, Vector<byte>* out, u32 target = 0,
              TextureSourceError* error_out = nullptr)
{
    TypeRegistry types;
    Vector<ByteSpan> spans;
    for (usize i = 0; i < images.Size(); ++i)
    {
        spans.Push(ByteSpan(images[i].Data(), images[i].Size()));
    }
    TextureSourceError error;
    const Status s = MintTexture(TtView(text), Span<const ByteSpan>(spans.Data(), spans.Size()), types, out, &error,
                                 target);
    if (error_out != nullptr)
    {
        *error_out = error;
    }
    return s;
}

Vector<byte> TtMintOne(const char* extra, u32 w, u32 h, const Vector<byte>& rgba, u32 target = 0)
{
    Vector<Vector<byte>> images;
    images.Push(TtRaw(w, h, rgba));
    Vector<byte> blob;
    const Status s = TtMint(TtSource(extra), images, &blob, target);
    INFO(s.Message());
    REQUIRE(s.Ok());
    return blob;
}

Vector<byte> TtSection(const TypeRegistry& types, const Vector<byte>& blob, u32 id)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan data;
    REQUIRE(reader.FindSection(id, &data));
    return TtBytes(data.Data(), data.Size());
}

// Rebuilds `blob` with section `id` replaced.
Vector<byte> TtWith(const TypeRegistry& types, const Vector<byte>& blob, u32 id, const Vector<byte>& replacement)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    BlobWriter writer(types, types::kTexture, reader.Id(), reader.Target());
    for (u32 section = kFirstUserSection; section < 24; ++section)
    {
        ByteSpan data;
        if (section == id)
        {
            REQUIRE(writer.AddSection(section, ByteSpan(replacement.Data(), replacement.Size())).Ok());
        }
        else if (reader.FindSection(section, &data))
        {
            REQUIRE(writer.AddSection(section, data).Ok());
        }
    }
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

Status TtOpen(const TypeRegistry& types, const Vector<byte>& blob)
{
    TextureView view;
    return view.Open(ByteSpan(blob.Data(), blob.Size()), types);
}

// The tight chain of a minted blob, level 0 first, for one layer.
Vector<byte> TtTightChain(const TextureView& view, u32 layer)
{
    Vector<byte> out;
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        const TextureLevel level = view.Level(layer, mip);
        const usize row = static_cast<usize>(TightLevelBytes(view.Format(), level.width, 1));
        for (u32 y = 0; y < level.height; ++y)
        {
            for (usize b = 0; b < row; ++b)
            {
                out.Push(level.bytes[static_cast<usize>(y) * level.row_pitch + b]);
            }
        }
    }
    return out;
}

Vector<byte> TtChain(rhi::Format format, u32 w, u32 h, const Vector<byte>& level0, MipFilter filter = MipFilter::Box,
                     MipAlpha alpha = MipAlpha::Straight)
{
    Vector<byte> chain;
    MipOptions options;
    options.filter = filter;
    options.alpha = alpha;
    const Status s = GenerateMipChain(format, w, h, ByteSpan(level0.Data(), level0.Size()), options, &chain);
    INFO(s.Message());
    REQUIRE(s.Ok());
    return chain;
}

}  // namespace

TEST_CASE("texture: the layouts of the targets and the full chain length", "[asset][texture]")
{
    TextureLayout layout;
    REQUIRE(TextureLayoutForTarget(0, &layout).Ok());
    CHECK(layout.row_alignment == 4);
    REQUIRE(TextureLayoutForTarget(1, &layout).Ok());
    CHECK(layout.row_alignment == 256);
    CHECK(layout.subresource_alignment == 512);
    REQUIRE(TextureLayoutForTarget(2, &layout).Ok());
    CHECK(TextureLayoutForTarget(77, &layout).Code() == ErrorCode::Invalid);
    CHECK(FullMipCount(1, 1) == 1);
    CHECK(FullMipCount(256, 64) == 9);
    CHECK(FullMipCount(5, 3) == 3);
    CHECK(FullMipCount(1, 7) == 3);
    CHECK(TightLevelBytes(rhi::Format::RGBA16_Float, 3, 2) == 48);
    CHECK(TightLevelBytes(rhi::Format::BC7_UNorm, 4, 4) == 16);
    CHECK(TightLevelBytes(rhi::Format::BC1_UNorm, 5, 5) == 32);
    CHECK(TightLevelBytes(rhi::Format::BC3_UNorm, 4, 4) == 0);  // decodable, but the minter has no BC3 encoder
}

TEST_CASE("mips: the box filter averages 2x2 texels exactly and halves odd sizes", "[asset][texture][mips]")
{
    // A 4x4 gradient: v = 10 x + 20 y.
    Vector<byte> image;
    for (u32 y = 0; y < 4; ++y)
    {
        for (u32 x = 0; x < 4; ++x)
        {
            image.Push(static_cast<byte>(10 * x + 20 * y));
        }
    }
    const Vector<byte> chain = TtChain(rhi::Format::R8_UNorm, 4, 4, image);
    REQUIRE(chain.Size() == 16 + 4 + 1);
    for (u32 y = 0; y < 2; ++y)
    {
        for (u32 x = 0; x < 2; ++x)
        {
            const u32 sum = image[(2 * y) * 4 + 2 * x] + image[(2 * y) * 4 + 2 * x + 1] +
                            image[(2 * y + 1) * 4 + 2 * x] + image[(2 * y + 1) * 4 + 2 * x + 1];
            CHECK(chain[16 + y * 2 + x] == (sum + 2) / 4);
        }
    }
    CHECK(chain[20] == (chain[16] + chain[17] + chain[18] + chain[19] + 2) / 4);

    // 5 x 3: the last column of the next level covers three texels, the last row two.
    Vector<byte> odd;
    for (u32 i = 0; i < 15; ++i)
    {
        odd.Push(static_cast<byte>(i * 16));
    }
    const Vector<byte> odd_chain = TtChain(rhi::Format::R8_UNorm, 5, 3, odd);
    REQUIRE(odd_chain.Size() == 15 + 2 + 1);  // 5x3, 2x1, 1x1
    // Texel 0 of the 2x1 level covers x 0..1 of all three rows (six texels); the rounding is in linear 16-bit light.
    const i32 six = static_cast<i32>(odd[0] + odd[1] + odd[5] + odd[6] + odd[10] + odd[11]);
    CHECK(odd_chain[15] >= (six - 3) / 6);
    CHECK(odd_chain[15] <= (six + 3) / 6 + 1);
    // Texel 1 covers x 2..4 (three columns) of all rows.
    const i32 nine = static_cast<i32>(odd[2] + odd[3] + odd[4] + odd[7] + odd[8] + odd[9] + odd[12] + odd[13] +
                                      odd[14]);
    CHECK(odd_chain[16] >= (nine - 4) / 9);
    CHECK(odd_chain[16] <= (nine + 4) / 9 + 1);
}

TEST_CASE("mips: sizes of non-power-of-two and one-texel-wide images", "[asset][texture][mips]")
{
    struct Case
    {
        u32 w, h, levels;
        u64 bytes;
    };
    // R8: 7x5 -> 3x2 -> 1x1; 1x7 -> 1x3 -> 1x1; 9x1 -> 4x1 -> 2x1 -> 1x1.
    const Case cases[] = {{7, 5, 3, 35 + 6 + 1}, {1, 7, 3, 7 + 3 + 1}, {9, 1, 4, 9 + 4 + 2 + 1}};
    for (const Case& c : cases)
    {
        Vector<byte> image;
        image.Resize(static_cast<usize>(c.w) * c.h);
        for (usize i = 0; i < image.Size(); ++i)
        {
            image[i] = static_cast<byte>(i * 9 + 1);
        }
        CHECK(FullMipCount(c.w, c.h) == c.levels);
        const Vector<byte> chain = TtChain(rhi::Format::R8_UNorm, c.w, c.h, image);
        CHECK(chain.Size() == c.bytes);
        // A one-texel-wide image keeps its column values' averages: the mean of the whole is the last texel (+- 1).
        u32 sum = 0;
        for (usize i = 0; i < image.Size(); ++i)
        {
            sum += image[i];
        }
        const i32 mean = static_cast<i32>((sum + image.Size() / 2) / image.Size());
        const i32 last = chain[chain.Size() - 1];
        CHECK((last - mean <= 16 && mean - last <= 16));  // unequal footprints weigh the texels unevenly, not exactly
    }
}

TEST_CASE("mips: sRGB data is filtered in linear light and uniform colors survive every level",
          "[asset][texture][mips]")
{
    // encode(decode(v)) == v for every sRGB value: a uniform 2x2 image of v has level 1 equal to v.
    for (u32 v = 0; v < 256; ++v)
    {
        Vector<byte> image;
        for (u32 t = 0; t < 4; ++t)
        {
            image.Push(static_cast<byte>(v));
            image.Push(static_cast<byte>(255 - v));
            image.Push(static_cast<byte>(v / 2));
            image.Push(255);
        }
        const Vector<byte> chain = TtChain(rhi::Format::RGBA8_SRGB, 2, 2, image);
        REQUIRE(chain.Size() == 16 + 4);
        CHECK(chain[16] == v);
        CHECK(chain[17] == 255 - v);
        CHECK(chain[18] == v / 2);
        CHECK(chain[19] == 255);
    }
    // Black and white average to 0.5 in linear light: sRGB 188 (0.5 linear is 0.7354 encoded, 187.5), not 128.
    Vector<byte> bw;
    const byte texels[8] = {0, 0, 0, 255, 255, 255, 255, 255};
    for (byte b : texels)
    {
        bw.Push(b);
    }
    const Vector<byte> chain = TtChain(rhi::Format::RGBA8_SRGB, 2, 1, bw);
    CHECK((chain[8] == 187 || chain[8] == 188));
    // The same pixels as linear data average to 128.
    const Vector<byte> linear = TtChain(rhi::Format::RGBA8_UNorm, 2, 1, bw);
    CHECK(linear[8] == 128);
}

TEST_CASE("mips: alpha weighting, premultiplication and ignoring alpha", "[asset][texture][mips]")
{
    // An opaque red texel beside a transparent blue one.
    Vector<byte> image;
    const byte texels[8] = {255, 0, 0, 255, 0, 0, 255, 0};
    for (byte b : texels)
    {
        image.Push(b);
    }
    const Vector<byte> straight = TtChain(rhi::Format::RGBA8_UNorm, 2, 1, image, MipFilter::Box, MipAlpha::Straight);
    CHECK(straight[8] == 255);  // the transparent texel's blue does not bleed in
    CHECK(straight[9] == 0);
    CHECK(straight[10] == 0);
    CHECK(straight[11] == 128);
    const Vector<byte> ignore = TtChain(rhi::Format::RGBA8_UNorm, 2, 1, image, MipFilter::Box, MipAlpha::Ignore);
    CHECK(ignore[8] == 128);
    CHECK(ignore[10] == 128);
    CHECK(ignore[11] == 128);

    Vector<byte> premultiplied = TtCopy(image);
    REQUIRE(PremultiplyAlphaRgba8(false, MutableByteSpan(premultiplied.Data(), premultiplied.Size())).Ok());
    CHECK(premultiplied[0] == 255);
    CHECK(premultiplied[6] == 0);  // blue times alpha 0
    const Vector<byte> chain = TtChain(rhi::Format::RGBA8_UNorm, 2, 1, premultiplied, MipFilter::Box,
                                       MipAlpha::Premultiply);
    CHECK(chain[8] == 128);
    CHECK(chain[10] == 0);
    CHECK(chain[11] == 128);
    // Half alpha halves the color; in sRGB it does so in linear light.
    Vector<byte> half_alpha;
    const byte gray[4] = {255, 255, 255, 128};
    for (byte b : gray)
    {
        half_alpha.Push(b);
    }
    REQUIRE(PremultiplyAlphaRgba8(true, MutableByteSpan(half_alpha.Data(), half_alpha.Size())).Ok());
    CHECK(half_alpha[0] > 180);
    CHECK(half_alpha[0] < 195);
    CHECK(half_alpha[3] == 128);
    Vector<byte> bad;
    bad.Resize(3);
    CHECK(PremultiplyAlphaRgba8(false, MutableByteSpan(bad.Data(), bad.Size())).Code() == ErrorCode::Invalid);
}

TEST_CASE("mips: half-float texels are filtered exactly and non-finite ones are refused", "[asset][texture][mips]")
{
    // 1.0 (0x3c00) and 0.0 average to 0.5 (0x3800); 2.0 (0x4000) and 1.0 average to 1.5 (0x3e00).
    Vector<byte> image;
    const byte texels[8] = {0x00, 0x3c, 0x00, 0x00, 0x00, 0x40, 0x00, 0x3c};
    for (byte b : texels)
    {
        image.Push(b);
    }
    const Vector<byte> chain = TtChain(rhi::Format::R16_Float, 2, 2, image, MipFilter::Box, MipAlpha::Ignore);
    REQUIRE(chain.Size() == 8 + 2);
    // (1 + 0 + 2 + 1) / 4 = 1.0
    CHECK(chain[8] == 0x00);
    CHECK(chain[9] == 0x3c);
    Vector<byte> two;
    const byte pair[4] = {0x00, 0x3c, 0x00, 0x00};
    for (byte b : pair)
    {
        two.Push(b);
    }
    const Vector<byte> mean = TtChain(rhi::Format::R16_Float, 2, 1, two, MipFilter::Box, MipAlpha::Ignore);
    CHECK(mean[4] == 0x00);
    CHECK(mean[5] == 0x38);

    Vector<byte> inf;
    for (byte b : {byte{0x00}, byte{0x7c}, byte{0x00}, byte{0x00}})
    {
        inf.Push(b);
    }
    Vector<byte> out;
    MipOptions options;
    options.alpha = MipAlpha::Ignore;
    CHECK(GenerateMipChain(rhi::Format::R16_Float, 2, 1, ByteSpan(inf.Data(), inf.Size()), options, &out).Code() ==
          ErrorCode::Invalid);
    options.alpha = MipAlpha::Straight;
    CHECK(GenerateMipChain(rhi::Format::R16_Float, 2, 1, ByteSpan(two.Data(), two.Size()), options, &out).Code() ==
          ErrorCode::Invalid);

    // RGBA8 -> half: 255 is exactly 1.0, 0 is 0, and sRGB 255 is linear 1.0 too.
    Vector<byte> rgba;
    for (byte b : {byte{255}, byte{0}, byte{255}, byte{255}})
    {
        rgba.Push(b);
    }
    Vector<byte> converted;
    REQUIRE(ConvertRgba8(rhi::Format::RGBA16_Float, true, 1, 1, ByteSpan(rgba.Data(), rgba.Size()), &converted).Ok());
    REQUIRE(converted.Size() == 8);
    CHECK(converted[1] == 0x3c);
    CHECK(converted[0] == 0);
    CHECK(converted[3] == 0);
    CHECK(converted[5] == 0x3c);
    CHECK(converted[7] == 0x3c);
}

TEST_CASE("mips: the Kaiser filter keeps uniform images, clamps overshoot and differs from the box",
          "[asset][texture][mips]")
{
    Vector<byte> uniform;
    uniform.Resize(8 * 8 * 4);
    for (usize i = 0; i < uniform.Size(); ++i)
    {
        uniform[i] = static_cast<byte>(i % 4 == 3 ? 255 : 77);
    }
    const Vector<byte> chain = TtChain(rhi::Format::RGBA8_UNorm, 8, 8, uniform, MipFilter::Kaiser);
    for (usize i = 8 * 8 * 4; i < chain.Size(); i += 4)
    {
        CHECK(chain[i] == 77);
        CHECK(chain[i + 3] == 255);
    }
    // A hard edge overshoots without wrapping: black / white texels, 8 wide.
    Vector<byte> edge;
    for (u32 x = 0; x < 8; ++x)
    {
        edge.Push(x < 4 ? 0 : 255);
    }
    const Vector<byte> kaiser = TtChain(rhi::Format::R8_UNorm, 8, 1, edge, MipFilter::Kaiser);
    const Vector<byte> box = TtChain(rhi::Format::R8_UNorm, 8, 1, edge, MipFilter::Box);
    CHECK(kaiser[8] == 0);     // clamped, not negative
    CHECK(kaiser[11] == 255);  // clamped, not above
    CHECK(box[8] == 0);
    CHECK(box[11] == 255);
    bool differs = false;
    for (u32 i = 8; i < 12; ++i)
    {
        differs = differs || kaiser[i] != box[i];
    }
    const Vector<byte> noise = TtNoise(16, 16, 5);
    CHECK((differs || TtFnv(TtChain(rhi::Format::RGBA8_UNorm, 16, 16, noise, MipFilter::Kaiser)) !=
                          TtFnv(TtChain(rhi::Format::RGBA8_UNorm, 16, 16, noise, MipFilter::Box))));
}

TEST_CASE("mips: golden hashes of the chains of a gradient, a checker and noise", "[asset][texture][mips]")
{
    Vector<byte> gradient;
    Vector<byte> checker;
    for (u32 y = 0; y < 32; ++y)
    {
        for (u32 x = 0; x < 32; ++x)
        {
            for (byte b : {static_cast<byte>(x * 8), static_cast<byte>(y * 8), static_cast<byte>(x + y), byte{255}})
            {
                gradient.Push(b);
            }
            const byte c = ((x / 4 + y / 4) % 2 == 0) ? 240 : 16;
            for (byte b : {c, c, c, byte{255}})
            {
                checker.Push(b);
            }
        }
    }
    const Vector<byte> noise = TtNoise(37, 23, 99);
    // Recorded from this implementation (integer arithmetic only): a change here is a change to every minted texture.
    CHECK(TtFnv(TtChain(rhi::Format::RGBA8_SRGB, 32, 32, gradient)) == 0x8da45f69a83f1c8eull);
    CHECK(TtFnv(TtChain(rhi::Format::RGBA8_UNorm, 32, 32, checker, MipFilter::Kaiser)) == 0x6e3ac9499a606088ull);
    CHECK(TtFnv(TtChain(rhi::Format::RGBA8_SRGB, 37, 23, noise)) == 0x3fdedac49ab56455ull);
    CHECK(TtFnv(TtChain(rhi::Format::RGBA8_UNorm, 37, 23, noise, MipFilter::Kaiser)) == 0xf164a20054f9b198ull);
}

TEST_CASE("texture source: the parser's errors name their lines", "[asset][texture][source]")
{
    struct Bad
    {
        const char* text;
        u32 line;
        const char* word;
    };
    const Bad bad[] = {
        {"source a.raw\n", 1, "id line"},
        {"id 00000000000000cc\nid 00000000000000cd\nsource a.raw\n", 2, "twice"},
        {"id 00000000000000cc\nsource a.raw\nbogus 1\n", 3, "unknown"},
        {"id 00000000000000cc\nsource a.raw\nformat rgba4\n", 3, "format is"},
        {"id 00000000000000cc\nsource a.raw\nkind sphere\n", 3, "kind is"},
        {"id 00000000000000cc\nsource a.raw\nmips 0\n", 3, "mips is"},
        {"id 00000000000000cc\nsource a.raw\nmips 16\n", 3, "mips is"},
        {"id 00000000000000cc\nsource a.raw\nfilter lanczos\n", 3, "filter is"},
        {"id 00000000000000cc\nsource a.raw\nalpha both\n", 3, "alpha is"},
        {"id 00000000000000cc\nsource a.raw\nsrgb maybe\n", 3, "srgb is"},
        {"id 00000000000000cc\nsource a.raw\nsource b.raw\n", 0, "number of source"},
        {"id 00000000000000cc\nkind cube\nsource a.raw\n", 0, "number of source"},
        {"id 00000000000000cc\nsource a.raw\nformat r8\nsrgb true\n", 0, "srgb needs"},
        {"id 00000000000000cc\nsource a.raw\nformat r16f\nalpha straight\n", 0, "straight alpha"},
        {"id 00000000000000cc\nsource a.raw\nformat r8\nformat rg8\n", 4, "twice"},
        {"id 00000000000000cc\nsource\n", 2, "one file name"},
    };
    for (const Bad& b : bad)
    {
        TextureSource source;
        TextureSourceError error;
        const Status s = ParseTextureSource(StringView(b.text, CStrLength(b.text)), &source, &error);
        INFO(b.text);
        CHECK(s.Code() == ErrorCode::Invalid);
        CHECK(error.line == b.line);
        CHECK(StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(b.word)));
    }
    TextureSource ok;
    TextureSourceError error;
    const char* text = "# a comment\nid 00000000000000cc\nkind array\nsource a.raw\nsource b.raw\nformat rgba8\n"
                       "srgb true\nmips 3\nfilter kaiser\nalpha premultiply\n";
    REQUIRE(ParseTextureSource(StringView(text, CStrLength(text)), &ok, &error).Ok());
    CHECK(ok.kind == TextureKind::Array2D);
    CHECK(ok.sources.Size() == 2);
    CHECK(ok.format == rhi::Format::RGBA8_SRGB);
    CHECK(ok.mips == 3);
    CHECK(ok.filter == MipFilter::Kaiser);
    CHECK(ok.alpha == MipAlpha::Premultiply);
    // The defaults: rgba8 straight, half floats ignore alpha.
    const char* defaults = "id 00000000000000cc\nsource a.raw\nformat r16f\n";
    REQUIRE(ParseTextureSource(StringView(defaults, CStrLength(defaults)), &ok, &error).Ok());
    CHECK(ok.alpha == MipAlpha::Ignore);
    CHECK(ok.format == rhi::Format::R16_Float);
}

TEST_CASE("raw images: the container round trips and refuses damage", "[asset][texture][source]")
{
    const Vector<byte> pixels = TtNoise(3, 2, 1);
    Vector<byte> raw;
    REQUIRE(BuildRawImage(3, 2, ByteSpan(pixels.Data(), pixels.Size()), 0xabcdef, &raw).Ok());
    RawImage image;
    REQUIRE(ParseRawImage(ByteSpan(raw.Data(), raw.Size()), &image).Ok());
    CHECK(image.width == 3);
    CHECK(image.height == 2);
    CHECK(image.id == 0xabcdef);
    CHECK(MemCompare(image.pixels.Data(), pixels.Data(), pixels.Size()) == 0);
    Vector<byte> bad = TtCopy(raw);
    bad[0] = 'X';
    CHECK(ParseRawImage(ByteSpan(bad.Data(), bad.Size()), &image).Code() == ErrorCode::Corrupt);
    CHECK(ParseRawImage(ByteSpan(raw.Data(), raw.Size() - 1), &image).Code() == ErrorCode::Corrupt);
    bad = TtCopy(raw);
    bad[7] = 0xff;  // a width of 2^32 - something
    CHECK(ParseRawImage(ByteSpan(bad.Data(), bad.Size()), &image).Code() == ErrorCode::Corrupt);
    CHECK(BuildRawImage(0, 2, ByteSpan(pixels.Data(), pixels.Size()), 0, &raw).Code() == ErrorCode::Invalid);
    CHECK(ParseRawImage(ByteSpan(raw.Data(), 5), &image).Code() == ErrorCode::Corrupt);
}

TEST_CASE("texture minter: a 2D texture round trips through the view with its mips", "[asset][texture]")
{
    TypeRegistry types;
    const Vector<byte> noise = TtNoise(13, 9, 3);
    const Vector<byte> blob = TtMintOne("srgb true\n", 13, 9, noise);
    TextureView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.Id() == ResourceId::FromValue(0xcc));
    CHECK(view.Kind() == TextureKind::Tex2D);
    CHECK(view.Format() == rhi::Format::RGBA8_SRGB);
    CHECK(view.Srgb());
    CHECK(view.Width() == 13);
    CHECK(view.Height() == 9);
    CHECK(view.Layers() == 1);
    CHECK(view.MipCount() == 4);  // 13x9, 6x4, 3x2, 1x1
    CHECK(!view.Premultiplied());
    // The levels hold the generated chain, with the rows padded to the target's pitch.
    const Vector<byte> chain = TtChain(rhi::Format::RGBA8_SRGB, 13, 9, noise);
    const Vector<byte> stored = TtTightChain(view, 0);
    REQUIRE(stored.Size() == chain.Size());
    CHECK(MemCompare(stored.Data(), chain.Data(), chain.Size()) == 0);
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        const TextureLevel level = view.Level(0, mip);
        CHECK(level.row_pitch % 4 == 0);
        CHECK(level.row_pitch >= level.width * 4);
        CHECK(level.bytes.Size() == static_cast<usize>(level.row_pitch) * level.height);
        CHECK(reinterpret_cast<usize>(level.bytes.Data()) % 16 == 0);
    }
    const rhi::TextureDesc desc = view.Desc(rhi::TextureUsage::Sampled);
    CHECK(desc.mip_levels == 4);
    CHECK(desc.array_layers == 1);
    CHECK(desc.format == rhi::Format::RGBA8_SRGB);
    CHECK(desc.Validate() == nullptr);

    // Fewer mips than the full chain.
    const Vector<byte> two = TtMintOne("mips 2\n", 13, 9, noise);
    TextureView two_view;
    REQUIRE(two_view.Open(ByteSpan(two.Data(), two.Size()), types).Ok());
    CHECK(two_view.MipCount() == 2);
}

TEST_CASE("texture minter: formats, a cube map and an array", "[asset][texture]")
{
    TypeRegistry types;
    const char* formats[] = {"format r8\n",
                             "format rg8\n",
                             "format rgba8\n",
                             "format rgba8\nsrgb true\n",
                             "format r16f\n",
                             "format rgba16f\n",
                             "format rgba16f\nsrgb true\nalpha premultiply\n"};
    const rhi::Format expected[] = {rhi::Format::R8_UNorm,    rhi::Format::RG8_UNorm, rhi::Format::RGBA8_UNorm,
                                    rhi::Format::RGBA8_SRGB,  rhi::Format::R16_Float, rhi::Format::RGBA16_Float,
                                    rhi::Format::RGBA16_Float};
    const Vector<byte> noise = TtNoise(8, 8, 11);
    for (u32 i = 0; i < 7; ++i)
    {
        const Vector<byte> blob = TtMintOne(formats[i], 8, 8, noise);
        TextureView view;
        INFO(formats[i]);
        REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
        CHECK(view.Format() == expected[i]);
        CHECK(view.MipCount() == 4);
    }

    Vector<Vector<byte>> faces;
    for (u32 f = 0; f < 6; ++f)
    {
        const Vector<byte> face = TtNoise(4, 4, 100 + f);
        faces.Push(TtRaw(4, 4, face));
    }
    Vector<byte> cube;
    REQUIRE(TtMint(TtSource("kind cube\n", 6), faces, &cube).Ok());
    TextureView view;
    REQUIRE(view.Open(ByteSpan(cube.Data(), cube.Size()), types).Ok());
    CHECK(view.Kind() == TextureKind::Cube);
    CHECK(view.Layers() == 6);
    CHECK(view.MipCount() == 3);
    CHECK(view.Desc(rhi::TextureUsage::Sampled).array_layers == 6);
    // Faces keep their own contents.
    for (u32 f = 0; f < 6; ++f)
    {
        const Vector<byte> face = TtNoise(4, 4, 100 + f);
        const TextureLevel level0 = view.Level(f, 0);
        CHECK(MemCompare(level0.bytes.Data(), face.Data(), 16) == 0);
    }
    // A non-square cube, differing sizes and a wrong file count are refused with the source line.
    Vector<Vector<byte>> wide;
    for (u32 f = 0; f < 6; ++f)
    {
        wide.Push(TtRaw(4, 2, TtNoise(4, 2, f)));
    }
    Vector<byte> out;
    TextureSourceError error;
    CHECK(TtMint(TtSource("kind cube\n", 6), wide, &out, 0, &error).Code() == ErrorCode::Invalid);
    CHECK(error.line == 2);
    Vector<Vector<byte>> mixed;
    mixed.Push(TtRaw(4, 4, TtNoise(4, 4, 1)));
    mixed.Push(TtRaw(8, 4, TtNoise(8, 4, 2)));
    CHECK(TtMint(TtSource("kind array\n", 2), mixed, &out, 0, &error).Code() == ErrorCode::Invalid);
    CHECK(error.line == 3);
    CHECK(TtMint(TtSource("kind array\n", 3), mixed, &out, 0, &error).Code() == ErrorCode::Invalid);
    Vector<Vector<byte>> junk;
    junk.Push(TtBytes(reinterpret_cast<const byte*>("nope"), 4));
    CHECK(TtMint(TtSource(""), junk, &out, 0, &error).Code() == ErrorCode::Invalid);
    CHECK(error.line == 2);
    // More mips than the image has.
    Vector<Vector<byte>> one;
    one.Push(TtRaw(4, 4, TtNoise(4, 4, 1)));
    CHECK(TtMint(TtSource("mips 4\n"), one, &out, 0, &error).Code() == ErrorCode::Invalid);

    Vector<Vector<byte>> array_images;
    for (u32 i = 0; i < 3; ++i)
    {
        array_images.Push(TtRaw(4, 4, TtNoise(4, 4, 7 + i)));
    }
    Vector<byte> array_blob;
    REQUIRE(TtMint(TtSource("kind array\n", 3), array_images, &array_blob).Ok());
    TextureView array_view;
    REQUIRE(array_view.Open(ByteSpan(array_blob.Data(), array_blob.Size()), types).Ok());
    CHECK(array_view.Layers() == 3);
    CHECK(array_view.Kind() == TextureKind::Array2D);
}

TEST_CASE("texture minter: the target decides the layout, and retargeting equals minting for it", "[asset][texture]")
{
    TypeRegistry types;
    const Vector<byte> noise = TtNoise(21, 11, 8);
    const Vector<byte> dev = TtMintOne("", 21, 11, noise, 0);
    const Vector<byte> dx12 = TtMintOne("", 21, 11, noise, 1);
    const Vector<byte> metal = TtMintOne("", 21, 11, noise, 2);
    CHECK(dev.Size() != dx12.Size());
    TextureView view;
    REQUIRE(view.Open(ByteSpan(dx12.Data(), dx12.Size()), types).Ok());
    CHECK(view.Layout().row_alignment == 256);
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        const TextureLevel level = view.Level(0, mip);
        CHECK(level.row_pitch % 256 == 0);
    }
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(dx12.Data(), dx12.Size()), types).Ok());
    CHECK(reader.Target() == 1);

    Vector<byte> retargeted;
    REQUIRE(RetargetTextureBlob(types, ByteSpan(dev.Data(), dev.Size()), 1, &retargeted).Ok());
    REQUIRE(retargeted.Size() == dx12.Size());
    CHECK(MemCompare(retargeted.Data(), dx12.Data(), dx12.Size()) == 0);
    REQUIRE(RetargetTextureBlob(types, ByteSpan(dx12.Data(), dx12.Size()), 2, &retargeted).Ok());
    CHECK(retargeted.Size() == metal.Size());
    CHECK(MemCompare(retargeted.Data(), metal.Data(), metal.Size()) == 0);
    CHECK(RetargetTextureBlob(types, ByteSpan(dev.Data(), dev.Size()), 9, &retargeted).Code() == ErrorCode::Invalid);
}

TEST_CASE("texture minter: the same source mints the same bytes, also from two threads", "[asset][texture]")
{
    const Vector<byte> noise = TtNoise(37, 23, 4);
    const Vector<byte> first = TtMintOne("srgb true\nfilter kaiser\n", 37, 23, noise);
    const Vector<byte> second = TtMintOne("srgb true\nfilter kaiser\n", 37, 23, noise);
    REQUIRE(first.Size() == second.Size());
    CHECK(MemCompare(first.Data(), second.Data(), first.Size()) == 0);

    // The threads call the minter only: Catch's assertion state is not thread-safe, so the inputs are made here.
    Vector<Vector<byte>> images;
    images.Push(TtRaw(37, 23, noise));
    const Vector<char> text = TtSource("srgb true\nfilter kaiser\n");
    struct Job
    {
        const Vector<char>* text;
        const Vector<Vector<byte>>* images;
        Vector<byte> out;
        bool ok = false;
    };
    Job a{&text, &images, {}, false};
    Job b{&text, &images, {}, false};
    auto run = [](Job* job) { job->ok = TtMint(*job->text, *job->images, &job->out).Ok(); };
    Thread t1(run, &a);
    Thread t2(run, &b);
    t1.Join();
    t2.Join();
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.out.Size() == first.Size());
    REQUIRE(b.out.Size() == first.Size());
    CHECK(MemCompare(a.out.Data(), first.Data(), first.Size()) == 0);
    CHECK(MemCompare(b.out.Data(), first.Data(), first.Size()) == 0);
}

TEST_CASE("texture: building refuses what the format cannot hold", "[asset][texture]")
{
    TypeRegistry types;
    Vector<byte> level;
    level.Resize(16);
    const ByteSpan span(level.Data(), level.Size());
    TextureBuildInput input;
    input.format = rhi::Format::RGBA8_UNorm;
    input.width = 2;
    input.height = 2;
    input.mip_count = 1;
    input.subresources = Span<const ByteSpan>(&span, 1);
    Vector<byte> out;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), input, &out).Ok());

    TextureBuildInput bad = input;
    bad.format = rhi::Format::BC1_UNorm;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.width = 0;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.width = 20000;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.layers = 2;  // a 2D texture has one layer
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.kind = TextureKind::Cube;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.mip_count = 3;  // a 2x2 image has two levels
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    bad = input;
    bad.mip_count = 0;
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    Vector<byte> small;
    small.Resize(15);
    const ByteSpan small_span(small.Data(), small.Size());
    bad = input;
    bad.subresources = Span<const ByteSpan>(&small_span, 1);
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), bad, &out).Code() == ErrorCode::Invalid);
    CHECK(BuildTextureBlob(types, ResourceId::FromValue(1), input, &out, 99).Code() == ErrorCode::Invalid);
}

TEST_CASE("texture: hostile blobs are refused", "[asset][texture]")
{
    TypeRegistry types;
    const Vector<byte> good = TtMintOne("srgb true\n", 8, 4, TtNoise(8, 4, 2));
    REQUIRE(TtOpen(types, good).Ok());
    const Vector<byte> header = TtSection(types, good, kTextureSectionHeader);
    const Vector<byte> table = TtSection(types, good, kTextureSectionLevels);
    const Vector<byte> data = TtSection(types, good, kTextureSectionData);

    SECTION("header fields out of range or inconsistent")
    {
        struct Patch
        {
            u32 offset;
            byte value;
        };
        // kind 9; format 200; width 0; height 0xff...; layers 2 for a 2D; mips 9; filter 5; alpha 7; alignment 3 and 0.
        const Patch patches[] = {{0, 9},  {4, 200}, {8, 0},  {15, 0xff}, {16, 2},   {20, 9},
                                 {24, 5}, {28, 7},  {32, 3}, {36, 0},    {33, 0x20}};
        for (const Patch& p : patches)
        {
            Vector<byte> bad = TtCopy(header);
            bad[p.offset] = p.value;
            INFO(p.offset);
            CHECK(TtOpen(types, TtWith(types, good, kTextureSectionHeader, bad)).Failed());
        }
        Vector<byte> bad = TtCopy(header);
        bad[4] = static_cast<byte>(rhi::Format::BC1_UNorm);  // a format this version does not store
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionHeader, bad)).Code() == ErrorCode::Corrupt);
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionHeader, TtBytes(header.Data(), 60))).Failed());
    }
    SECTION("a level table or data section that does not fit")
    {
        Vector<byte> short_table = TtBytes(table.Data(), table.Size() - 16);
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionLevels, short_table)).Failed());
        for (u32 byte_index = 0; byte_index < 16; ++byte_index)
        {
            Vector<byte> bad = TtCopy(table);
            bad[byte_index] ^= 0x40;  // the first entry: offset, size and pitch
            INFO(byte_index);
            CHECK(TtOpen(types, TtWith(types, good, kTextureSectionLevels, bad)).Failed());
        }
        Vector<byte> overlapping = TtCopy(table);
        overlapping[16] = 0;  // the second subresource at offset 0: overlaps the first
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionLevels, overlapping)).Failed());
        Vector<byte> huge = TtCopy(table);
        huge[7] = 0x7f;  // an offset of 2^62
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionLevels, huge)).Failed());
        CHECK(TtOpen(types, TtWith(types, good, kTextureSectionData, TtBytes(data.Data(), data.Size() - 4))).Failed());
    }
    SECTION("another type, truncation and an old version")
    {
        BlobWriter other(types, types::kNames, ResourceId::FromValue(5), 0);
        Vector<byte> not_a_texture;
        REQUIRE(other.Finish(&not_a_texture).Ok());
        CHECK(TtOpen(types, not_a_texture).Failed());
        for (usize cut = 0; cut < good.Size(); cut += 13)
        {
            TextureView view;
            CHECK(view.Open(ByteSpan(good.Data(), cut), types).Failed());
        }
        Vector<byte> v0 = TtCopy(good);
        v0[12] = 0;
        const Status s = TtOpen(types, v0);
        CHECK(s.Code() == ErrorCode::UnsupportedVersion);
        CHECK(StringView(s.Message()).Contains(StringView("asset_mint remint")));
    }
    SECTION("flipped bytes never open into a view that reads past its blob")
    {
        test::Rng rng(77);
        for (u32 round = 0; round < 300; ++round)
        {
            Vector<byte> bad = TtCopy(good);
            bad[rng.Below(static_cast<u32>(bad.Size()))] ^= static_cast<byte>(1u << rng.Below(8));
            TextureView view;
            if (view.Open(ByteSpan(bad.Data(), bad.Size()), types).Ok())
            {
                for (u32 mip = 0; mip < view.MipCount(); ++mip)
                {
                    const TextureLevel level = view.Level(0, mip);
                    CHECK(level.bytes.Data() >= bad.Data());
                    CHECK(level.bytes.Data() + level.bytes.Size() <= bad.Data() + bad.Size());
                }
            }
        }
    }
}

TEST_CASE("texture: a blob uploads to the device from its own bytes and survives a reset", "[asset][texture][gpu]")
{
    TypeRegistry types;
    const Vector<byte> blob = TtMintOne("srgb true\n", 16, 8, TtNoise(16, 8, 6));
    TextureView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    const SharedBytesPtr shared = CopyShared(blob);  // the texture shares a copy of the blob
    Vector<renderer::TextureLevelSpan> refs;
    for (u32 mip = 0; mip < view.MipCount(); ++mip)
    {
        const TextureLevel level = view.Level(0, mip);
        renderer::TextureLevelSpan ref;
        ref.offset = static_cast<u64>(level.bytes.Data() - blob.Data());  // where it is in the blob
        ref.bytes = level.bytes.Size();
        ref.row_pitch = level.row_pitch;
        ref.mip = mip;
        ref.layer = 0;
        refs.Push(ref);
    }
    const Span<const renderer::TextureLevelSpan> levels(refs.Data(), refs.Size());

    const bool was = LogRender.Enabled();
    LogRender.SetEnabled(false);
    renderer::DeviceResources registry;
    rhi::NullDevicePtr first = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(registry.OnDeviceCreated(first.Get(), first->Epoch()));
    renderer::GpuTexture texture;
    REQUIRE(texture.CreateFromSharedLevels(first.Get(), view.Desc(rhi::TextureUsage::Sampled), shared, levels,
                                           &registry, "blob"));
    CHECK(texture.Desc().mip_levels == 5);
    CHECK(first->Resources().textures == 1);

    // Levels that are missing, repeated, out of range or without data are refused and leave nothing behind.
    renderer::GpuTexture refused;
    CHECK_FALSE(refused.CreateFromSharedLevels(first.Get(), view.Desc(rhi::TextureUsage::Sampled), shared,
                                               Span<const renderer::TextureLevelSpan>(refs.Data(), refs.Size() - 1),
                                               &registry, "short"));
    Vector<renderer::TextureLevelSpan> repeated;
    for (usize i = 0; i < refs.Size(); ++i)
    {
        repeated.Push(refs[i]);
    }
    repeated[2].mip = 1;
    CHECK_FALSE(refused.CreateFromSharedLevels(first.Get(), view.Desc(rhi::TextureUsage::Sampled), shared,
                                               Span<const renderer::TextureLevelSpan>(repeated.Data(), repeated.Size()),
                                               &registry, "repeat"));
    repeated[2] = refs[2];
    repeated[3].offset = ~0ull >> 1;  // outside the region
    CHECK_FALSE(refused.CreateFromSharedLevels(first.Get(), view.Desc(rhi::TextureUsage::Sampled), shared,
                                               Span<const renderer::TextureLevelSpan>(repeated.Data(), repeated.Size()),
                                               &registry, "null"));
    CHECK(first->Resources().textures == 1);

    // A reset: the texture is made again from the same blob bytes.
    registry.OnDeviceReleasing(first->Epoch());
    CHECK_FALSE(texture.Valid());
    rhi::NullDevicePtr second = rhi::CreateNullDevice(rhi::NullDeviceOptions{});
    REQUIRE(registry.OnDeviceCreated(second.Get(), second->Epoch()));
    CHECK(texture.Valid());
    CHECK(texture.Desc().mip_levels == 5);
    CHECK(second->Resources().textures == 1);
    texture.Destroy();
    CHECK(second->Resources().textures == 0);
    LogRender.SetEnabled(was);
}

}  // namespace engine_placeholder::asset
