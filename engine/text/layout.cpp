#include "text/layout.h"

#include "containers/array.h"
#include "log/assert.h"
#include "string/grapheme.h"
#include "string/utf8_codec.h"

namespace vse::text
{

namespace
{

enum class Kind : u8
{
    Normal,
    Space,
    Tab,
    Break
};

// One grapheme cluster before it is placed: its glyphs with advances relative to the cluster start.
struct Item
{
    u32 byte_begin = 0;
    u32 byte_end = 0;
    u32 first_glyph = 0;  // index into the glyph list (cluster-local advances in `offsets`)
    u32 glyph_count = 0;
    Kind kind = Kind::Normal;
    GlyphId base = 0;  // the glyph kerning looks at
    f32 width = 0;     // sum of the glyphs' advances (a tab's width depends on its position)
    // set by placement
    f32 x = 0;
    f32 kern = 0;
    f32 placed_width = 0;
    u32 line = 0;
};

struct LocalGlyph
{
    GlyphId glyph = 0;
    f32 offset = 0;  // from the cluster's pen
    f32 advance = 0;
    u32 byte = 0;
};

// Advances are asked of the font once per glyph per layout.
class AdvanceCache
{
public:
    explicit AdvanceCache(const Font& font) : font_(font) {}
    f32 Get(GlyphId glyph)
    {
        Slot& slot = slots_[glyph % kSlots];
        if (!slot.used || slot.glyph != glyph)
        {
            slot.used = true;
            slot.glyph = glyph;
            slot.advance = font_.Metrics(glyph).advance;
        }
        return slot.advance;
    }

private:
    struct Slot
    {
        bool used = false;
        GlyphId glyph = 0;
        f32 advance = 0;
    };
    static constexpr u32 kSlots = 256;
    const Font& font_;
    Array<Slot, kSlots> slots_{};
};

bool ZeroAdvance(GraphemeBreakProperty p)
{
    return p == GraphemeBreakProperty::Extend || p == GraphemeBreakProperty::ZWJ;
}

}  // namespace

Status Layout(StringView utf8, const TextStyle& style, const LayoutOptions& options, TextLayout* out)
{
    ENGINE_ASSERT(out != nullptr, "Layout: out is null");
    out->glyphs.Clear();
    out->clusters.Clear();
    out->lines.Clear();
    out->width = out->height = 0;
    out->byte_length = 0;
    out->invalid_sequences = 0;
    if (style.font == nullptr || !style.font->IsOpen())
    {
        return Status::Of(Error::InvalidArgument, "the style has no open font");
    }
    if (utf8.Length() > 0xFFFFFFFFull)
    {
        return Status::Of(Error::InvalidArgument, "the text is longer than 4 GiB: byte offsets are 32-bit");
    }
    if (!(options.line_height_scale > 0.0f) || options.line_height_scale > 100.0f)
    {
        return Status::Of(Error::InvalidArgument, "line_height_scale must be above 0 and at most 100");
    }
    if (options.max_width < 0.0f || !(options.max_width < 1.0e9f))
    {
        return Status::Of(Error::InvalidArgument, "max_width must be 0 or a finite positive width");
    }
    Font& font = *style.font;
    if (font.PixelSize() != style.pixel_size)
    {
        const Status size = font.SetPixelSize(style.pixel_size);
        if (!size.Ok())
        {
            return size;
        }
    }
    const FontMetrics metrics = font.Metrics();
    AdvanceCache advances(font);
    const GlyphId space_glyph = font.FindGlyph(U' ');
    f32 tab_stop = static_cast<f32>(options.tab_width_in_spaces == 0 ? 1 : options.tab_width_in_spaces) *
                   advances.Get(space_glyph);
    if (!(tab_stop > 0.5f))
    {
        tab_stop = static_cast<f32>(style.pixel_size);  // a font with no usable space glyph: a tab is one em
    }

    // --- pass 1: clusters and their glyphs ---
    Vector<Item> items;
    Vector<LocalGlyph> local;
    if (!items.TryReserve(utf8.Length()) || !local.TryReserve(utf8.Length()))
    {
        return Status::Of(Error::OutOfMemory, "out of memory laying out the text");
    }
    for (StringView cluster : GraphemeClusters(utf8))
    {
        Item item;
        item.byte_begin = static_cast<u32>(cluster.Data() - utf8.Data());
        item.byte_end = item.byte_begin + static_cast<u32>(cluster.Length());
        item.first_glyph = static_cast<u32>(local.Size());
        usize at = 0;
        f32 pen = 0;
        bool first = true;
        while (at < cluster.Length())
        {
            bool valid = true;
            const DecodedCodepoint d = DecodeUtf8At(cluster.Data(), cluster.Length(), at, &valid);
            if (!valid)
            {
                ++out->invalid_sequences;
            }
            if (first)
            {
                if (d.codepoint == '\n' || d.codepoint == '\r')
                {
                    item.kind = Kind::Break;
                }
                else if (d.codepoint == '\t')
                {
                    item.kind = Kind::Tab;
                }
                else if (d.codepoint == ' ')
                {
                    item.kind = Kind::Space;
                }
            }
            if (item.kind != Kind::Break && item.kind != Kind::Tab)
            {
                LocalGlyph g;
                g.glyph = font.FindGlyph(d.codepoint);
                g.byte = item.byte_begin + static_cast<u32>(at);
                g.offset = pen;
                g.advance = (!first && ZeroAdvance(LookupGraphemeBreakProperty(d.codepoint))) ? 0.0f
                                                                                              : advances.Get(g.glyph);
                if (first)
                {
                    item.base = g.glyph;
                }
                pen += g.advance;
                local.Push(g);
                ++item.glyph_count;
            }
            first = false;
            at += d.byte_length;
        }
        item.width = pen;
        items.Push(item);
    }

    // --- pass 2: line breaking and placement ---
    const usize n = items.Size();
    const f32 line_height = metrics.line_height * options.line_height_scale;
    const f32 baseline_in_line = metrics.ascent + (line_height - metrics.line_height) * 0.5f;
    usize line_start = 0;
    u32 line_index = 0;
    bool ended_with_break = false;
    struct Range
    {
        usize first;
        usize end;
    };
    Vector<Range> ranges;  // at most one line per cluster, plus the empty last line
    if (!ranges.TryReserve(n + 1))
    {
        return Status::Of(Error::OutOfMemory, "out of memory laying out the text");
    }
    while (line_start < n)
    {
        f32 x = 0;
        bool have_prev = false;
        GlyphId prev = 0;
        bool have_break = false;
        usize last_break = 0;
        usize i = line_start;
        ended_with_break = false;
        while (i < n)
        {
            Item& item = items[i];
            if (item.kind == Kind::Break)
            {
                item.x = x;
                item.kern = 0;
                item.placed_width = 0;
                item.line = line_index;
                ++i;
                ended_with_break = true;
                break;
            }
            f32 kern = 0;
            f32 width = item.width;
            if (item.kind == Kind::Tab)
            {
                width = (static_cast<f32>(static_cast<u64>(x / tab_stop) + 1) * tab_stop) - x;
            }
            else if (have_prev)
            {
                kern = font.Kerning(prev, item.base);
            }
            const bool visible = item.kind == Kind::Normal;
            if (options.max_width > 0 && visible && i > line_start && x + kern + width > options.max_width)
            {
                if (have_break && last_break + 1 > line_start)
                {
                    i = last_break + 1;
                }
                break;  // soft wrap: the cluster (or everything after the last space) starts the next line
            }
            x += kern;
            item.x = x;
            item.kern = kern;
            item.placed_width = width;
            item.line = line_index;
            x += width;
            if (item.kind != Kind::Normal)
            {
                have_break = true;
                last_break = i;
            }
            have_prev = item.kind != Kind::Tab;
            prev = item.base;
            ++i;
        }
        ranges.Push(Range{line_start, i});
        line_start = i;
        ++line_index;
    }
    if (n == 0 || ended_with_break)
    {
        ranges.Push(Range{n, n});  // the empty last line after a trailing break (and the one line of an empty text)
    }

    // --- pass 3: lines, alignment, output ---
    if (!out->glyphs.TryReserve(local.Size()) || !out->clusters.TryReserve(n) || !out->lines.TryReserve(ranges.Size()))
    {
        return Status::Of(Error::OutOfMemory, "out of memory laying out the text");
    }
    f32 widest = 0;
    for (usize r = 0; r < ranges.Size(); ++r)
    {
        f32 visible_width = 0;
        for (usize k = ranges[r].first; k < ranges[r].end; ++k)
        {
            const Item& it = items[k];
            if (it.kind == Kind::Normal)
            {
                const f32 right = it.x + it.placed_width;
                if (right > visible_width)
                {
                    visible_width = right;
                }
            }
        }
        if (visible_width > widest)
        {
            widest = visible_width;
        }
    }
    const f32 layout_width = options.max_width > 0 ? options.max_width : widest;
    u32 byte_cursor = 0;
    for (usize r = 0; r < ranges.Size(); ++r)
    {
        LineInfo line;
        line.first_cluster = static_cast<u32>(ranges[r].first);
        line.cluster_count = static_cast<u32>(ranges[r].end - ranges[r].first);
        f32 visible_width = 0;
        for (usize k = ranges[r].first; k < ranges[r].end; ++k)
        {
            const Item& it = items[k];
            if (it.kind == Kind::Normal)
            {
                const f32 right = it.x + it.placed_width;
                if (right > visible_width)
                {
                    visible_width = right;
                }
            }
        }
        line.width = visible_width;
        line.x = options.align == Align::Center  ? (layout_width - visible_width) * 0.5f
                 : options.align == Align::Right ? layout_width - visible_width
                                                 : 0.0f;
        if (line.x < 0)
        {
            line.x = 0;
        }
        line.top = static_cast<f32>(r) * line_height;
        line.height = line_height;
        line.baseline = line.top + baseline_in_line;
        line.byte_begin = ranges[r].first < n ? items[ranges[r].first].byte_begin : byte_cursor;
        line.byte_end = ranges[r].end > ranges[r].first ? items[ranges[r].end - 1].byte_end : line.byte_begin;
        byte_cursor = line.byte_end;
        for (usize k = ranges[r].first; k < ranges[r].end; ++k)
        {
            const Item& it = items[k];
            ClusterInfo c;
            c.byte_begin = it.byte_begin;
            c.byte_end = it.byte_end;
            c.line = static_cast<u32>(r);
            c.x = line.x + it.x;
            c.width = it.placed_width;
            c.first_glyph = static_cast<u32>(out->glyphs.Size());
            c.glyph_count = it.glyph_count;
            c.kind = it.kind == Kind::Break   ? ClusterKind::Break
                     : it.kind == Kind::Tab   ? ClusterKind::Tab
                     : it.kind == Kind::Space ? ClusterKind::Space
                                              : ClusterKind::Normal;
            for (u32 g = 0; g < it.glyph_count; ++g)
            {
                const LocalGlyph& lg = local[it.first_glyph + g];
                PositionedGlyph pg;
                pg.glyph = lg.glyph;
                pg.x = line.x + it.x + lg.offset;
                pg.y = line.baseline;
                pg.advance = lg.advance;
                pg.source_byte_offset = lg.byte;
                pg.grapheme_index = static_cast<u32>(k);
                pg.line = static_cast<u32>(r);
                out->glyphs.Push(pg);
            }
            out->clusters.Push(c);
        }
        out->lines.Push(line);
    }
    out->width = widest;
    out->height = static_cast<f32>(ranges.Size()) * line_height;
    out->byte_length = static_cast<u32>(utf8.Length());
    return Status{};
}

}  // namespace vse::text
