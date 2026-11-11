#pragma once

#include "containers/vector.h"
#include "text/layout.h"
#include "types.h"

//! Cursor positions, hit-testing, movement and selection over a [[text::TextLayout]] (Phase 14).
//!
//! Offsets are byte offsets into the laid-out UTF-8 text, and the only valid cursor offsets are **grapheme
//! boundaries**: the start of a cluster, or the length of the text. An offset inside a cluster is snapped to that
//! cluster's start. Combining marks, ZWJ sequences and flags are one step. At a soft wrap the offset where one line
//! ends and the next begins belongs to the later line (there is no caret affinity). Word movement is not provided
//! (Phase 15).
namespace engine_placeholder::text
{

/// Where a caret is drawn.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct CursorPosition
{
    f32 x = 0;       ///< Horizontal position of the caret.
    f32 y = 0;       ///< Top of the caret (the top of its line).
    f32 height = 0;  ///< Height of the caret (the line height).
    u32 line = 0;    ///< The line the caret is on.
};

/// A rectangle in layout coordinates.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct SelectionRect
{
    f32 x = 0;  ///< Left edge.
    f32 y = 0;  ///< Top edge.
    f32 w = 0;  ///< Width.
    f32 h = 0;  ///< Height.
};

/// Returns where the caret for `byte_offset` is drawn.
///
/// An offset inside a cluster snaps to the cluster's start; an offset at or past the end of the text is the end of the
/// last line.
///
/// - Param layout: the layout.
/// - Param byte_offset: a byte offset into the text.
/// - Returns: the caret position.
/// - Fails: never.
CursorPosition PositionOfOffset(const TextLayout& layout, u32 byte_offset);

/// Returns the grapheme boundary nearest to the point `(x, y)`.
///
/// The half of a cluster the point lies in decides which side is chosen. A point above the text clamps to the first
/// line, one below to the last; left of a line is its start, right of it the end of its visible text (before trailing
/// spaces and the line break). Two boundaries at the same x (around a cluster with no width, such as a lone combining
/// mark) cannot both be reached by a click; the earlier one is chosen.
///
/// - Param layout: the layout.
/// - Param x: horizontal position in layout coordinates.
/// - Param y: vertical position in layout coordinates.
/// - Returns: a byte offset that is a grapheme boundary.
/// - Fails: never.
u32 OffsetAtPoint(const TextLayout& layout, f32 x, f32 y);

/// Returns the next grapheme boundary after `byte_offset` (the text length at the end).
///
/// - Param layout: the layout.
/// - Param byte_offset: a byte offset; one inside a cluster counts as the cluster's start.
/// - Returns: the boundary one cluster on.
/// - Fails: never.
u32 NextBoundary(const TextLayout& layout, u32 byte_offset);

/// Returns the grapheme boundary before `byte_offset` (0 at the start).
///
/// - Param layout: the layout.
/// - Param byte_offset: a byte offset; one inside a cluster counts as the cluster's end, so the result is that
/// cluster's start.
/// - Returns: the boundary one cluster back.
/// - Fails: never.
u32 PreviousBoundary(const TextLayout& layout, u32 byte_offset);

/// Returns the offset of the start of the line that holds `byte_offset`.
///
/// - Param layout: the layout.
/// - Param byte_offset: a byte offset.
/// - Returns: the line's first boundary.
/// - Fails: never.
u32 LineStart(const TextLayout& layout, u32 byte_offset);

/// Returns the offset where the visible text of the line that holds `byte_offset` ends.
///
/// That is before the line break, and before trailing spaces of a soft-wrapped line; a wrapped line without a trailing
/// space ends at the offset that starts the next line.
///
/// - Param layout: the layout.
/// - Param byte_offset: a byte offset.
/// - Returns: the line's end boundary.
/// - Fails: never.
u32 LineEnd(const TextLayout& layout, u32 byte_offset);

/// Returns the offset one line up at the horizontal position `preferred_x`; on the first line, 0.
///
/// A caller keeps `preferred_x` (the x of the caret when vertical movement began) so the column survives lines of
/// different length.
///
/// - Param layout: the layout.
/// - Param byte_offset: the current offset.
/// - Param preferred_x: the column to aim for, in layout coordinates.
/// - Returns: a grapheme boundary.
/// - Fails: never.
u32 LineAbove(const TextLayout& layout, u32 byte_offset, f32 preferred_x);

/// Returns the offset one line down at the horizontal position `preferred_x`; on the last line, the text length.
///
/// - Param layout: the layout.
/// - Param byte_offset: the current offset.
/// - Param preferred_x: the column to aim for, in layout coordinates.
/// - Returns: a grapheme boundary.
/// - Fails: never.
u32 LineBelow(const TextLayout& layout, u32 byte_offset, f32 preferred_x);

/// Collects the rectangles that cover the text between two offsets, one per line (adjacent clusters merge into one
/// span).
///
/// The result does not depend on which offset is `begin`. Both offsets snap to cluster starts. An empty selection, and
/// the empty part of a line (a blank line, the position where a line only starts), give no rectangle; a line break is
/// not painted.
///
/// - Param layout: the layout.
/// - Param begin: one end of the selection.
/// - Param end: the other end.
/// - Param out: receives the rectangles; cleared first.
/// - Ownership: `out` owns its elements.
/// - Fails: `OutOfMemory`; `out` is left empty.
Status SelectionRects(const TextLayout& layout, u32 begin, u32 end, Vector<SelectionRect>* out);

}  // namespace engine_placeholder::text
