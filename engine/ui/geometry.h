#pragma once

#include "types.h"

//! The plain geometry types of the UI toolkit: points, rectangles and corner radii in logical units, and the whole-pixel rectangle the draw list clips with.

namespace engine_placeholder::ui {

/// A point.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Point {
    f32 x = 0; ///< Horizontal position, growing to the right.
    f32 y = 0; ///< Vertical position, growing downwards.
};

/// An axis-aligned rectangle, origin at its top-left corner. In logical units unless a function says it is in pixels.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Rect {
    f32 x = 0; ///< Left edge.
    f32 y = 0; ///< Top edge.
    f32 w = 0; ///< Width; a rectangle with `w <= 0` or `h <= 0` is empty.
    f32 h = 0; ///< Height.

    /// Returns whether the rectangle covers no area.
    ///
    /// - Fails: never.
    bool Empty() const { return !(w > 0) || !(h > 0); }
    /// Returns the right edge, `x + w`.
    ///
    /// - Fails: never.
    f32 Right() const { return x + w; }
    /// Returns the bottom edge, `y + h`.
    ///
    /// - Fails: never.
    f32 Bottom() const { return y + h; }
    /// Returns whether `p` lies inside the rectangle (the left and top edges count, the right and bottom edges do not).
    ///
    /// - Fails: never.
    bool Contains(const Point& p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};

/// Corner radii of a rounded rectangle.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct CornerRadii {
    f32 top_left = 0;     ///< Radius of the top-left corner.
    f32 top_right = 0;    ///< Radius of the top-right corner.
    f32 bottom_right = 0; ///< Radius of the bottom-right corner.
    f32 bottom_left = 0;  ///< Radius of the bottom-left corner.

    /// Makes all four radii zero.
    CornerRadii() = default;
    /// Makes all four radii `radius`.
    /// - Param radius: the radius of every corner.
    CornerRadii(f32 radius) : top_left(radius), top_right(radius), bottom_right(radius), bottom_left(radius) {}
    /// Makes the four radii separately.
    /// - Param tl: top-left radius.
    /// - Param tr: top-right radius.
    /// - Param br: bottom-right radius.
    /// - Param bl: bottom-left radius.
    CornerRadii(f32 tl, f32 tr, f32 br, f32 bl) : top_left(tl), top_right(tr), bottom_right(br), bottom_left(bl) {}
    /// Returns whether every radius is zero.
    ///
    /// - Fails: never.
    bool AllZero() const { return !(top_left > 0) && !(top_right > 0) && !(bottom_right > 0) && !(bottom_left > 0); }
};

/// A width and a height in logical units.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Size {
    f32 w = 0; ///< Width.
    f32 h = 0; ///< Height.
};

/// Distances from the four edges of a rectangle inwards (padding, margins).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Insets {
    f32 left = 0;   ///< Left distance.
    f32 top = 0;    ///< Top distance.
    f32 right = 0;  ///< Right distance.
    f32 bottom = 0; ///< Bottom distance.

    /// Makes zero insets.
    Insets() = default;
    /// Makes the same distance on all four sides.
    /// - Param all: the distance.
    Insets(f32 all) : left(all), top(all), right(all), bottom(all) {}
    /// Makes insets from the four distances.
    /// - Param l: left.
    /// - Param t: top.
    /// - Param r: right.
    /// - Param b: bottom.
    Insets(f32 l, f32 t, f32 r, f32 b) : left(l), top(t), right(r), bottom(b) {}
    /// Returns `left + right`.
    ///
    /// - Fails: never.
    f32 Horizontal() const { return left + right; }
    /// Returns `top + bottom`.
    ///
    /// - Fails: never.
    f32 Vertical() const { return top + bottom; }
};

/// A rectangle of whole physical pixels: a clip rectangle or a surface size.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct PixelRect {
    i32 x = 0; ///< Left column.
    i32 y = 0; ///< Top row.
    i32 w = 0; ///< Width in pixels; the rectangle is empty when `w <= 0` or `h <= 0`.
    i32 h = 0; ///< Height in pixels.

    /// Returns whether the rectangle covers no pixel.
    ///
    /// - Fails: never.
    bool Empty() const { return w <= 0 || h <= 0; }
    /// Returns the part of this rectangle that lies inside `other` (empty when they do not overlap).
    ///
    /// - Fails: never.
    PixelRect Intersect(const PixelRect& other) const {
        const i32 x0 = x > other.x ? x : other.x;
        const i32 y0 = y > other.y ? y : other.y;
        const i32 x1 = (x + w) < (other.x + other.w) ? (x + w) : (other.x + other.w);
        const i32 y1 = (y + h) < (other.y + other.h) ? (y + h) : (other.y + other.h);
        PixelRect r;
        r.x = x0;
        r.y = y0;
        r.w = x1 > x0 ? x1 - x0 : 0;
        r.h = y1 > y0 ? y1 - y0 : 0;
        return r;
    }
    /// Returns whether two rectangles have the same position and size.
    ///
    /// - Fails: never.
    bool operator==(const PixelRect& other) const { return x == other.x && y == other.y && w == other.w && h == other.h; }
};

} // namespace engine_placeholder::ui
