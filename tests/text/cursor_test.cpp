#include <catch2/catch_test_macros.hpp>
#include "cstr/cstr.h"
#include "lang/limits.h"

#include "font_support.h"
#include "text/cursor.h"

using namespace vse;
using namespace vse::text;
using text_test::Opened;

namespace
{

TextLayout CLay(Opened& o, const char* text, f32 max_width = 0, u32 size = 24)
{
    TextStyle style;
    style.font = &o.font;
    style.pixel_size = size;
    LayoutOptions options;
    options.max_width = max_width;
    TextLayout layout;
    REQUIRE(Layout(StringView(text, CStrLength(text)), style, options, &layout).Ok());
    return layout;
}

// Every grapheme boundary, in order, by walking NextBoundary from 0.
Vector<u32> Walk(const TextLayout& layout)
{
    Vector<u32> offsets;
    u32 at = 0;
    offsets.Push(0);
    while (at < layout.byte_length)
    {
        const u32 next = NextBoundary(layout, at);
        REQUIRE(next > at);
        offsets.Push(next);
        at = next;
    }
    return offsets;
}

const char* const kStrings[] = {
    "Hello world",
    ("caf\xC3\xA9 caf"
     "e\xCC\x81"),                                                       // precomposed and combining accents
    "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7",  // two flags (four regional indicators)
    "x\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7y",  // ZWJ family between letters
    "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB a",                                      // Hangul jamo L V T
    "line one\nline two is longer\r\n\nlast",
    "alpha beta gamma delta epsilon zeta eta theta",
    "",
    "\xCC\x81\xCC\x81",  // only combining marks
    "trailing \n",
};

}  // namespace

TEST_CASE("NextBoundary walks every cluster exactly once and ends at the length", "[text][cursor]")
{
    Opened o;
    for (const char* text : kStrings)
    {
        const TextLayout layout = CLay(o, text);
        const Vector<u32> walk = Walk(layout);
        REQUIRE(walk.Size() == layout.clusters.Size() + 1);
        for (usize i = 0; i < layout.clusters.Size(); ++i)
        {
            CHECK(walk[i] == layout.clusters[i].byte_begin);
        }
        CHECK(walk[walk.Size() - 1] == layout.byte_length);
        // And back: PreviousBoundary retraces the walk.
        u32 at = layout.byte_length;
        for (usize i = walk.Size() - 1; i > 0; --i)
        {
            at = PreviousBoundary(layout, at);
            CHECK(at == walk[i - 1]);
        }
        CHECK(PreviousBoundary(layout, 0) == 0);
        CHECK(NextBoundary(layout, layout.byte_length) == layout.byte_length);
    }
}

TEST_CASE("Combining marks, flags, ZWJ sequences and jamo are one step", "[text][cursor]")
{
    Opened o;
    CHECK(Walk(CLay(o, "caf\xC3\xA9")).Size() == 5);                       // four clusters
    CHECK(Walk(CLay(o, "cafe\xCC\x81")).Size() == 5);                      // e + combining acute is one
    CHECK(Walk(CLay(o, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8")).Size() == 2);  // one flag
    CHECK(Walk(CLay(o, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7")).Size() == 3);
    CHECK(Walk(CLay(o, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7")).Size() == 2);
    CHECK(Walk(CLay(o, "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB")).Size() == 2);
    // An offset inside a cluster snaps to its start.
    const TextLayout accent = CLay(o, "e\xCC\x81z");
    CHECK(PositionOfOffset(accent, 1).x == PositionOfOffset(accent, 0).x);
    CHECK(NextBoundary(accent, 1) == 3);
    CHECK(PreviousBoundary(accent, 2) == 0);
}

TEST_CASE("Positions: offsets, the end of the text, and the empty string", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "ab\ncd");
    const CursorPosition p0 = PositionOfOffset(layout, 0);
    CHECK(p0.x == 0);
    CHECK(p0.line == 0);
    CHECK(p0.height == layout.lines[0].height);
    CHECK(PositionOfOffset(layout, 1).x == layout.glyphs[0].advance);
    const CursorPosition line2 = PositionOfOffset(layout, 3);
    CHECK(line2.line == 1);
    CHECK(line2.x == 0);
    CHECK(line2.y == layout.lines[1].top);
    const CursorPosition end = PositionOfOffset(layout, layout.byte_length);
    CHECK(end.line == 1);
    CHECK(end.x == layout.clusters[4].x + layout.clusters[4].width);
    CHECK(PositionOfOffset(layout, 9999).x == end.x);  // past the end is the end
    const TextLayout empty = CLay(o, "");
    const CursorPosition e = PositionOfOffset(empty, 0);
    CHECK(e.x == 0);
    CHECK(e.line == 0);
    CHECK(e.height == empty.lines[0].height);
    CHECK(OffsetAtPoint(empty, 50, 50) == 0);
    CHECK(NextBoundary(empty, 0) == 0);
    // A trailing line break gives a caret on the empty last line.
    const TextLayout trailing = CLay(o, "a\n");
    CHECK(PositionOfOffset(trailing, trailing.byte_length).line == 1);
}

TEST_CASE("Hit testing picks the nearest boundary and clamps outside the text", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "abc\ndef");
    const ClusterInfo& b = layout.clusters[1];
    const f32 y0 = layout.lines[0].top + layout.lines[0].height * 0.5f;
    CHECK(OffsetAtPoint(layout, b.x + b.width * 0.25f, y0) == b.byte_begin);
    CHECK(OffsetAtPoint(layout, b.x + b.width * 0.75f, y0) == b.byte_end);
    CHECK(OffsetAtPoint(layout, -50, y0) == 0);
    CHECK(OffsetAtPoint(layout, 5000, y0) == 3);  // end of the visible text, before the break
    CHECK(OffsetAtPoint(layout, 0, -40) == 0);    // above: first line
    CHECK(OffsetAtPoint(layout, 5000, -40) == 3);
    CHECK(OffsetAtPoint(layout, 5000, 5000) == layout.byte_length);  // below: last line, right of it
    CHECK(OffsetAtPoint(layout, -1, 5000) == 4);
    const f32 y1 = layout.lines[1].top + 2;
    CHECK(OffsetAtPoint(layout, layout.clusters[5].x + 1, y1) == 5);
}

TEST_CASE("Round trip: every boundary hits itself, every point lands within half a cluster", "[text][cursor]")
{
    Opened o;
    for (const char* text : kStrings)
    {
        for (f32 width : {0.0f, 90.0f})
        {
            const TextLayout layout = CLay(o, text, width);
            const Vector<u32> walk = Walk(layout);
            for (usize i = 0; i < walk.Size(); ++i)
            {
                const u32 offset = walk[i];
                const CursorPosition pos = PositionOfOffset(layout, offset);
                // A boundary among the trailing spaces of a soft-wrapped line is not reachable by a click (it clamps to
                // the visible end).
                const LineInfo& line = layout.lines[pos.line];
                if (pos.line + 1 < layout.lines.Size() && offset > LineEnd(layout, offset) && offset < line.byte_end)
                {
                    continue;
                }
                if (i > 0)
                {  // a cluster with no width (a lone combining mark) shares its x with the next boundary; a click
                   // chooses the earlier one
                    const CursorPosition before = PositionOfOffset(layout, walk[i - 1]);
                    if (before.line == pos.line && before.x == pos.x)
                    {
                        continue;
                    }
                }
                INFO(text << " width " << width << " offset " << offset);
                CHECK(OffsetAtPoint(layout, pos.x, pos.y + pos.height * 0.5f) == offset);
            }
            for (usize l = 0; l < layout.lines.Size(); ++l)
            {
                const LineInfo& line = layout.lines[l];
                f32 max_cluster = 1;
                for (u32 k = 0; k < line.cluster_count; ++k)
                {
                    if (layout.clusters[line.first_cluster + k].width > max_cluster)
                    {
                        max_cluster = layout.clusters[line.first_cluster + k].width;
                    }
                }
                // NOLINTNEXTLINE(bugprone-float-loop-counter): sample positions along a line; the count is irrelevant
                for (f32 x = line.x; x <= line.x + line.width; x += 3.7f)
                {
                    const f32 y = line.top + line.height * 0.5f;
                    const u32 offset = OffsetAtPoint(layout, x, y);
                    const CursorPosition pos = PositionOfOffset(layout, offset);
                    INFO(text << " x " << x << " line " << l);
                    CHECK(pos.line == l);
                    CHECK(pos.x >= x - max_cluster * 0.5f - 0.01f);
                    CHECK(pos.x <= x + max_cluster * 0.5f + 0.01f);
                }
            }
        }
    }
}

TEST_CASE("Line start and end, and vertical movement keeps the column", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "first line here\nab\nthird line is long");
    CHECK(LineStart(layout, 5) == 0);
    CHECK(LineEnd(layout, 5) == 15);
    CHECK(LineStart(layout, 17) == 16);
    CHECK(LineEnd(layout, 17) == 18);
    CHECK(LineEnd(layout, layout.byte_length) == layout.byte_length);
    const f32 column = PositionOfOffset(layout, 10).x;
    const u32 down = LineBelow(layout, 10, column);
    CHECK(down == 18);                                  // the short line clamps to its end
    const u32 down2 = LineBelow(layout, down, column);  // the preferred column comes back on the longer line
    CHECK(PositionOfOffset(layout, down2).line == 2);
    const f32 landed = PositionOfOffset(layout, down2).x;
    CHECK(landed >= column - 12);
    CHECK(landed <= column + 12);
    CHECK(LineAbove(layout, down2, column) == 18);
    CHECK(LineAbove(layout, 3, 0) == 0);                                    // the first line: start of text
    CHECK(LineBelow(layout, layout.byte_length, 0) == layout.byte_length);  // the last line: end of text
}

TEST_CASE("Soft-wrapped lines: the end skips trailing spaces; wrap offsets belong to the later line", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "alpha beta gamma delta", 95);
    REQUIRE(layout.lines.Size() >= 2);
    const LineInfo& first = layout.lines[0];
    const u32 end = LineEnd(layout, 0);
    CHECK(end < first.byte_end);  // "alpha " ends before its space
    CHECK(end == 5);
    CHECK(PositionOfOffset(layout, first.byte_end).line == 1);
    CHECK(LineStart(layout, first.byte_end) == first.byte_end);
    const TextLayout mid = CLay(o, "WWWWWWWWWW", 60);
    CHECK(LineEnd(mid, 0) == mid.lines[0].byte_end);  // no space to skip
}

TEST_CASE("Selection rectangles: one per line, order independent", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "one two\nthree four\nfive");
    Vector<SelectionRect> rects;
    REQUIRE(SelectionRects(layout, 4, 4, &rects).Ok());
    CHECK(rects.Size() == 0);
    REQUIRE(SelectionRects(layout, 4, 7, &rects).Ok());
    REQUIRE(rects.Size() == 1);
    CHECK(rects[0].x == PositionOfOffset(layout, 4).x);
    CHECK(rects[0].y == layout.lines[0].top);
    CHECK(rects[0].h == layout.lines[0].height);
    CHECK(rects[0].w == PositionOfOffset(layout, 7).x - PositionOfOffset(layout, 4).x);
    Vector<SelectionRect> forward;
    Vector<SelectionRect> backward;
    REQUIRE(SelectionRects(layout, 4, 21, &forward).Ok());
    REQUIRE(SelectionRects(layout, 21, 4, &backward).Ok());
    REQUIRE(forward.Size() == 3);
    REQUIRE(backward.Size() == 3);
    for (usize i = 0; i < 3; ++i)
    {
        CHECK(forward[i].x == backward[i].x);
        CHECK(forward[i].w == backward[i].w);
        CHECK(forward[i].y == layout.lines[i].top);
    }
    CHECK(forward[1].x == layout.lines[1].x);                    // a middle line is covered from its start
    CHECK(forward[1].w == layout.lines[1].width);                // to the end of its text (the break is not painted)
    CHECK(forward[0].x + forward[0].w == layout.clusters[7].x);  // the first line runs to the end of its text
    // A blank line inside a selection gives no rectangle.
    const TextLayout blank = CLay(o, "a\n\nb");
    REQUIRE(SelectionRects(blank, 0, blank.byte_length, &rects).Ok());
    CHECK(rects.Size() == 2);
    // Selecting inside a cluster snaps to the cluster start (no rectangle for half an accent).
    const TextLayout accent = CLay(o, "e\xCC\x81z");
    REQUIRE(SelectionRects(accent, 0, 1, &rects).Ok());
    CHECK(rects.Size() == 0);
}

TEST_CASE("Aligned layouts hit-test and select relative to their line offsets", "[text][cursor]")
{
    Opened o;
    for (Align align : {Align::Center, Align::Right})
    {
        TextStyle style;
        style.font = &o.font;
        TextLayout layout;
        LayoutOptions options;
        options.max_width = 300;
        options.align = align;
        REQUIRE(Layout(StringView("Hi\nWorld wide", 13), style, options, &layout).Ok());
        REQUIRE(layout.lines.Size() == 2);
        CHECK(layout.lines[0].x > 0);
        for (u32 offset : {0u, 1u, 2u, 3u, 5u, 9u})
        {
            const CursorPosition pos = PositionOfOffset(layout, offset);
            CHECK(OffsetAtPoint(layout, pos.x, pos.y + pos.height * 0.5f) == offset);
        }
        CHECK(OffsetAtPoint(layout, 0, layout.lines[0].top + 2) == 0);  // left of an indented line is its start
        Vector<SelectionRect> rects;
        REQUIRE(SelectionRects(layout, 0, 2, &rects).Ok());
        REQUIRE(rects.Size() == 1);
        CHECK(rects[0].x == layout.lines[0].x);
        CHECK(rects[0].w == layout.lines[0].width);
    }
}

TEST_CASE("Extreme and not-a-number points are clamped, not converted", "[text][cursor]")
{
    Opened o;
    const TextLayout layout = CLay(o, "ab\ncd");
    const f32 huge = 1.0e30f;
    const f32 nan = Limits<f32>::QuietNaN();  // constant arithmetic that overflows is an error on MSVC (C4056)
    CHECK(OffsetAtPoint(layout, 0, huge) == 3);
    CHECK(OffsetAtPoint(layout, huge, huge) == layout.byte_length);
    CHECK(OffsetAtPoint(layout, 0, -huge) == 0);
    CHECK(OffsetAtPoint(layout, 0, nan) == 0);
    const f32 inf = Limits<f32>::Infinity();
    CHECK(OffsetAtPoint(layout, inf, inf) == layout.byte_length);
}
