#include <catch2/catch_test_macros.hpp>
#include "cstr/cstr.h"

#include "font_support.h"
#include "text/layout.h"

using namespace vse;
using namespace vse::text;
using text_test::Opened;

namespace
{

TextLayout Lay(Opened& o, const char* text, u32 size = 24, f32 max_width = 0, Align align = Align::Left, u32 tab = 4)
{
    TextStyle style;
    style.font = &o.font;
    style.pixel_size = size;
    LayoutOptions options;
    options.max_width = max_width;
    options.align = align;
    options.tab_width_in_spaces = tab;
    TextLayout layout;
    const Status status = Layout(StringView(text, CStrLength(text)), style, options, &layout);
    REQUIRE(status.Ok());
    return layout;
}

void RequireTiling(const TextLayout& layout)
{
    u32 at = 0;
    for (usize i = 0; i < layout.clusters.Size(); ++i)
    {
        REQUIRE(layout.clusters[i].byte_begin == at);
        REQUIRE(layout.clusters[i].byte_end > at);
        at = layout.clusters[i].byte_end;
    }
    REQUIRE(at == layout.byte_length);
}

}  // namespace

TEST_CASE("Advances of Hello, world! add up to the font's advances plus kerning", "[text][layout]")
{
    Opened o;
    const char* text = "Hello, world!";
    const TextLayout layout = Lay(o, text);
    REQUIRE(layout.glyphs.Size() == 13);
    f32 pen = 0;
    for (usize i = 0; i < layout.glyphs.Size(); ++i)
    {
        if (i > 0)
        {
            pen += o.font.Kerning(layout.glyphs[i - 1].glyph, layout.glyphs[i].glyph);
        }
        CHECK(layout.glyphs[i].x == pen);
        CHECK(layout.glyphs[i].glyph == o.font.FindGlyph(static_cast<char32>(text[i])));
        CHECK(layout.glyphs[i].advance == o.font.Metrics(layout.glyphs[i].glyph).advance);
        CHECK(layout.glyphs[i].source_byte_offset == i);
        CHECK(layout.glyphs[i].grapheme_index == i);
        CHECK(layout.glyphs[i].line == 0);
        pen += layout.glyphs[i].advance;
    }
    CHECK(layout.width == pen);
    CHECK(layout.lines.Size() == 1);
    CHECK(layout.height == o.font.Metrics().line_height);
    RequireTiling(layout);
}

TEST_CASE("A kerned pair is closer than the unkerned sum", "[text][layout]")
{
    Opened o;
    const TextLayout av = Lay(o, "AV", 48);
    const f32 unkerned = o.font.Metrics(o.font.FindGlyph(U'A')).advance;
    CHECK(av.glyphs[1].x < unkerned);
    const TextLayout ab = Lay(o, "ab", 48);
    CHECK(ab.glyphs[1].x == o.font.Metrics(o.font.FindGlyph(U'a')).advance);
}

TEST_CASE("Wrapping breaks at spaces and never inside a cluster", "[text][layout]")
{
    Opened o;
    const char* text = "alpha beta gamma delta epsilon";
    const TextLayout layout = Lay(o, text, 20, 130);
    REQUIRE(layout.lines.Size() > 2);
    for (usize i = 0; i < layout.lines.Size(); ++i)
    {
        const LineInfo& line = layout.lines[i];
        CHECK(line.width <= 130.0f + 0.01f);
        if (i > 0)
        {
            CHECK(text[line.byte_begin] != ' ');  // a line starts with a word
        }
    }
    RequireTiling(layout);
    // Lines tile the bytes too.
    u32 at = 0;
    for (usize i = 0; i < layout.lines.Size(); ++i)
    {
        CHECK(layout.lines[i].byte_begin == at);
        at = layout.lines[i].byte_end;
    }
    CHECK(at == layout.byte_length);
    // Combining acute after e: the pair is one cluster, so a line start is never between them.
    const TextLayout accents = Lay(o,
                                   "e\xCC\x81"
                                   "e\xCC\x81"
                                   "e\xCC\x81"
                                   "e\xCC\x81"
                                   "e\xCC\x81"
                                   "e\xCC\x81",
                                   24, 30);
    CHECK(accents.lines.Size() > 1);
    for (usize i = 0; i < accents.lines.Size(); ++i)
    {
        CHECK(accents.lines[i].byte_begin % 3 == 0);
    }
    CHECK(accents.glyphs.Size() == 12);
    CHECK(accents.glyphs[1].advance == 0);  // the combining mark
    CHECK(accents.glyphs[1].x ==
          accents.glyphs[0].x +
              accents.glyphs[0]
                  .advance);  // the mark is drawn at the pen after its base (fonts give it a negative bearing)
}

TEST_CASE("A word wider than the line wraps between clusters", "[text][layout]")
{
    Opened o;
    const TextLayout layout = Lay(o, "WWWWWWWWWW", 24, 50);
    CHECK(layout.lines.Size() > 3);
    for (usize i = 0; i < layout.lines.Size(); ++i)
    {
        CHECK(layout.lines[i].cluster_count >= 1);
    }
    RequireTiling(layout);
    const TextLayout narrow = Lay(o, "WW", 24,
                                  1);  // narrower than one glyph: still one cluster per line, no endless loop
    CHECK(narrow.lines.Size() == 2);
}

TEST_CASE("A ZWJ family emoji is one cluster that draws its glyphs", "[text][layout]")
{
    Opened o;
    const char*
        family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    const TextLayout layout = Lay(o, family);
    CHECK(layout.clusters.Size() == 1);
    REQUIRE(layout.glyphs.Size() == 7);
    for (usize i = 0; i < 7; ++i)
    {
        CHECK(layout.glyphs[i].grapheme_index == 0);
    }
    CHECK(layout.glyphs[1].advance == 0);  // the joiners add nothing
    CHECK(layout.glyphs[3].advance == 0);
    CHECK(layout.glyphs[0].glyph == 0);  // the shipped font has no emoji: .notdef
    RequireTiling(layout);
}

TEST_CASE("Invalid UTF-8 becomes U+FFFD and is counted", "[text][layout]")
{
    Opened o;
    const TextLayout layout = Lay(o, "a\xFF"
                                     "b\xC3");
    CHECK(layout.invalid_sequences == 2);
    CHECK(layout.glyphs.Size() == 4);
    CHECK(layout.glyphs[1].glyph == o.font.FindGlyph(0xFFFD));
    RequireTiling(layout);
}

TEST_CASE("The empty string is one empty line; line breaks of every kind", "[text][layout]")
{
    Opened o;
    const TextLayout empty = Lay(o, "");
    CHECK(empty.glyphs.Size() == 0);
    REQUIRE(empty.lines.Size() == 1);
    CHECK(empty.lines[0].cluster_count == 0);
    CHECK(empty.height == o.font.Metrics().line_height);
    const TextLayout crlf = Lay(o, "a\r\nb");
    CHECK(crlf.clusters.Size() == 3);
    REQUIRE(crlf.lines.Size() == 2);
    CHECK(crlf.glyphs.Size() == 2);
    CHECK(crlf.glyphs[1].line == 1);
    CHECK(crlf.glyphs[1].y > crlf.glyphs[0].y);
    CHECK(crlf.glyphs[1].x == 0);
    const TextLayout lf = Lay(o, "a\nb\n");
    CHECK(lf.lines.Size() == 3);  // the empty line after the last break
    CHECK(lf.lines[2].cluster_count == 0);
    CHECK(lf.height == 3 * o.font.Metrics().line_height);
    const TextLayout blank_middle = Lay(o, "a\n\nb");
    CHECK(blank_middle.lines.Size() == 3);
    CHECK(blank_middle.lines[1].width == 0);
    RequireTiling(crlf);
    RequireTiling(lf);
}

TEST_CASE("Tabs advance to the next tab stop", "[text][layout]")
{
    Opened o;
    const TextLayout lead = Lay(o, "\tb", 24);
    REQUIRE(o.font.SetPixelSize(24).Ok());
    const f32 stop = 4 * o.font.Metrics(o.font.FindGlyph(U' ')).advance;
    CHECK(lead.glyphs[0].x == stop);
    const TextLayout mid = Lay(o, "a\tb", 24);
    CHECK(mid.glyphs[1].x == stop);
    const TextLayout two = Lay(o, "a\t\tb", 24);
    CHECK(two.glyphs[1].x == 2 * stop);
    const TextLayout wide = Lay(o, "\tb", 24, 0, Align::Left, 8);
    CHECK(wide.glyphs[0].x == 2 * stop);
}

TEST_CASE("Alignment offsets each line inside the width", "[text][layout]")
{
    Opened o;
    const TextLayout left = Lay(o, "Hi\nWorld", 24, 300, Align::Left);
    const TextLayout center = Lay(o, "Hi\nWorld", 24, 300, Align::Center);
    const TextLayout right = Lay(o, "Hi\nWorld", 24, 300, Align::Right);
    CHECK(left.lines[0].x == 0);
    for (usize i = 0; i < 2; ++i)
    {
        CHECK(center.lines[i].x == (300 - center.lines[i].width) * 0.5f);
        CHECK(right.lines[i].x == 300 - right.lines[i].width);
        CHECK(right.glyphs[0].x == right.lines[0].x);
    }
    CHECK(center.lines[0].x > center.lines[1].x);  // the shorter line is indented more
    // Without max_width the widest line is the reference.
    const TextLayout widest = Lay(o, "Hi\nWorld", 24, 0, Align::Right);
    CHECK(widest.lines[1].x == 0);
    CHECK(widest.lines[0].x > 0);
}

TEST_CASE("Layout is deterministic and glyph offsets follow the text", "[text][layout]")
{
    Opened o;
    const char* text = "The quick brown fox\njumps over the lazy dog, \xC3\xA9\xCC\x81 \xCE\xA9";
    const TextLayout a = Lay(o, text, 18, 150);
    const TextLayout b = Lay(o, text, 18, 150);
    REQUIRE(a.glyphs.Size() == b.glyphs.Size());
    bool same = true;
    for (usize i = 0; i < a.glyphs.Size(); ++i)
    {
        same = same && a.glyphs[i].glyph == b.glyphs[i].glyph && a.glyphs[i].x == b.glyphs[i].x &&
               a.glyphs[i].y == b.glyphs[i].y;
    }
    CHECK(same);
    for (usize i = 1; i < a.glyphs.Size(); ++i)
    {
        CHECK(a.glyphs[i].source_byte_offset > a.glyphs[i - 1].source_byte_offset);
    }
    RequireTiling(a);
}

TEST_CASE("Layout refuses a style without a font", "[text][layout]")
{
    TextStyle style;
    TextLayout layout;
    CHECK(Layout(StringView("x", 1), style, LayoutOptions{}, &layout).Is(Error::InvalidArgument));
    CHECK(layout.lines.Size() == 0);
}

TEST_CASE("Layout refuses nonsense options", "[text][layout]")
{
    Opened o;
    TextStyle style;
    style.font = &o.font;
    TextLayout layout;
    LayoutOptions bad;
    bad.line_height_scale = 0;
    CHECK(Layout(StringView("x", 1), style, bad, &layout).Is(Error::InvalidArgument));
    bad.line_height_scale = -1;
    CHECK(Layout(StringView("x", 1), style, bad, &layout).Is(Error::InvalidArgument));
    bad.line_height_scale = 1;
    bad.max_width = -5;
    CHECK(Layout(StringView("x", 1), style, bad, &layout).Is(Error::InvalidArgument));
    CHECK(layout.glyphs.Size() == 0);
}

TEST_CASE("A long text with many lines lays out", "[text][layout]")
{
    Opened o;
    Vector<char> text;
    for (u32 i = 0; i < 20000; ++i)
    {
        text.Push('a' + static_cast<char>(i % 26));
        if (i % 5 == 4)
        {
            text.Push('\n');
        }
    }
    TextStyle style;
    style.font = &o.font;
    TextLayout layout;
    REQUIRE(Layout(StringView(text.Data(), text.Size()), style, LayoutOptions{}, &layout).Ok());
    CHECK(layout.lines.Size() == 4001);
    RequireTiling(layout);
}
