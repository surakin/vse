#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

#include "font_support.h"
#include "text/font.h"

using namespace engine_placeholder;
using namespace engine_placeholder::text;

namespace {

struct Opened {
    FontLibrary library;
    Font font;
    Opened() {
        REQUIRE(library.Initialize().Ok());
        const Status status = Font::OpenFile(library, text_test::FontPath(), FontOptions{}, &font);
        REQUIRE(status.Ok());
    }
};

} // namespace

TEST_CASE("The shipped font opens from a path and from bytes", "[text][font]") {
    FontLibrary library;
    REQUIRE(library.Initialize().Ok());
    Font from_path;
    REQUIRE(Font::OpenFile(library, text_test::FontPath(), FontOptions{}, &from_path).Ok());
    CHECK(from_path.IsOpen());
    CHECK(from_path.PixelSize() == 16);
    CHECK(from_path.GlyphCount() > 100);
    Vector<byte> bytes = text_test::FontBytes();
    REQUIRE(bytes.Size() > 1000);
    Font from_bytes;
    REQUIRE(Font::Open(library, ByteSpan(bytes.Data(), bytes.Size()), FontOptions{}, &from_bytes).Ok());
    CHECK(from_bytes.Id() != from_path.Id());
    CHECK(from_bytes.Id() != 0);
}

TEST_CASE("Metrics grow with the pixel size", "[text][font]") {
    Opened o;
    f32 last_ascent = 0;
    f32 last_height = 0;
    for (u32 size : {12u, 16u, 24u, 48u}) {
        REQUIRE(o.font.SetPixelSize(size).Ok());
        const FontMetrics m = o.font.Metrics();
        CHECK(m.ascent > last_ascent);
        CHECK(m.line_height > last_height);
        CHECK(m.descent > 0);
        CHECK(m.units_per_em == 2048);
        last_ascent = m.ascent;
        last_height = m.line_height;
    }
}

TEST_CASE("Pixel sizes of 0 and above the sanity limit are refused; large ones are accepted", "[text][font]") {
    Opened o;
    CHECK(o.font.SetPixelSize(0).Is(Error::InvalidArgument));
    CHECK(o.font.SetPixelSize(kMaxPixelSize + 1).Is(Error::InvalidArgument));
    CHECK(o.font.PixelSize() == 16);
    CHECK(o.font.SetPixelSize(kMaxAtlasPixelSize + 1).Ok());
    CHECK(o.font.SetPixelSize(kMaxPixelSize).Ok());
    CHECK(o.font.Metrics().line_height > 1000);
    Font closed;
    CHECK(closed.SetPixelSize(16).Is(Error::InvalidArgument));
}

TEST_CASE("Glyph lookup: real glyphs, .notdef, and invalid code points", "[text][font]") {
    Opened o;
    for (char32 c : {U'H', U'e', U'l', U'o', U' '}) {
        CHECK(o.font.FindGlyph(c) != 0);
        CHECK(o.font.HasGlyph(c));
    }
    CHECK(o.font.FindGlyph(U'H') != o.font.FindGlyph(U'e'));
    CHECK(o.font.FindGlyph(0x4E2D) == 0);      // CJK: not in the font
    CHECK_FALSE(o.font.HasGlyph(0x4E2D));
    CHECK(o.font.FindGlyph(0xD800) == 0);      // surrogate
    CHECK(o.font.FindGlyph(0x110000) == 0);    // above the Unicode range
    CHECK(o.font.FindGlyph(0xFFFFFFFFu) == 0);
    CHECK(o.font.FindGlyph(0x0416) != 0);      // Cyrillic Zhe
    CHECK(o.font.FindGlyph(0x03A9) != 0);      // Greek Omega
}

TEST_CASE("Glyph metrics: advances, boxes, and the space", "[text][font]") {
    Opened o;
    REQUIRE(o.font.SetPixelSize(32).Ok());
    const GlyphMetrics h = o.font.Metrics(o.font.FindGlyph(U'H'));
    CHECK(h.advance > 10);
    CHECK(h.width > 10);
    CHECK(h.height > 15);
    CHECK(h.bearing_y > 15);
    const GlyphMetrics space = o.font.Metrics(o.font.FindGlyph(U' '));
    CHECK(space.advance > 5);
    CHECK(space.width == 0);
    CHECK(space.height == 0);
    const GlyphMetrics w = o.font.Metrics(o.font.FindGlyph(U'W'));
    CHECK(w.advance > h.advance);
    CHECK(o.font.Metrics(o.font.GlyphCount() + 5).advance == 0); // out of range: zeroes
    // Doubling the size roughly doubles an advance.
    const f32 a32 = h.advance;
    REQUIRE(o.font.SetPixelSize(64).Ok());
    CHECK(o.font.Metrics(o.font.FindGlyph(U'H')).advance > a32 * 1.8f);
}

TEST_CASE("Kerning pulls AV together and leaves ab alone", "[text][font]") {
    Opened o;
    REQUIRE(o.font.SetPixelSize(48).Ok());
    const f32 av = o.font.Kerning(o.font.FindGlyph(U'A'), o.font.FindGlyph(U'V'));
    const f32 ab = o.font.Kerning(o.font.FindGlyph(U'a'), o.font.FindGlyph(U'b'));
    CHECK(av < 0);
    CHECK(ab == 0);
}

TEST_CASE("Outlines: contour counts and the box against the metrics", "[text][font]") {
    Opened o;
    Outline outline;
    REQUIRE(o.font.LoadOutline(o.font.FindGlyph(U'o'), &outline).Ok());
    CHECK(outline.contour_ends.Size() == 2);
    CHECK(outline.points.Size() > 8);
    CHECK(outline.contour_ends[outline.contour_ends.Size() - 1] == outline.points.Size() - 1);
    REQUIRE(o.font.LoadOutline(o.font.FindGlyph(U'l'), &outline).Ok());
    CHECK(outline.contour_ends.Size() == 1);
    // The 'l' box in font units, scaled to the size, is the metrics box (within a pixel).
    REQUIRE(o.font.SetPixelSize(64).Ok());
    const GlyphMetrics m = o.font.Metrics(o.font.FindGlyph(U'l'));
    const f32 scale = 64.0f / 2048.0f;
    const f32 box_height = static_cast<f32>(outline.y_max - outline.y_min) * scale;
    CHECK(box_height > static_cast<f32>(m.height) - 2.0f);
    CHECK(box_height < static_cast<f32>(m.height) + 2.0f);
    CHECK(o.font.LoadOutline(o.font.FindGlyph(U' '), &outline).Ok());
    CHECK(outline.points.Size() == 0);
    CHECK(o.font.LoadOutline(o.font.GlyphCount(), &outline).Is(Error::InvalidArgument));
    CHECK(outline.points.Size() == 0);
}

TEST_CASE("Bad input is refused with distinct statuses", "[text][font]") {
    FontLibrary library;
    REQUIRE(library.Initialize().Ok());
    Vector<byte> good = text_test::FontBytes();
    Font font;
    CHECK(Font::Open(library, ByteSpan(good.Data(), 0), FontOptions{}, &font).Is(Error::Empty));
    const char text[] = "This is a plain text file and not a font at all, just words.";
    const Status not_font = Font::Open(library, ByteSpan(reinterpret_cast<const byte*>(text), sizeof(text)), FontOptions{}, &font);
    INFO(static_cast<int>(not_font.error) << " " << not_font.message);
    CHECK(not_font.Is(Error::NotAFont));
    Status truncated = Font::Open(library, ByteSpan(good.Data(), good.Size() / 3), FontOptions{}, &font);
    CHECK_FALSE(truncated.Ok());
    CHECK(truncated.Is(Error::Corrupt));
    FontOptions bad_face;
    bad_face.face_index = 9;
    CHECK(Font::Open(library, ByteSpan(good.Data(), good.Size()), bad_face, &font).Is(Error::Unsupported));
    CHECK_FALSE(font.IsOpen());
    const Status missing = Font::OpenFile(library, io::Path(StringView("/no/such/dir/font.ttf")), FontOptions{}, &font);
    CHECK(missing.Is(Error::NotFound));
    CHECK(missing.io_error == io::Error::NotFound);
    FontLibrary cold;
    CHECK(Font::Open(cold, ByteSpan(good.Data(), good.Size()), FontOptions{}, &font).Is(Error::InvalidArgument));
}

TEST_CASE("A font moves and closes; the library sees every block returned", "[text][font]") {
    FontLibrary library;
    REQUIRE(library.Initialize().Ok());
    {
        Font a;
        REQUIRE(Font::OpenFile(library, text_test::FontPath(), FontOptions{}, &a).Ok());
        const u64 id = a.Id();
        Font b(static_cast<Font&&>(a));
        CHECK_FALSE(a.IsOpen());
        CHECK(b.Id() == id);
        Font c;
        c = static_cast<Font&&>(b);
        CHECK(c.IsOpen());
        CHECK(c.FindGlyph(U'x') != 0);
        c.Close();
        CHECK_FALSE(c.IsOpen());
        c.Close(); // harmless
    }
    // Only the library's own module tables are live now; the fonts' blocks are back.
    const LibraryAllocationStats before = library.Stats();
    {
        Font a;
        REQUIRE(Font::OpenFile(library, text_test::FontPath(), FontOptions{}, &a).Ok());
        CHECK(library.Stats().live_bytes_at_end > before.live_bytes_at_end);
    }
    CHECK(library.Stats().live_bytes_at_end == before.live_bytes_at_end);
}

TEST_CASE("A failed allocation at any point leaves nothing behind", "[text][font]") {
    Vector<byte> good = text_test::FontBytes();
    u32 failures = 0;
    for (u64 n = 0; n < 400; ++n) {
        FontLibrary library;
        REQUIRE(library.Initialize().Ok());
        const u64 live_before = library.Stats().live_bytes_at_end;
        library.FailAllocationsAfter(n);
        {
            Font font;
            const Status status = Font::Open(library, ByteSpan(good.Data(), good.Size()), FontOptions{}, &font);
            if (status.Ok()) {
                // Later allocations fail too: the unhinted outline path must stay safe. (Hinted glyph loading is left out: FreeType 2.15.0's auto-hinter
                // dereferences a null hash table when an allocation fails during the first glyph load; KNOWN-ISSUES.md.)
                Outline outline;
                (void)font.LoadOutline(font.FindGlyph(U'g'), &outline);
            } else {
                ++failures;
                CHECK((status.Is(Error::OutOfMemory) || status.Is(Error::Corrupt)));
                CHECK_FALSE(font.IsOpen());
            }
        }
        library.FailAllocationsAfter(~u64{0});
        CHECK(library.Stats().live_bytes_at_end == live_before);
    }
    CHECK(failures > 5); // the injection really bit
}

TEST_CASE("Separate libraries and fonts work in parallel from several threads", "[text][font]") {
    Vector<byte> good = text_test::FontBytes();
    std::atomic<u32> ok{0};
    Vector<std::thread> threads;
    for (u32 t = 0; t < 4; ++t) {
        threads.Push(std::thread([&good, &ok, t] {
            FontLibrary library;
            if (!library.Initialize().Ok()) return;
            Font font;
            if (!Font::Open(library, ByteSpan(good.Data(), good.Size()), FontOptions{}, &font).Ok()) return;
            for (u32 i = 0; i < 50; ++i) {
                if (!font.SetPixelSize(10 + t + i).Ok()) return;
                (void)font.Metrics(font.FindGlyph(U'a' + (i % 26)));
            }
            ok.fetch_add(1);
        }));
    }
    for (usize i = 0; i < threads.Size(); ++i) threads[i].join();
    CHECK(ok.load() == 4);
}

TEST_CASE("A glyph's pixel box contains its whole outline, fractional bearing included", "[text][font]") {
    Opened o;
    for (u32 size : {13u, 17u, 33u}) {
        REQUIRE(o.font.SetPixelSize(size).Ok());
        for (char32 c = U'!'; c <= U'~'; ++c) {
            const GlyphId g = o.font.FindGlyph(c);
            Outline outline;
            REQUIRE(o.font.LoadOutline(g, &outline).Ok());
            const GlyphMetrics m = o.font.Metrics(g);
            const f32 scale = static_cast<f32>(size) / 2048.0f;
            const f32 outline_width = static_cast<f32>(outline.x_max - outline.x_min) * scale;
            INFO("size " << size << " glyph " << static_cast<u32>(c));
            CHECK(static_cast<f32>(m.width) >= outline_width - 0.6f);
            CHECK(static_cast<f32>(m.height) >= static_cast<f32>(outline.y_max - outline.y_min) * scale - 1.0f); // light hinting fits heights to the grid
        }
    }
}

TEST_CASE("OpenFile and Open of the same bytes give the same font", "[text][font]") {
    FontLibrary library;
    REQUIRE(library.Initialize().Ok());
    Font a;
    Font b;
    REQUIRE(Font::OpenFile(library, text_test::FontPath(), FontOptions{}, &a).Ok());
    Vector<byte> bytes = text_test::FontBytes();
    REQUIRE(Font::Open(library, ByteSpan(bytes.Data(), bytes.Size()), FontOptions{}, &b).Ok());
    CHECK(a.GlyphCount() == b.GlyphCount());
    CHECK(a.Metrics(a.FindGlyph(U'g')).advance == b.Metrics(b.FindGlyph(U'g')).advance);
}
