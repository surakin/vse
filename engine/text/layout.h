#pragma once

#include "containers/vector.h"
#include "string_view.h"
#include "text/font.h"
#include "types.h"

//! Text layout: UTF-8 in, positioned glyphs, grapheme clusters and lines out. No shaping, no bidirectional
//! text, no vertical text.
//!
//! Coordinates are pixels with the origin at the top-left of the first line and y growing down (as the overlay pass
//! draws). A layout depends only on its input and the font's metrics at the style's size, and the same input gives
//! byte-identical output.
namespace vse::text
{

/// How a run of text is drawn: which font, at what size and in what color.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: borrows the font, which must outlive the call (and is not touched afterwards).
struct TextStyle
{
    Font* font = nullptr;     ///< The font; [[text::Layout]] sets its pixel size to `pixel_size`.
    u32 pixel_size = 16;      ///< Size in device pixels (the caller applies any display scale).
    u32 color = 0xFFFFFFFFu;  ///< Packed RGBA (red in the lowest byte), carried to the drawing pass; layout ignores it.
};

/// Horizontal alignment of each line inside the layout width.
enum class Align : u8
{
    Left,    ///< Lines start at x = 0.
    Center,  ///< Lines are centered in the layout width.
    Right,   ///< Lines end at the layout width.
};

/// Options of [[text::Layout]].
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct LayoutOptions
{
    f32 max_width = 0;             ///< Soft wrap width in pixels; 0 means no wrapping.
    f32 line_height_scale = 1.0f;  ///< Multiplies the font's line height.
    u32 tab_width_in_spaces = 4;   ///< A tab advances to the next multiple of this many space widths.
    Align
        align = Align::Left;  ///< Alignment of each line inside `max_width` (or the widest line when `max_width` is 0).
};

/// One glyph with the pen position it is drawn from.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct PositionedGlyph
{
    GlyphId glyph = 0;           ///< The glyph id (`.notdef` for a code point the font lacks).
    f32 x = 0;                   ///< Pen position: the glyph's origin on the baseline.
    f32 y = 0;                   ///< Baseline y of the glyph's line.
    f32 advance = 0;             ///< The glyph's advance (0 for combining marks, ZWJ and variation selectors).
    u32 source_byte_offset = 0;  ///< Byte offset of the glyph's code point in the input.
    u32 grapheme_index = 0;      ///< Index of the glyph's grapheme cluster (counting clusters from 0).
    u32 line = 0;                ///< Index of the glyph's line.
};

/// What a grapheme cluster is, as far as layout is concerned.
enum class ClusterKind : u8
{
    Normal,  ///< Drawn text.
    Space,   ///< A space (U+0020): a wrap opportunity.
    Tab,     ///< A tab: advances to the next tab stop, draws nothing.
    Break,   ///< A line break (`\n`, `\r\n`, `\r`): zero width, ends its line.
};

/// One grapheme cluster: the unit the cursor moves by.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct ClusterInfo
{
    u32 byte_begin = 0;   ///< First byte of the cluster in the input.
    u32 byte_end = 0;     ///< One past its last byte.
    u32 line = 0;         ///< The line it sits on.
    f32 x = 0;            ///< Left edge of the cluster (alignment included).
    f32 width = 0;        ///< Distance to the start of the next cluster's pen (the sum of its advances).
    u32 first_glyph = 0;  ///< Index of its first glyph in [[text::TextLayout::glyphs]].
    u32 glyph_count = 0;  ///< Its glyphs (0 for a line break or a tab).
    ClusterKind kind = ClusterKind::Normal;  ///< Whether it is text, a space, a tab or a line break.
};

/// One line of the layout.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct LineInfo
{
    u32 first_cluster = 0;  ///< Index of its first cluster in [[text::TextLayout::clusters]].
    u32 cluster_count = 0;  ///< Its clusters, including the line break that ends it (0 for the empty last line).
    u32 byte_begin = 0;     ///< First byte of the line.
    u32 byte_end = 0;       ///< One past its last byte, line break included.
    f32 x = 0;              ///< Alignment offset of the line's first cluster.
    f32 width = 0;          ///< Width of the visible text (trailing spaces and the break do not count).
    f32 top = 0;            ///< Top edge of the line.
    f32 height = 0;         ///< Line height (font line height times the scale).
    f32 baseline = 0;       ///< Baseline y.
};

/// The result of [[text::Layout]].
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its vectors.
struct TextLayout
{
    Vector<PositionedGlyph> glyphs;  ///< All glyphs in reading order.
    Vector<ClusterInfo> clusters;    ///< All grapheme clusters in order (their byte ranges tile the input).
    Vector<LineInfo> lines;          ///< At least one line (an empty string has one empty line).
    f32 width = 0;                   ///< Width of the widest line.
    f32 height = 0;                  ///< Sum of the line heights.
    u32 byte_length = 0;             ///< Length of the input in bytes.
    u32 invalid_sequences = 0;       ///< Malformed UTF-8 sequences, each drawn as U+FFFD.
};

/// Lays out `utf8` in one style.
///
/// One code point maps to one glyph through the font's character map; a cluster of several code points yields one glyph
/// per code point, with zero advance for combining marks, zero-width joiners and variation selectors (the no-shaping
/// limit). Kerning from the font's `kern` table applies between neighbouring clusters on a line. `\\n`,
/// `\\r\\n` and `\\r` end a line; a tab advances to the next tab stop; with `max_width` above 0 a line wraps after the
/// last space (U+0020; the only break opportunities are a space and a tab, so other whitespace such as U+00A0 or U+3000
/// never breaks) that fits, or between clusters when a word is wider than the line (never inside a cluster). Trailing
/// spaces never trigger a wrap and do not count towards the width.
///
/// - Param utf8: the text; malformed sequences become U+FFFD and are counted.
/// - Param style: the font and size.
/// - Param options: wrapping, line height, tabs, alignment.
/// - Param out: receives the layout; cleared first.
/// - Ownership: nothing is retained; `out` does not refer to `utf8` or the font.
/// - Fails: `InvalidArgument` for a style with no open font, a pixel size the font refuses, a `line_height_scale` not
/// in (0, 100] or a negative or non-finite `max_width`, text longer than 4 GiB (offsets are 32-bit); `OutOfMemory`;
/// `out` is left empty.
Status Layout(StringView utf8, const TextStyle& style, const LayoutOptions& options, TextLayout* out);

}  // namespace vse::text
