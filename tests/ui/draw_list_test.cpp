#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"

#include "font_support.h"
#include "text/glyph_atlas.h"
#include "text/layout.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

constexpr u32 kWhite = 0xFFFFFFFFu;

struct Fixture
{
    DrawList list;
    DrawListCanvas canvas;
    explicit Fixture(f32 scale = 1.0f, text::GlyphAtlas* atlas = nullptr) : canvas(&list, atlas)
    {
        canvas.Begin(scale, 800, 600);
    }
};

u64 Fnv(u64 h, const void* data, usize size)
{
    const byte* p = static_cast<const byte*>(data);
    for (usize i = 0; i < size; ++i)
    {
        // Every byte of a Vertex array was written by the canvas
        // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

u64 HashList(const DrawList& l)
{
    u64 h = 1469598103934665603ull;
    h = Fnv(h, l.vertices.Data(), l.vertices.Size() * sizeof(Vertex));
    h = Fnv(h, l.indices.Data(), l.indices.Size() * sizeof(u32));
    for (usize i = 0; i < l.commands.Size(); ++i)
    {
        const DrawCommand& c = l.commands[i];
        const i32 fields[] = {
            static_cast<i32>(c.first_index),  static_cast<i32>(c.index_count), c.clip.x, c.clip.y, c.clip.w, c.clip.h,
            static_cast<i32>(c.texture.kind), static_cast<i32>(c.texture.id),  c.layer};
        h = Fnv(h, fields, sizeof(fields));
    }
    return h;
}

// A fixed scene without text (text depends on the rasteriser).
void FixedScene(Canvas& c)
{
    c.DrawRect({10, 10, 100, 50}, 0xFF0000FFu);
    c.DrawRoundedRect({20, 20, 80, 30}, CornerRadii(6, 6, 0, 0), 0xFF00FF00u);
    c.DrawBorder({20, 20, 80, 30}, CornerRadii(6), 1, kWhite);
    c.PushClip({0, 0, 60, 60});
    c.DrawLine({0, 0}, {50, 40}, 2, 0xFF123456u);
    c.DrawTriangle({5, 5}, {25, 5}, {15, 25}, 0xFF654321u);
    c.PopClip();
    c.DrawImage({30, 30, 16, 16}, 7, {0, 0, 1, 1}, kWhite);
    c.DrawShadow({40, 40, 60, 30}, 4, 8, 0x80000000u);
    c.BeginLayer(3);
    c.DrawRect({0, 0, 20, 20}, 0xFFABCDEFu);
    c.EndLayer();
}

}  // namespace

TEST_CASE("DrawList: the vertex is 40 bytes", "[ui][drawlist]")
{
    CHECK(sizeof(Vertex) == 40);
}

TEST_CASE("DrawList: a rectangle is one solid quad of whole pixels", "[ui][drawlist]")
{
    Fixture f(2.0f);
    f.canvas.DrawRect({1.3f, 2.2f, 10, 5}, 0xFF0000FFu);
    REQUIRE(f.list.vertices.Size() == 4);
    REQUIRE(f.list.indices.Size() == 6);
    // Edges are round(x * 2): 2.6 -> 3, 4.4 -> 4, (11.3 * 2 = 22.6) -> 23, (7.2 * 2 = 14.4) -> 14.
    CHECK(f.list.vertices[0].x == 3);
    CHECK(f.list.vertices[0].y == 4);
    CHECK(f.list.vertices[3].x == 23);
    CHECK(f.list.vertices[3].y == 14);
    CHECK((f.list.vertices[0].flags & kModeMask) == kModeSolid);
    CHECK(f.list.vertices[0].color == 0xFF0000FFu);
    for (usize i = 0; i < 6; ++i)
    {
        CHECK(f.list.indices[i] < 4);
    }
    REQUIRE(f.list.commands.Size() == 1);
    CHECK(f.list.commands[0].index_count == 6);
}

TEST_CASE("DrawList: rounded rects and borders are rectangle-mode quads with per-corner radii", "[ui][drawlist]")
{
    Fixture f(2.0f);
    f.canvas.DrawRoundedRect({0, 0, 40, 20}, CornerRadii(4, 6, 8, 2), kWhite);
    REQUIRE(f.list.vertices.Size() == 4);
    const Vertex* v = f.list.vertices.Data();
    CHECK((v[0].flags & kModeMask) == kModeRect);
    CHECK(v[0].half_w == 40);
    CHECK(v[0].half_h == 20);
    CHECK(v[0].radius == 8);   // top-left, scaled by 2
    CHECK(v[1].radius == 12);  // top-right
    CHECK(v[2].radius == 4);   // bottom-left
    CHECK(v[3].radius == 16);  // bottom-right
    CHECK(v[0].u == -40);      // the offset from the centre, in pixels
    CHECK(v[3].v == 20);
    // A radius is cut to half of the shorter side.
    Fixture g;
    g.canvas.DrawRoundedRect({0, 0, 40, 20}, CornerRadii(500), kWhite);
    CHECK(g.list.vertices[0].radius == 10);
    // All-zero radii are a plain rectangle.
    Fixture h;
    h.canvas.DrawRoundedRect({0, 0, 40, 20}, CornerRadii(), kWhite);
    CHECK((h.list.vertices[0].flags & kModeMask) == kModeSolid);
}

TEST_CASE("DrawList: a 1-unit border is 1, 1 and 2 pixels at scales 1, 1.25 and 2", "[ui][drawlist]")
{
    const f32 scales[3] = {1.0f, 1.25f, 2.0f};
    const f32 expected[3] = {1.0f, 1.0f, 2.0f};
    for (int i = 0; i < 3; ++i)
    {
        Fixture f(scales[i]);
        f.canvas.DrawBorder({0, 0, 40, 20}, CornerRadii(4), 1, kWhite);
        REQUIRE(f.list.vertices.Size() == 4);
        CHECK(f.list.vertices[0].border == expected[i]);
        CHECK((f.list.vertices[0].flags & kFlagRing) != 0);
    }
    Fixture f;
    f.canvas.DrawBorder({0, 0, 40, 20}, CornerRadii(4), 0, kWhite);
    CHECK(f.list.vertices.Empty());
}

TEST_CASE("DrawList: lines, triangles, images and shadows have the vertices the shader expects", "[ui][drawlist]")
{
    Fixture f;
    f.canvas.DrawLine({0, 0}, {10, 0}, 2, kWhite);
    REQUIRE(f.list.vertices.Size() == 4);
    CHECK(f.list.vertices[0].half_w == 5);
    CHECK(f.list.vertices[0].half_h == 1);
    CHECK(f.list.vertices[0].y == 1.0f);  // the box is centred on the line (the sign follows the normal)
    CHECK(f.list.vertices[2].y == -1.0f);
    f.canvas.DrawTriangle({0, 0}, {10, 0}, {5, 8}, kWhite);
    REQUIRE(f.list.vertices.Size() == 7);
    REQUIRE(f.list.indices.Size() == 9);
    CHECK(f.list.indices[8] == 6);
    f.canvas.DrawImage({0, 0, 8, 8}, 9, {0.25f, 0.5f, 0.5f, 0.25f}, 0x80FFFFFFu);
    REQUIRE(f.list.vertices.Size() == 11);
    CHECK((f.list.vertices[7].flags & kModeMask) == kModeImage);
    CHECK(f.list.vertices[7].u == 0.25f);
    CHECK(f.list.vertices[10].u == 0.75f);
    CHECK(f.list.vertices[10].v == 0.75f);
    f.canvas.DrawImage({0, 0, 8, 8}, 0, {0, 0, 1, 1}, kWhite);  // image 0 draws nothing
    CHECK(f.list.vertices.Size() == 11);
    f.canvas.DrawShadow({10, 10, 20, 20}, 4, 6, 0x80000000u);
    REQUIRE(f.list.vertices.Size() == 15);
    CHECK(f.list.vertices[11].x == 4);  // grown by the blur
    CHECK((f.list.vertices[11].flags & kFlagShadow) != 0);
    CHECK(f.list.vertices[11].half_w == 10);  // the box the shader measures stays the rectangle
    CHECK(f.list.vertices[11].border == 6);
}

TEST_CASE("DrawList: clips intersect, and a draw outside the clip makes no vertex", "[ui][drawlist][clip]")
{
    Fixture f;
    f.canvas.PushClip({0, 0, 50, 50});
    f.canvas.PushClip({25, 25, 100, 100});
    CHECK(f.canvas.CurrentClip() == PixelRect{25, 25, 25, 25});
    f.canvas.DrawRect({100, 100, 10, 10}, kWhite);  // outside
    CHECK(f.list.vertices.Empty());
    CHECK(f.list.stats.culled == 1);
    f.canvas.DrawRect({30, 30, 10, 10}, kWhite);
    REQUIRE(f.list.commands.Size() == 1);
    CHECK(f.list.commands[0].clip == PixelRect{25, 25, 25, 25});
    f.canvas.PopClip();
    CHECK(f.canvas.CurrentClip() == PixelRect{0, 0, 50, 50});
    f.canvas.PopClip();
    CHECK(f.canvas.CurrentClip() == PixelRect{0, 0, 800, 600});
    // Disjoint clips leave an empty clip: everything is culled.
    f.canvas.PushClip({0, 0, 10, 10});
    f.canvas.PushClip({20, 20, 10, 10});
    CHECK(f.canvas.CurrentClip().Empty());
    const usize before = f.list.vertices.Size();
    f.canvas.DrawRect({0, 0, 800, 600}, kWhite);
    CHECK(f.list.vertices.Size() == before);
    // A fully transparent colour draws nothing either.
    f.canvas.PopClip();
    f.canvas.PopClip();
    f.canvas.DrawRect({0, 0, 10, 10}, 0x00FFFFFFu);
    CHECK(f.list.vertices.Size() == before);
}

TEST_CASE("DrawList: draws with equal state share a command", "[ui][drawlist][merge]")
{
    Fixture f;
    f.canvas.DrawRect({0, 0, 10, 10}, kWhite);
    f.canvas.DrawRoundedRect({20, 0, 10, 10}, CornerRadii(2), kWhite);
    f.canvas.DrawRect({40, 0, 10, 10}, kWhite);
    CHECK(f.list.commands.Size() == 1);
    CHECK(f.list.commands[0].index_count == 18);
    f.canvas.PushClip({0, 0, 100, 100});
    f.canvas.DrawRect({0, 20, 10, 10}, kWhite);
    CHECK(f.list.commands.Size() == 2);  // a new clip, a new command
    f.canvas.PopClip();
    f.canvas.DrawImage({0, 0, 8, 8}, 1, {0, 0, 1, 1}, kWhite);
    f.canvas.DrawRect({50, 50, 8, 8}, kWhite);  // needs no texture: joins the image command
    f.canvas.DrawImage({0, 0, 8, 8}, 1, {0, 0, 1, 1}, kWhite);
    CHECK(f.list.commands.Size() == 3);
    f.canvas.DrawImage({0, 0, 8, 8}, 2, {0, 0, 1, 1}, kWhite);  // another image: a new command
    CHECK(f.list.commands.Size() == 4);
    CHECK(f.list.commands[2].texture.kind == TextureKind::Image);
    CHECK(f.list.commands[2].texture.id == 1);
    CHECK(f.list.commands[2].index_count == 18);
}

TEST_CASE("DrawList: layers sort by order, keeping call order inside an order", "[ui][drawlist][layers]")
{
    Fixture f;
    f.canvas.DrawRect({0, 0, 10, 10}, 0xFF000001u);  // a: base layer
    f.canvas.BeginLayer(5);
    f.canvas.DrawRect({0, 0, 10, 10}, 0xFF000002u);  // b: popup
    f.canvas.EndLayer();
    f.canvas.DrawRect({20, 0, 10, 10}, 0xFF000003u);  // c: base layer, after the popup
    f.canvas.BeginLayer(2);
    f.canvas.DrawRect({0, 0, 10, 10}, 0xFF000004u);  // d
    f.canvas.EndLayer();
    f.canvas.BeginLayer(5);
    f.canvas.DrawRect({0, 0, 10, 10}, 0xFF000005u);  // e: same order as b, later
    f.canvas.EndLayer();
    f.list.SortLayers();
    REQUIRE(f.list.commands.Size() == 5);
    const u32 expected[5] = {1, 3, 4, 2, 5};
    for (usize i = 0; i < 5; ++i)
    {
        const DrawCommand& c = f.list.commands[i];
        CHECK(f.list.vertices[f.list.indices[c.first_index]].color == (0xFF000000u | expected[i]));
    }
}

TEST_CASE("DrawList: a layer is not cut by the clip of the code that opened it", "[ui][drawlist][layers]")
{
    Fixture f;
    f.canvas.PushClip({0, 0, 10, 10});
    f.canvas.BeginLayer(1);
    CHECK(f.canvas.CurrentClip() == PixelRect{0, 0, 800, 600});
    f.canvas.PushClip({5, 5, 20, 20});
    f.canvas.PopClip();
    f.canvas.EndLayer();
    CHECK(f.canvas.CurrentClip() == PixelRect{0, 0, 10, 10});
}

TEST_CASE("DrawList: past the vertex limit a draw is dropped and counted, never wrapped", "[ui][drawlist][limits]")
{
    Fixture f;
    f.list.max_vertices = 400;
    for (int i = 0; i < 150; ++i)
    {
        f.canvas.DrawRect({static_cast<f32>(i), 0, 1, 1}, kWhite);
    }
    CHECK(f.list.vertices.Size() == 400);
    CHECK(f.list.stats.overflowed == 50);
    for (usize i = 0; i < f.list.indices.Size(); ++i)
    {
        REQUIRE(f.list.indices[i] < f.list.vertices.Size());
    }
}

TEST_CASE("DrawList: 100,000 rectangles keep every index in range", "[ui][drawlist][limits]")
{
    Fixture f;
    for (int i = 0; i < 100000; ++i)
    {
        f.canvas.DrawRect({static_cast<f32>(i % 700), static_cast<f32>(i % 500), 2, 2}, kWhite);
    }
    CHECK(f.list.vertices.Size() == 400000);
    CHECK(f.list.indices.Size() == 600000);
    CHECK(f.list.stats.overflowed == 0);
    u32 largest = 0;
    for (usize i = 0; i < f.list.indices.Size(); ++i)
    {
        largest = f.list.indices[i] > largest ? f.list.indices[i] : largest;
    }
    CHECK(largest == 399999);
}

TEST_CASE("DrawList: the same scene gives the same bytes", "[ui][drawlist][determinism]")
{
    Fixture a(2.0f), b(2.0f);
    FixedScene(a.canvas);
    FixedScene(b.canvas);
    a.list.SortLayers();
    b.list.SortLayers();
    CHECK(HashList(a.list) == HashList(b.list));
    CHECK(a.list.vertices.Size() == 31);
    // The golden: a hash of the vertex, index and command bytes of the scene at scale 2 (recorded 2026-11-10). A change
    // to the vertex layout or to the snapping rules moves it on purpose; update it in the same commit, after reading
    // the diff of the data.
    CHECK(HashList(a.list) == 0xce74463a14efbb7bull);
}

TEST_CASE("DrawList: a steady frame allocates nothing once the arrays have grown", "[ui][drawlist][allocation]")
{
    Fixture f(1.25f);
    for (int frame = 0; frame < 3; ++frame)
    {
        f.list.Clear();
        f.canvas.Begin(1.25f, 800, 600);
        FixedScene(f.canvas);
        f.list.SortLayers();
    }
    const usize vc = f.list.vertices.Capacity(), ic = f.list.indices.Capacity(), cc = f.list.commands.Capacity();
    const Vertex* vdata = f.list.vertices.Data();
    for (int frame = 0; frame < 100; ++frame)
    {
        f.list.Clear();
        f.canvas.Begin(1.25f, 800, 600);
        FixedScene(f.canvas);
        f.list.SortLayers();
    }
    CHECK(f.list.vertices.Capacity() == vc);
    CHECK(f.list.indices.Capacity() == ic);
    CHECK(f.list.commands.Capacity() == cc);
    CHECK(f.list.vertices.Data() == vdata);  // never reallocated
}

TEST_CASE("DrawList: text quads equal the text module's placement and name the atlas page", "[ui][drawlist][text]")
{
    ui_test::Opened o;
    text::GlyphAtlas atlas;
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = 24;  // logical 12 at scale 2
    text::TextLayout layout;
    REQUIRE(text::Layout(StringView("Hello, ui"), style, text::LayoutOptions{}, &layout).Ok());
    Fixture f(2.0f, &atlas);
    f.canvas.DrawText(layout, style, {5.25f, 7.5f}, 0xFF00FF00u);
    REQUIRE(!f.list.vertices.Empty());
    REQUIRE(f.list.vertices.Size() % 4 == 0);
    // Replay the placement with the text module directly.
    REQUIRE(o.font.SetPixelSize(24).Ok());
    usize quad = 0;
    for (usize i = 0; i < layout.glyphs.Size(); ++i)
    {
        text::GlyphEntry entry;
        REQUIRE(atlas.GetOrAdd(o.font, layout.glyphs[i].glyph, &entry).Ok());
        if (!entry.has_pixels)
        {
            continue;
        }
        const text::GlyphQuad q = text::PlaceGlyph(10.5f, 15.0f, layout.glyphs[i].x, layout.glyphs[i].y, entry, 1.0f);
        const Vertex* v = &f.list.vertices[quad * 4];
        CHECK(v[0].x == q.left);
        CHECK(v[0].y == q.top);
        CHECK(v[3].x == q.right);
        CHECK(v[3].y == q.bottom);
        CHECK(v[0].u == entry.u0);
        CHECK(v[3].v == entry.v1);
        CHECK((v[0].flags & kModeMask) == kModeGlyph);
        CHECK(v[0].color == 0xFF00FF00u);
        ++quad;
    }
    CHECK(quad * 4 == f.list.vertices.Size());
    REQUIRE(f.list.commands.Size() == 1);  // one atlas page, one clip
    CHECK(f.list.commands[0].texture.kind == TextureKind::Atlas);
    CHECK(f.list.commands[0].texture.id == 0);
    CHECK(f.list.stats.glyphs_dropped == 0);
}

TEST_CASE("DrawList: glyphs the atlas cannot hold, or drawn without an atlas, are counted as dropped",
          "[ui][drawlist][text]")
{
    ui_test::Opened o;
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = 32;
    text::TextLayout layout;
    REQUIRE(text::Layout(StringView("Wide"), style, text::LayoutOptions{}, &layout).Ok());
    {
        Fixture f(1.0f, nullptr);
        f.canvas.DrawText(layout, style, {0, 0}, kWhite);
        CHECK(f.list.vertices.Empty());
        CHECK(f.list.stats.glyphs_dropped == layout.glyphs.Size());
    }
    {
        text::GlyphAtlas tiny(16, 1);  // a page too small for a 32 px glyph
        Fixture f(1.0f, &tiny);
        f.canvas.DrawText(layout, style, {0, 0}, kWhite);
        CHECK(f.list.vertices.Empty());
        CHECK(f.list.stats.glyphs_dropped == layout.glyphs.Size());
    }
}

TEST_CASE("DrawList: text above the atlas maximum is scaled up from the maximum", "[ui][drawlist][text]")
{
    ui_test::Opened o;
    text::GlyphAtlas atlas;
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = 256;
    text::TextLayout layout;
    REQUIRE(text::Layout(StringView("A"), style, text::LayoutOptions{}, &layout).Ok());
    Fixture f(1.0f, &atlas);
    f.canvas.DrawText(layout, style, {0, 0}, kWhite);
    CHECK(f.list.stats.scaled_large_glyphs == 1);
    REQUIRE(f.list.vertices.Size() == 4);
    CHECK(f.list.vertices[3].x - f.list.vertices[0].x > 128.0f);  // wider than a maximum-size glyph could be
}

TEST_CASE("DrawList: text outside the clip makes no vertex", "[ui][drawlist][text]")
{
    ui_test::Opened o;
    text::GlyphAtlas atlas;
    text::TextStyle style;
    style.font = &o.font;
    style.pixel_size = 16;
    text::TextLayout layout;
    REQUIRE(text::Layout(StringView("hidden"), style, text::LayoutOptions{}, &layout).Ok());
    Fixture f(1.0f, &atlas);
    f.canvas.PushClip({0, 0, 10, 10});
    f.canvas.DrawText(layout, style, {300, 300}, kWhite);
    CHECK(f.list.vertices.Empty());
    CHECK(f.list.stats.culled > 0);
    CHECK(f.list.stats.glyphs_dropped == 0);
}

TEST_CASE("DrawList: huge, infinite and NaN coordinates cannot break the integer conversions", "[ui][drawlist][limits]")
{
    Fixture f;
    const f32 inf = Limits<f32>::Infinity();  // constant arithmetic that overflows is an error on MSVC (C4056)
    const f32 nan = Limits<f32>::QuietNaN();
    f.canvas.PushClip({-1.0e12f, -1.0e12f, 2.0e12f, 2.0e12f});  // a scrolled-away content rectangle
    CHECK(f.canvas.CurrentClip() == PixelRect{0, 0, 800, 600});
    f.canvas.DrawRect({10, 10, 1.0e12f, 20}, kWhite);  // drawn, clipped by the scissor
    CHECK(f.list.vertices.Size() == 4);
    f.canvas.DrawRect({nan, 0, 10, 10}, kWhite);
    f.canvas.DrawRect({0, 0, inf, 10}, kWhite);
    f.canvas.DrawLine({0, 0}, {inf, 5}, 1, kWhite);
    f.canvas.DrawTriangle({nan, 0}, {1, 0}, {0, 1}, kWhite);
    f.canvas.PopClip();
    CHECK(f.list.vertices.Size() <= 20);  // nothing crashed; whatever was admitted is bounded by the clip
}
