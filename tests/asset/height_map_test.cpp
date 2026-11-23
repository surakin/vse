// Height maps as assets (Phase 28 Day 486): the blob, the mip chain, the block pyramid, the CPU sampler, hostile blobs
// and determinism.

#include <catch2/catch_test_macros.hpp>

#include "asset/blob.h"
#include "asset/height_map.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "height_source.h"
#include "support.h"

namespace vse::asset
{

namespace
{

using test::AsBytes;
using test::Rid;

Vector<byte> HmSamples(u32 w, u32 h, u32 (*f)(u32, u32))
{
    Vector<byte> out;
    out.Resize(static_cast<usize>(w) * h * 2);
    for (u32 z = 0; z < h; ++z)
    {
        for (u32 x = 0; x < w; ++x)
        {
            const u32 s = f(x, z);
            out[(static_cast<usize>(z) * w + x) * 2] = static_cast<byte>(s & 0xFF);
            out[(static_cast<usize>(z) * w + x) * 2 + 1] = static_cast<byte>(s >> 8);
        }
    }
    return out;
}

u32 HmRamp(u32 x, u32 z)
{
    return (x + 2 * z) * 1000;
}

u32 HmRandom(u32 x, u32 z)
{
    u32 v = x * 73856093u ^ z * 19349663u;
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return v & 0xFFFF;
}

HeightMapHeader HmHeader(u32 w, u32 h)
{
    HeightMapHeader header;
    header.width = w;
    header.height = h;
    header.range_min = 0.0f;
    header.range_max = 65535.0f;  // one metre a sample value
    header.cell_size = 2.0f;
    return header;
}

SharedBytesPtr HmShare(const Vector<byte>& v)
{
    return CopyShared(ByteSpan(v.Data(), v.Size()));
}

Vector<byte> HmBlob(u32 w, u32 h, u32 (*f)(u32, u32))
{
    TypeRegistry types;
    Vector<byte> samples = HmSamples(w, h, f);
    Vector<byte> blob;
    REQUIRE(BuildHeightMapBlob(types, Rid(7), HmHeader(w, h), AsBytes(samples), &blob).Ok());
    return blob;
}

}  // namespace

TEST_CASE("height map: mip count of a chain", "[asset][height_map]")
{
    CHECK(HeightMapMipCount(2, 2) == 2);
    CHECK(HeightMapMipCount(4, 4) == 3);
    CHECK(HeightMapMipCount(5, 2) == 3);
    CHECK(HeightMapMipCount(8192, 8192) == 14);
}

TEST_CASE("height map: build then view, the samples and the mips", "[asset][height_map]")
{
    TypeRegistry types;
    const Vector<byte> blob = HmBlob(4, 4, HmRamp);
    HeightMapView view;
    REQUIRE(view.Open(HmShare(blob), types).Ok());
    CHECK(view.Id() == Rid(7));
    CHECK(view.Header().width == 4);
    CHECK(view.MipCount() == 3);
    CHECK(view.MipWidth(1) == 2);
    CHECK(view.MipWidth(2) == 1);
    for (u32 z = 0; z < 4; ++z)
    {
        for (u32 x = 0; x < 4; ++x)
        {
            CHECK(view.Sample(x, z) == HmRamp(x, z));
        }
    }
    // level 1 sample (0, 0) is the rounded mean of (0,0) (1,0) (0,1) (1,1) = 0, 1000, 2000, 3000 -> 1500
    const byte* level1 = view.Bytes()->Bytes().Data() + view.MipOffset(1);
    CHECK((level1[0] | (level1[1] << 8)) == 1500);
    // level 2 is the mean of the four of level 1
    const byte* level2 = view.Bytes()->Bytes().Data() + view.MipOffset(2);
    const u32 l1[4] = {1500, 3500, 5500, 7500};
    CHECK((level2[0] | (level2[1] << 8)) == static_cast<int>((l1[0] + l1[1] + l1[2] + l1[3] + 2) >> 2));
}

TEST_CASE("height map: the pyramid matches a brute-force scan on random maps", "[asset][height_map]")
{
    TypeRegistry types;
    const u32 sizes[][2] = {{17, 17}, {9, 33}, {2, 2}, {20, 5}};
    for (const auto& size : sizes)
    {
        const Vector<byte> blob = HmBlob(size[0], size[1], HmRandom);
        HeightMapView view;
        REQUIRE(view.Open(HmShare(blob), types).Ok());
        CHECK(view.BlocksX() == (size[0] - 1 + 7) / 8);
        CHECK(view.BlocksZ() == (size[1] - 1 + 7) / 8);
        for (u32 bz = 0; bz < view.BlocksZ(); ++bz)
        {
            for (u32 bx = 0; bx < view.BlocksX(); ++bx)
            {
                u32 lo = 0xFFFF;
                u32 hi = 0;
                for (u32 z = bz * 8; z <= bz * 8 + 8 && z < size[1]; ++z)
                {
                    for (u32 x = bx * 8; x <= bx * 8 + 8 && x < size[0]; ++x)
                    {
                        const u32 s = view.Sample(x, z);
                        lo = s < lo ? s : lo;
                        hi = s > hi ? s : hi;
                    }
                }
                CHECK(view.BlockMin(bx, bz) == lo);
                CHECK(view.BlockMax(bx, bz) == hi);
            }
        }
    }
}

TEST_CASE("height map: HeightAt follows the two triangles of a cell", "[asset][height_map]")
{
    TypeRegistry types;
    const Vector<byte> blob = HmBlob(4, 4, HmRamp);
    HeightMapView view;
    REQUIRE(view.Open(HmShare(blob), types).Ok());
    // range 0..65535 metres: a sample value is its height in metres; the cell size is 2 m
    CHECK(Abs(view.HeightAt(0.0f, 0.0f) - 0.0f) < 1e-3f);
    CHECK(Abs(view.HeightAt(2.0f, 0.0f) - 1000.0f) < 1e-3f);
    CHECK(Abs(view.HeightAt(0.0f, 2.0f) - 2000.0f) < 1e-3f);
    // a ramp is a plane, so a point inside a cell is the plane's value whichever triangle it is in
    CHECK(Abs(view.HeightAt(1.0f, 1.0f) - (500.0f + 1000.0f)) < 1e-2f);
    CHECK(Abs(view.HeightAt(3.0f, 0.5f) - 2000.0f) < 1e-2f);
    // outside the map the point is clamped
    CHECK(Abs(view.HeightAt(-5.0f, -5.0f) - 0.0f) < 1e-3f);
    CHECK(Abs(view.HeightAt(100.0f, 100.0f) - HmRamp(3, 3)) < 1e-3f);
}

TEST_CASE("height map: HeightAt on a non-planar cell cuts along the minimum to maximum diagonal", "[asset][height_map]")
{
    TypeRegistry types;
    // one cell: h00 = 0, h10 = 1000, h01 = 4000, h11 = 2000 (not a plane)
    Vector<byte> samples;
    samples.Resize(8);
    const u32 v[4] = {0, 1000, 4000, 2000};
    for (u32 i = 0; i < 4; ++i)
    {
        samples[i * 2] = static_cast<byte>(v[i] & 0xFF);
        samples[i * 2 + 1] = static_cast<byte>(v[i] >> 8);
    }
    Vector<byte> blob;
    REQUIRE(BuildHeightMapBlob(types, Rid(1), HmHeader(2, 2), AsBytes(samples), &blob).Ok());
    HeightMapView view;
    REQUIRE(view.Open(HmShare(blob), types).Ok());
    // on the diagonal (fx == fz) the height is the mean of the diagonal's ends
    CHECK(Abs(view.HeightAt(1.0f, 1.0f) - 1000.0f) < 1e-2f);
    // below the diagonal (fx > fz): h00 + fx * (h10 - h00) + fz * (h11 - h10), fx = 0.75, fz = 0.25
    CHECK(Abs(view.HeightAt(1.5f, 0.5f) - (750.0f + 250.0f)) < 1e-2f);
    // above it (fz > fx): h00 + fz * (h01 - h00) + fx * (h11 - h01), fx = 0.25, fz = 0.75
    CHECK(Abs(view.HeightAt(0.5f, 1.5f) - (3000.0f - 500.0f)) < 1e-2f);
}

TEST_CASE("height map: NormalAt of a ramp and of a flat map", "[asset][height_map]")
{
    TypeRegistry types;
    HeightMapView ramp;
    const Vector<byte> blob = HmBlob(4, 4, HmRamp);
    REQUIRE(ramp.Open(HmShare(blob), types).Ok());
    // slope in x: 1000 m per 2 m cell = 500; in z: 2000 m per cell = 1000
    const Vec3 n = ramp.NormalAt(2.0f, 2.0f);
    const f32 len = Sqrt(500.0f * 500.0f + 1.0f + 1000.0f * 1000.0f);
    CHECK(Abs(n.x - (-500.0f / len)) < 1e-4f);
    CHECK(Abs(n.y - (1.0f / len)) < 1e-4f);
    CHECK(Abs(n.z - (-1000.0f / len)) < 1e-4f);
    const Vector<byte> flat = HmBlob(4, 4, [](u32, u32) -> u32 { return 300; });
    HeightMapView plane;
    REQUIRE(plane.Open(HmShare(flat), types).Ok());
    const Vec3 up = plane.NormalAt(3.0f, 3.0f);
    CHECK(up.x == 0.0f);
    CHECK(up.y == 1.0f);
    CHECK(up.z == 0.0f);
}

TEST_CASE("height map: building is deterministic", "[asset][height_map]")
{
    const Vector<byte> a = HmBlob(33, 17, HmRandom);
    const Vector<byte> b = HmBlob(33, 17, HmRandom);
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("height map: a header or samples that are refused", "[asset][height_map]")
{
    TypeRegistry types;
    Vector<byte> samples = HmSamples(4, 4, HmRamp);
    Vector<byte> blob;
    HeightMapHeader h = HmHeader(4, 4);
    h.width = 1;
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
    h = HmHeader(4, 4);
    h.width = kMaxHeightMapSide + 1;
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
    h = HmHeader(4, 4);
    h.range_min = 10.0f;
    h.range_max = 5.0f;
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
    h = HmHeader(4, 4);
    h.range_max = Limits<f32>::Infinity();
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
    h = HmHeader(4, 4);
    h.cell_size = 0.0f;
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
    h = HmHeader(4, 4);
    samples.Resize(samples.Size() - 2);
    CHECK(!BuildHeightMapBlob(types, Rid(1), h, AsBytes(samples), &blob).Ok());
}

namespace
{

// A blob with the given sections, as a hostile file would have.
Vector<byte> HmHostile(u32 w, u32 h, u32 mips, usize heights_bytes, usize block_bytes, u32 block_min, u32 block_max)
{
    TypeRegistry types;
    byte head[kHeightMapHeaderBytes] = {};
    auto put = [&](usize at, u32 v)
    {
        head[at] = static_cast<byte>(v & 0xFF);
        head[at + 1] = static_cast<byte>((v >> 8) & 0xFF);
        head[at + 2] = static_cast<byte>((v >> 16) & 0xFF);
        head[at + 3] = static_cast<byte>((v >> 24) & 0xFF);
    };
    put(0, w);
    put(4, h);
    put(8, mips);
    put(12, 8);
    const f32 one = 1.0f;
    u32 bits = 0;
    MemCopy(&bits, &one, 4);
    put(20, bits);
    put(24, bits);
    Vector<byte> heights;
    heights.Resize(heights_bytes);
    Vector<byte> blocks;
    blocks.Resize(block_bytes);
    for (usize i = 0; i + 3 < blocks.Size(); i += 4)
    {
        blocks[i] = static_cast<byte>(block_min);
        blocks[i + 2] = static_cast<byte>(block_max);
    }
    BlobWriter writer(types, types::kHeightMap, Rid(3), 0);
    REQUIRE(writer.AddSection(kHeightMapSectionHeader, ByteSpan(head, sizeof(head))).Ok());
    REQUIRE(writer.AddSection(kHeightMapSectionHeights, ByteSpan(heights.Data(), heights.Size())).Ok());
    REQUIRE(writer.AddSection(kHeightMapSectionBlocks, ByteSpan(blocks.Data(), blocks.Size())).Ok());
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

}  // namespace

TEST_CASE("height map: hostile blobs are refused", "[asset][height_map]")
{
    TypeRegistry types;
    HeightMapView view;
    // sanity: a well-formed 2x2 hostile builder blob opens (2 levels: 4 + 1 samples = 10 bytes, 1 block = 4 bytes)
    CHECK(view.Open(HmShare(HmHostile(2, 2, 2, 10, 4, 0, 5)), types).Ok());
    CHECK(!view.Open(HmShare(HmHostile(2, 2, 2, 10, 4, 9, 5)), types).Ok());  // a block with min above max
    CHECK(!view.Open(HmShare(HmHostile(2, 2, 2, 8, 4, 0, 5)), types).Ok());   // the chain is cut short
    CHECK(!view.Open(HmShare(HmHostile(2, 2, 2, 10, 8, 0, 5)), types).Ok());  // a pyramid with too many blocks
    CHECK(!view.Open(HmShare(HmHostile(2, 2, 3, 10, 4, 0, 5)), types).Ok());  // the wrong level count
    CHECK(!view.Open(HmShare(HmHostile(0, 2, 2, 10, 4, 0, 5)), types).Ok());  // a 0-wide map
    CHECK(!view.Open(HmShare(HmHostile(0xFFFFFFFFu, 0xFFFFFFFFu, 2, 10, 4, 0, 5)), types).Ok());  // sizes that overflow
    CHECK(!view.Open(SharedBytesPtr(), types).Ok());
}

namespace
{

const char* const kHmSource = "# a small map\n"
                              "id 00000000000000aa\n"
                              "source hills.r16\n"
                              "width 4\n"
                              "height 4\n"
                              "range_min -10\n"
                              "range_max 100.5\n"
                              "cell_size 2\n";

}  // namespace

TEST_CASE("height map source: parse and mint through the text form", "[asset][height_map][mint]")
{
    HeightMapSource source;
    HeightMapSourceError error;
    REQUIRE(ParseHeightMapSource(test::Vp(kHmSource), &source, &error).Ok());
    CHECK(source.id == Rid(0xaa));
    CHECK(source.header.width == 4);
    CHECK(source.header.range_min == -10.0f);
    CHECK(source.header.range_max == 100.5f);
    CHECK(source.header.cell_size == 2.0f);
    CHECK(source.source_line == 3);
    TypeRegistry types;
    const Vector<byte> samples = HmSamples(4, 4, HmRamp);
    Vector<byte> a;
    Vector<byte> b;
    REQUIRE(MintHeightMap(test::Vp(kHmSource), AsBytes(samples), types, &a, &error).Ok());
    REQUIRE(MintHeightMap(test::Vp(kHmSource), AsBytes(samples), types, &b, &error).Ok());
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
    HeightMapView view;
    REQUIRE(view.Open(HmShare(a), types).Ok());
    CHECK(view.Id() == Rid(0xaa));
    CHECK(view.Header().range_min == -10.0f);
    CHECK(Abs(view.SampleToHeight(65535) - 100.5f) < 1e-3f);
    CHECK(view.SampleToHeight(0) == -10.0f);
}

TEST_CASE("height map source: errors name the line", "[asset][height_map][mint]")
{
    HeightMapSource source;
    HeightMapSourceError error;
    CHECK(!ParseHeightMapSource(test::Vp("source a.r16\n"), &source, &error).Ok());
    CHECK(error.line == 1);
    CHECK(!ParseHeightMapSource(test::Vp("id 00000000000000aa\nbogus 1\n"), &source, &error).Ok());
    CHECK(error.line == 2);
    CHECK(!ParseHeightMapSource(test::Vp("id 00000000000000aa\nwidth 4\nwidth 5\n"), &source, &error).Ok());
    CHECK(error.line == 3);
    CHECK(!ParseHeightMapSource(test::Vp("id 00000000000000aa\nwidth 4\n"), &source, &error).Ok());
    CHECK(error.line == 0);  // records are missing
    CHECK(!ParseHeightMapSource(test::Vp("id 00000000000000aa\nwidth x\n"), &source, &error).Ok());
    CHECK(error.line == 2);
    // a range the wrong way round
    const char* bad = "id 00000000000000aa\nsource a\nwidth 4\nheight 4\nrange_min 5\nrange_max 1\ncell_size 1\n";
    CHECK(!ParseHeightMapSource(test::Vp(bad), &source, &error).Ok());
    // a sample file of the wrong size
    TypeRegistry types;
    Vector<byte> blob;
    const Vector<byte> few = HmSamples(2, 2, HmRamp);
    CHECK(!MintHeightMap(test::Vp(kHmSource), AsBytes(few), types, &blob, &error).Ok());
    CHECK(error.line == 3);
}

}  // namespace vse::asset
