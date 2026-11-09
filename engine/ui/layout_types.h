#pragma once

#include "types.h"
#include "ui/geometry.h"

//! The vocabulary of layout (Phase 15, Day 193): constraints, alignment and the per-widget layout properties the containers read.

namespace engine_placeholder::ui {

/// A size limit that means "no limit".
inline constexpr f32 kUnbounded = 3.0e38f;

/// What a widget is asked to fit in when it is measured: a smallest and a largest size per axis, in logical units. A maximum of `kUnbounded` means "as large as you like".
///
/// Thread-safety: thread-compatible plain data.
struct Constraints {
    f32 min_w = 0;           ///< Smallest width.
    f32 max_w = kUnbounded;  ///< Largest width.
    f32 min_h = 0;           ///< Smallest height.
    f32 max_h = kUnbounded;  ///< Largest height.

    /// Returns `size` limited to these constraints (the maximum wins over the minimum if they cross).
    ///
    /// Fails: never.
    Size Clamp(Size size) const {
        Size out = size;
        if (out.w < min_w) out.w = min_w;
        if (out.h < min_h) out.h = min_h;
        if (out.w > max_w) out.w = max_w;
        if (out.h > max_h) out.h = max_h;
        return out;
    }
    /// Returns whether all four limits are equal.
    ///
    /// Fails: never.
    bool operator==(const Constraints& o) const { return min_w == o.min_w && max_w == o.max_w && min_h == o.min_h && max_h == o.max_h; }
};

/// How a widget sits in the space its container gives it along one axis.
enum class Align : u8 {
    Start,   ///< At the start (left or top) with its preferred size.
    Center,  ///< Centered with its preferred size.
    End,     ///< At the end (right or bottom) with its preferred size.
    Stretch, ///< Filling the space, within its minimum and maximum size.
};

/// How a [[ui::AnchorLayout]] places a child: the edges are fractions of the content box plus offsets in logical units. When a minimum equals its maximum on an axis, the child
/// has its preferred size there and `offset_min` moves its start edge.
///
/// Thread-safety: thread-compatible plain data.
struct AnchorSpec {
    f32 min_x = 0;       ///< Left edge as a fraction of the content width.
    f32 min_y = 0;       ///< Top edge as a fraction of the content height.
    f32 max_x = 0;       ///< Right edge as a fraction of the content width.
    f32 max_y = 0;       ///< Bottom edge as a fraction of the content height.
    f32 offset_left = 0;   ///< Added to the left edge.
    f32 offset_top = 0;    ///< Added to the top edge.
    f32 offset_right = 0;  ///< Added to the right edge.
    f32 offset_bottom = 0; ///< Added to the bottom edge.
};

/// What a widget tells its container about itself.
///
/// A width or height of -1 means "from the content" (`Widget::Measure`); any other value is the preferred size, which a container uses to size the widget when it is not
/// stretched (`Align::Stretch` fills the cell instead, so set `min` and `max` equal, or an alignment other than `Stretch`, for a fixed size). The size is always limited to `[min, max]`.
///
/// Thread-safety: thread-compatible plain data.
struct LayoutProps {
    Size min;                                  ///< Smallest size; the container never makes the widget smaller unless it overflows.
    Size max = Size{kUnbounded, kUnbounded};   ///< Largest size.
    f32 width = -1;                            ///< Preferred width, or -1 for the content's.
    f32 height = -1;                           ///< Preferred height, or -1 for the content's.
    f32 weight = 0;                            ///< Share of the free space along the main axis of a [[ui::BoxLayout]], or of the track in a [[ui::GridLayout]]; 0 takes none.
    Align align_x = Align::Stretch;            ///< Horizontal placement in the cell.
    Align align_y = Align::Stretch;            ///< Vertical placement in the cell.
    u32 column = 0;                            ///< [[ui::GridLayout]]: the first column.
    u32 row = 0;                               ///< [[ui::GridLayout]]: the first row.
    u32 column_span = 1;                       ///< [[ui::GridLayout]]: columns covered.
    u32 row_span = 1;                          ///< [[ui::GridLayout]]: rows covered.
    AnchorSpec anchor;                         ///< [[ui::AnchorLayout]]: the edges.
    bool fill_parent = false;                  ///< A plain parent gives this widget its whole content box (what makes a container follow the surface size).
};

} // namespace engine_placeholder::ui
