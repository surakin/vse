#include <catch2/catch_test_macros.hpp>
#include "cstr/format.h"

#include "font_support.h"
#include "text/glyph_atlas.h"
#include "text/shelf_packer.h"

using namespace engine_placeholder;
using namespace engine_placeholder::text;
using text_test::Opened;

namespace
{

// A small deterministic generator (xorshift) so the randomized tests repeat.
struct Random
{
    u64 state = 0x2545F4914F6CDD1Dull;
    u32 Next(u32 bound)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<u32>(state % bound);
    }
};

}  // namespace

TEST_CASE("The packer never overlaps rectangles and respects padding and bounds", "[text][atlas]")
{
    constexpr u32 kSize = 2048;
    constexpr u32 kPad = 1;
    ShelfPacker packer(kSize, kSize, kPad);
    Vector<byte> grid;  // occupancy of each pixel, to detect overlap
    grid.Resize(usize{kSize} * kSize);
    Random random;
    u32 placed = 0;
    for (u32 i = 0; i < 10000; ++i)
    {
        const u32 w = 1 + random.Next(24);
        const u32 h = 1 + random.Next(24);
        Rect r;
        if (!packer.Place(w, h, &r))
        {
            continue;
        }
        ++placed;
        REQUIRE(r.w == w);
        REQUIRE(r.h == h);
        REQUIRE(r.x >= kPad);
        REQUIRE(r.y >= kPad);
        REQUIRE(r.x + r.w + kPad <= kSize);
        REQUIRE(r.y + r.h + kPad <= kSize);
        // The rectangle and its one-pixel apron must be free (this is the padding guarantee).
        for (u32 y = r.y - kPad; y < r.y + r.h + kPad; ++y)
        {
            for (u32 x = r.x - kPad; x < r.x + r.w + kPad; ++x)
            {
                const bool inner = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
                if (inner)
                {
                    REQUIRE(grid[usize{y} * kSize + x] == 0);
                }
                else
                {
                    REQUIRE(grid[usize{y} * kSize + x] !=
                            1);  // the apron may touch another apron, never another rectangle
                }
            }
        }
        for (u32 y = r.y; y < r.y + r.h; ++y)
        {
            for (u32 x = r.x; x < r.x + r.w; ++x)
            {
                grid[usize{y} * kSize + x] = 1;
            }
        }
    }
    CHECK(placed > 5000);
}

TEST_CASE("The packer fills a page to a known fraction and rejects what cannot fit", "[text][atlas]")
{
    ShelfPacker packer(64, 64, 1);
    Rect r;
    CHECK_FALSE(packer.Place(63, 8, &r));  // with its padding it is wider than the page
    CHECK_FALSE(packer.Place(8, 63, &r));
    CHECK_FALSE(packer.Place(0, 4, &r));
    u32 count = 0;
    while (packer.Place(7, 8, &r))
    {
        ++count;  // 8 columns of 7 + 1 pad fit in 63 (1 + 7*9 = 64 > 63: so 7), 7 rows of 8 + 1 pad
    }
    CHECK(count == 7 * 7);
    CHECK(packer.UsedArea() == u64{count} * 7 * 8);
    // Determinism: the same requests give the same places.
    ShelfPacker again(64, 64, 1);
    Rect a;
    Rect b;
    ShelfPacker third(64, 64, 1);
    for (u32 i = 0; i < 20; ++i)
    {
        REQUIRE(again.Place(5 + i % 3, 6 + i % 5, &a));
        REQUIRE(third.Place(5 + i % 3, 6 + i % 5, &b));
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }
}

TEST_CASE("Rendering produces coverage bitmaps", "[text][atlas]")
{
    Opened o;
    REQUIRE(o.font.SetPixelSize(32).Ok());
    GlyphBitmap h;
    REQUIRE(o.font.Render(o.font.FindGlyph(U'H'), &h).Ok());
    CHECK(h.width > 10);
    CHECK(h.height > 15);
    CHECK(h.pixels.Size() == usize{h.width} * h.height);
    CHECK(h.top > 15);
    u32 max = 0;
    for (usize i = 0; i < h.pixels.Size(); ++i)
    {
        if (h.pixels[i] > max)
        {
            max = h.pixels[i];
        }
    }
    CHECK(max == 255);
    GlyphBitmap space;
    REQUIRE(o.font.Render(o.font.FindGlyph(U' '), &space).Ok());
    CHECK(space.width == 0);
    CHECK(space.height == 0);
    CHECK(space.pixels.Size() == 0);
    CHECK(space.advance > 5);
    CHECK(o.font.Render(o.font.GlyphCount() + 1, &h).Is(Error::InvalidArgument));
    CHECK(h.pixels.Size() == 0);
}

TEST_CASE("Total coverage matches the outline area in every hinting mode", "[text][atlas]")
{
    for (Hinting hinting : {Hinting::None, Hinting::Light, Hinting::Normal, Hinting::Bytecode})
    {
        Opened o(hinting);
        REQUIRE(o.font.SetPixelSize(48).Ok());
        for (char32 c : {U'H', U'I', U'E', U'L', U'T'})
        {  // straight-edged glyphs: the outline is a polygon
            const GlyphId g = o.font.FindGlyph(c);
            Outline outline;
            REQUIRE(o.font.LoadOutline(g, &outline).Ok());
            f64 area = 0;
            u32 begin = 0;
            for (usize k = 0; k < outline.contour_ends.Size(); ++k)
            {
                const u32 end = outline.contour_ends[k];
                f64 twice = 0;
                for (u32 i = begin; i <= end; ++i)
                {
                    const OutlinePoint& a = outline.points[i];
                    const OutlinePoint& b = outline.points[i == end ? begin : i + 1];
                    twice += static_cast<f64>(a.x) * b.y - static_cast<f64>(b.x) * a.y;
                }
                area += twice / 2;  // outer contours and holes carry opposite signs
                begin = end + 1;
            }
            if (area < 0)
            {
                area = -area;
            }
            area *= (48.0 / 2048.0) * (48.0 / 2048.0);
            GlyphBitmap bitmap;
            REQUIRE(o.font.Render(g, &bitmap).Ok());
            f64 coverage = 0;
            for (usize i = 0; i < bitmap.pixels.Size(); ++i)
            {
                coverage += bitmap.pixels[i] / 255.0;
            }
            INFO("hinting " << static_cast<int>(hinting) << " glyph " << static_cast<u32>(c));
            CHECK(coverage > area * 0.85);
            CHECK(coverage < area * 1.15);
        }
    }
}

TEST_CASE("The maximum size is accepted and one above it is refused without touching the atlas", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas;
    GlyphEntry entry;
    REQUIRE(o.font.SetPixelSize(kMaxAtlasPixelSize).Ok());
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'g'), &entry).Ok());
    CHECK(entry.valid);
    const AtlasStats before = atlas.Stats();
    Vector<byte> snapshot;
    snapshot.Resize(atlas.PagePixels(0).Size());
    for (usize i = 0; i < snapshot.Size(); ++i)
    {
        snapshot[i] = atlas.PagePixels(0).Data()[i];
    }
    REQUIRE(o.font.SetPixelSize(kMaxAtlasPixelSize + 1).Ok());
    GlyphEntry refused;
    CHECK(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'h'), &refused).Is(Error::TooLarge));
    CHECK_FALSE(refused.valid);
    GlyphBitmap bitmap;
    CHECK(o.font.Render(o.font.FindGlyph(U'h'), &bitmap).Is(Error::TooLarge));
    const AtlasStats after = atlas.Stats();
    CHECK(after.too_large == before.too_large + 1);
    CHECK(after.glyphs == before.glyphs);
    CHECK(after.misses == before.misses);
    CHECK(after.pages == before.pages);
    REQUIRE(atlas.PageCount() == 1);
    bool same = true;
    for (usize i = 0; i < snapshot.Size(); ++i)
    {
        same = same && snapshot[i] == atlas.PagePixels(0).Data()[i];
    }
    CHECK(same);
}

TEST_CASE("The atlas shares entries, counts, and keys by font, size and glyph", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas;
    REQUIRE(o.font.SetPixelSize(16).Ok());
    const GlyphId a = o.font.FindGlyph(U'a');
    GlyphEntry first;
    GlyphEntry second;
    REQUIRE(atlas.GetOrAdd(o.font, a, &first).Ok());
    REQUIRE(atlas.GetOrAdd(o.font, a, &second).Ok());
    CHECK(first.page == second.page);
    CHECK(first.rect.x == second.rect.x);
    CHECK(first.rect.y == second.rect.y);
    CHECK(atlas.Stats().misses == 1);
    CHECK(atlas.Stats().hits == 1);
    CHECK(atlas.Stats().glyphs == 1);
    REQUIRE(o.font.SetPixelSize(17).Ok());  // another size is another entry
    GlyphEntry bigger;
    REQUIRE(atlas.GetOrAdd(o.font, a, &bigger).Ok());
    CHECK(atlas.Stats().glyphs == 2);
    CHECK((bigger.rect.x != first.rect.x || bigger.rect.y != first.rect.y));
    Font other;  // another font, same glyph id and size
    REQUIRE(Font::OpenFile(o.library, text_test::FontPath(), FontOptions{}, &other).Ok());
    REQUIRE(other.SetPixelSize(17).Ok());
    GlyphEntry from_other;
    REQUIRE(atlas.GetOrAdd(other, a, &from_other).Ok());
    CHECK(atlas.Stats().glyphs == 3);
    // Texture coordinates describe the rect.
    CHECK(first.u0 == static_cast<f32>(first.rect.x) / 1024.0f);
    CHECK(first.v1 == static_cast<f32>(first.rect.y + first.rect.h) / 1024.0f);
    // A space has an advance and no pixels.
    GlyphEntry space;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U' '), &space).Ok());
    CHECK(space.valid);
    CHECK_FALSE(space.has_pixels);
    CHECK(space.advance > 3);
}

TEST_CASE("A placed glyph dirties exactly its rectangle", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas;
    REQUIRE(o.font.SetPixelSize(24).Ok());
    GlyphEntry e1;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'W'), &e1).Ok());
    Rect dirty;
    REQUIRE(atlas.TakeDirty(0, &dirty));
    CHECK(dirty.x == e1.rect.x);
    CHECK(dirty.y == e1.rect.y);
    CHECK(dirty.w == e1.rect.w);
    CHECK(dirty.h == e1.rect.h);
    CHECK_FALSE(atlas.TakeDirty(0, &dirty));  // taken: clean now
    GlyphEntry again;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'W'), &again).Ok());  // a hit dirties nothing
    CHECK_FALSE(atlas.TakeDirty(0, &dirty));
    GlyphEntry e2;
    GlyphEntry e3;
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'x'), &e2).Ok());
    REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(U'y'), &e3).Ok());
    REQUIRE(atlas.TakeDirty(0, &dirty));
    const u32 x0 = e2.rect.x < e3.rect.x ? e2.rect.x : e3.rect.x;
    const u32 y0 = e2.rect.y < e3.rect.y ? e2.rect.y : e3.rect.y;
    const u32 x1 = e2.rect.x + e2.rect.w > e3.rect.x + e3.rect.w ? e2.rect.x + e2.rect.w : e3.rect.x + e3.rect.w;
    const u32 y1 = e2.rect.y + e2.rect.h > e3.rect.y + e3.rect.h ? e2.rect.y + e2.rect.h : e3.rect.y + e3.rect.h;
    CHECK(dirty.x == x0);
    CHECK(dirty.y == y0);
    CHECK(dirty.x + dirty.w == x1);
    CHECK(dirty.y + dirty.h == y1);
}

TEST_CASE("The 94 printable ASCII glyphs at 16 px pack into one 256x256 page", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas(256, 1);
    REQUIRE(o.font.SetPixelSize(16).Ok());
    for (char32 c = U'!'; c <= U'~'; ++c)
    {
        GlyphEntry entry;
        REQUIRE(atlas.GetOrAdd(o.font, o.font.FindGlyph(c), &entry).Ok());
    }
    CHECK(atlas.PageCount() == 1);
    CHECK(atlas.Stats().glyphs == 94);
}

TEST_CASE("A full page adds a page; beyond the maximum a glyph is refused and counted", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas(64, 2);  // tiny pages so a few big glyphs fill them
    REQUIRE(o.font.SetPixelSize(40).Ok());
    u32 stored = 0;
    u32 refused = 0;
    for (char32 c = U'A'; c <= U'Z'; ++c)
    {
        GlyphEntry entry;
        const Status status = atlas.GetOrAdd(o.font, o.font.FindGlyph(c), &entry);
        if (status.Ok())
        {
            ++stored;
            CHECK(entry.page < 2);
        }
        else
        {
            ++refused;
            CHECK(status.Is(Error::AtlasFull));
            CHECK_FALSE(entry.valid);
        }
    }
    CHECK(atlas.PageCount() == 2);
    CHECK(stored > 2);
    CHECK(refused > 0);
    CHECK(atlas.Stats().failures == refused);
    // A glyph larger than a page is refused even on a fresh atlas.
    GlyphAtlas small(16, 4);
    REQUIRE(o.font.SetPixelSize(60).Ok());
    GlyphEntry entry;
    CHECK(small.GetOrAdd(o.font, o.font.FindGlyph(U'W'), &entry).Is(Error::AtlasFull));
    CHECK(small.Stats().failures == 1);
}

namespace
{

// "Hello" rasterized glyph by glyph with the pen advancing by the rounded advance, into one 8-bit image, as a binary
// PGM (P5).
Vector<byte> HelloPgm(Font& font, u32 size, u32* width_out, u32* height_out)
{
    font.SetPixelSize(size);
    const FontMetrics metrics = font.Metrics();
    const u32 ascent = static_cast<u32>(metrics.ascent + 1.0f);
    const u32 height = ascent + static_cast<u32>(metrics.descent + 1.0f);
    const u32 width = size * 4;
    Vector<byte> image;
    image.Resize(usize{width} * height);
    i32 pen = 2;
    const char32 text[] = {U'H', U'e', U'l', U'l', U'o'};
    for (char32 c : text)
    {
        GlyphBitmap bitmap;
        REQUIRE(font.Render(font.FindGlyph(c), &bitmap).Ok());
        for (u32 row = 0; row < bitmap.height; ++row)
        {
            for (u32 col = 0; col < bitmap.width; ++col)
            {
                const i32 x = pen + bitmap.left + static_cast<i32>(col);
                const i32 y = static_cast<i32>(ascent) - bitmap.top + static_cast<i32>(row);
                if (x < 0 || y < 0 || x >= static_cast<i32>(width) || y >= static_cast<i32>(height))
                {
                    continue;
                }
                image[usize{static_cast<u32>(y)} * width +
                      static_cast<u32>(x)] = bitmap.pixels[usize{row} * bitmap.width + col];
            }
        }
        pen += static_cast<i32>(bitmap.advance + 0.5f);
    }
    Vector<byte> pgm;
    const char header[] = "P5\n%u %u\n255\n";
    char line[64];
    const int n = FormatTo(line, sizeof(line), header, width, height);
    for (int i = 0; i < n; ++i)
    {
        pgm.Push(static_cast<byte>(line[i]));
    }
    for (usize i = 0; i < image.Size(); ++i)
    {
        pgm.Push(image[i]);
    }
    *width_out = width;
    *height_out = height;
    return pgm;
}

}  // namespace

TEST_CASE("Rasterized Hello matches the committed goldens", "[text][atlas][golden]")
{
    Opened o;
    struct Case
    {
        u32 size;
        const char* path;
    };
    for (const Case& c :
         {Case{16, TEXT_TEST_GOLDEN_DIR "/hello_16.pgm"}, Case{32, TEXT_TEST_GOLDEN_DIR "/hello_32.pgm"}})
    {
        u32 w = 0;
        u32 h = 0;
        const Vector<byte> pgm = HelloPgm(o.font, c.size, &w, &h);
        const io::Path path{StringView(c.path)};
        Vector<byte> golden;
        const io::Status read = io::ReadFile(path, &golden);
        INFO(c.path);
        if (read.Is(io::Error::NotFound))
        {
            // A missing golden is written for review and the test fails until it is committed.
            REQUIRE(io::WriteFile(path, ByteSpan(pgm.Data(), pgm.Size())).Ok());
            FAIL("golden written; check it by eye and commit it");
        }
        REQUIRE(read.Ok());
        REQUIRE(golden.Size() == pgm.Size());
        bool same = true;
        for (usize i = 0; i < pgm.Size(); ++i)
        {
            same = same && golden[i] == pgm[i];
        }
        CHECK(same);
    }
}

TEST_CASE("Dirty rectangles are tracked per page", "[text][atlas]")
{
    Opened o;
    GlyphAtlas atlas(64, 3);
    REQUIRE(o.font.SetPixelSize(40).Ok());
    GlyphEntry entries[8];
    u32 added = 0;
    for (char32 c = U'A'; c <= U'H'; ++c)
    {
        if (atlas.GetOrAdd(o.font, o.font.FindGlyph(c), &entries[added]).Ok())
        {
            ++added;
        }
    }
    REQUIRE(atlas.PageCount() >= 2);
    for (u32 page = 0; page < atlas.PageCount(); ++page)
    {
        Rect dirty;
        REQUIRE(atlas.TakeDirty(page, &dirty));
        for (u32 i = 0; i < added; ++i)
        {
            if (entries[i].page != page)
            {
                continue;
            }
            CHECK(entries[i].rect.x >= dirty.x);
            CHECK(entries[i].rect.y >= dirty.y);
            CHECK(entries[i].rect.x + entries[i].rect.w <= dirty.x + dirty.w);
            CHECK(entries[i].rect.y + entries[i].rect.h <= dirty.y + dirty.h);
        }
        CHECK_FALSE(atlas.TakeDirty(page, &dirty));
    }
}

TEST_CASE("The packer opens a last shelf of the exact height when the class does not fit", "[text][atlas]")
{
    ShelfPacker packer(40, 20, 1);
    Rect r;
    REQUIRE(packer.Place(10, 9, &r));  // class 12: shelf rows 1..13, next shelf at 14
    REQUIRE(packer.Place(10, 5, &r));  // reuses the 12-high shelf
    CHECK(r.y == 1);
    REQUIRE(packer.Place(30, 5, &r));  // needs a new shelf: class 8 would end at 14+8+1=23 > 20, exact 5 ends at 20
    CHECK(r.y == 14);
    CHECK_FALSE(packer.Place(30, 6, &r));
}
