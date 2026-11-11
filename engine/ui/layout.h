#pragma once

#include "containers/vector.h"
#include "types.h"
#include "ui/layout_types.h"
#include "ui/widget.h"

//! The container widgets (Phase 15, Day 193): [[ui::BoxLayout]], [[ui::StackLayout]], [[ui::AnchorLayout]],
//! [[ui::GridLayout]] and [[ui::Spacer]].
//!
//! Layout is two steps per widget. `Measure(constraints)` says what size the widget wants from its content;
//! `Arrange(rect)` places it and its children. Everything is in logical units and fractional; the canvas snaps
//! rectangles to whole pixels when drawing, so neighbours share an edge at any scale. A child that is collapsed takes
//! no space; one that is only hidden keeps it. Children read their `LayoutProps` (weight, alignment, minimum and
//! maximum size, grid cell, anchors).

namespace engine_placeholder::ui
{

/// The direction a [[ui::BoxLayout]] lays its children along.
enum class Orientation : u8
{
    Horizontal,  ///< Left to right.
    Vertical,    ///< Top to bottom.
};

/// A container that gives every shown child a cell along one axis: preferred sizes first, then free space to the
/// children with a weight (up to their maximum), or, when there is too little room, each child shrinks towards its
/// minimum in proportion to how far above it is. Across the axis a child follows its alignment.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class BoxLayout : public Widget
{
public:
    /// Makes a box.
    /// - Param orientation: the main axis.
    /// - Param name: the widget name.
    explicit BoxLayout(Orientation orientation, StringView name = StringView());
    /// Sets the gap between children along the main axis; marks layout dirty.
    void SetSpacing(f32 spacing);
    /// Sets the space between the box's edge and its children; marks layout dirty.
    void SetPadding(const Insets& padding);
    /// Returns the orientation.
    Orientation GetOrientation() const { return orientation_; }
    /// Returns the size wanted: the children's preferred sizes along the axis plus spacing and padding, the largest
    /// child across it.
    Size Measure(const Constraints& constraints) override;
    /// Places the children as described in the class comment.
    void Arrange(const Rect& rect) override;

private:
    Orientation orientation_;
    f32 spacing_ = 0;
    Insets padding_;
};

/// A container that puts every child in the whole content box, one over the other (the last on top), each aligned by
/// its own properties.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class StackLayout : public Widget
{
public:
    /// Makes a stack.
    /// - Param name: the widget name.
    explicit StackLayout(StringView name = StringView());
    /// Sets the space between the edge and the children; marks layout dirty.
    void SetPadding(const Insets& padding);
    /// Returns the largest preferred size of the children plus the padding.
    Size Measure(const Constraints& constraints) override;
    /// Places each child in the content box.
    void Arrange(const Rect& rect) override;

private:
    Insets padding_;
};

/// A container that places each child by edges given as fractions of the content box plus offsets
/// (`LayoutProps::anchor`).
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class AnchorLayout : public Widget
{
public:
    /// Makes an anchor layout.
    /// - Param name: the widget name.
    explicit AnchorLayout(StringView name = StringView());
    /// Places each child by its anchors.
    void Arrange(const Rect& rect) override;
};

/// How a row or column of a [[ui::GridLayout]] gets its size.
///
/// - Thread-safety: thread-compatible plain data.
struct TrackSize
{
    /// The kinds of track.
    enum class Kind : u8
    {
        Fixed,   ///< A given size.
        Auto,    ///< The largest preferred size of the single-cell children in the track.
        Weight,  ///< Its preferred size plus a share of the free space (after every track's preferred size) in
                 ///< proportion to `value`.
    };
    Kind kind = Kind::Auto;  ///< How the track is sized.
    f32 value = 0;           ///< The size (`Fixed`) or the weight (`Weight`).

    /// Returns a fixed track.
    /// - Param size: its size in logical units.
    static TrackSize Fixed(f32 size) { return TrackSize{Kind::Fixed, size}; }
    /// Returns an auto track.
    static TrackSize Auto() { return TrackSize{Kind::Auto, 0}; }
    /// Returns a weighted track.
    /// - Param weight: its share of the free space.
    static TrackSize Weight(f32 weight) { return TrackSize{Kind::Weight, weight}; }
};

/// A container with rows and columns. A child names its cell (`column`, `row`) and span in its `LayoutProps`; tracks
/// that were not declared are `Auto`. Children that span several tracks do not take part in sizing an `Auto` track:
/// they fill the cells they cover.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its children.
class GridLayout : public Widget
{
public:
    /// Makes a grid.
    /// - Param name: the widget name.
    explicit GridLayout(StringView name = StringView());
    /// Declares the next column; marks layout dirty.
    void AddColumn(TrackSize size);
    /// Declares the next row; marks layout dirty.
    void AddRow(TrackSize size);
    /// Sets the gaps between columns and between rows; marks layout dirty.
    void SetSpacing(f32 column_spacing, f32 row_spacing);
    /// Sets the space between the edge and the cells; marks layout dirty.
    void SetPadding(const Insets& padding);
    /// Returns the sum of the track sizes (weights at their preferred size), spacing and padding.
    Size Measure(const Constraints& constraints) override;
    /// Sizes the tracks and places each child in its cells.
    void Arrange(const Rect& rect) override;

private:
    struct Solved
    {
        Vector<f32> columns;
        Vector<f32> rows;
    };
    void Solve(f32 avail_w, f32 avail_h, Solved* out);
    u32 ColumnCount() const;
    u32 RowCount() const;
    Vector<TrackSize> columns_;
    Vector<TrackSize> rows_;
    f32 column_spacing_ = 0;
    f32 row_spacing_ = 0;
    Insets padding_;
};

/// An empty widget with weight 1: it takes the free space in a [[ui::BoxLayout]] and draws nothing.
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns nothing by itself.
class Spacer : public Widget
{
public:
    /// Makes a spacer.
    /// - Param weight: its share of the free space.
    explicit Spacer(f32 weight = 1.0f);
};

}  // namespace engine_placeholder::ui
