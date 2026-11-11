#pragma once

#include "containers/vector.h"
#include "types.h"

//! A shelf packer for glyph atlas pages (Phase 14): rectangles go left to right on shelves, a new shelf opens below
//! when none fits.
namespace engine_placeholder::text
{

/// A rectangle of pixels in a page.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct Rect
{
    u32 x = 0;  ///< Left column.
    u32 y = 0;  ///< Top row.
    u32 w = 0;  ///< Width in pixels.
    u32 h = 0;  ///< Height in pixels.
};

/// Places rectangles in a fixed-size page without overlap, with `padding` empty pixels around every rectangle and the
/// page edge.
///
/// Shelves have a height class (rounded up to a multiple of 4); a rectangle goes on the shelf of the smallest class
/// that fits it and has room, otherwise a new shelf opens. The result depends only on the sequence of requests. After
/// construction the packer allocates only to grow its list of shelves.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: owns its shelf list.
class ShelfPacker
{
public:
    /// Creates a packer for a page of `width` x `height` pixels.
    ///
    /// - Param width: page width in pixels.
    /// - Param height: page height in pixels.
    /// - Param padding: empty pixels kept around each rectangle.
    ShelfPacker(u32 width, u32 height, u32 padding);

    /// Places a `w` x `h` rectangle.
    ///
    /// - Param w: width in pixels, at least 1.
    /// - Param h: height in pixels, at least 1.
    /// - Param out: receives the position; unchanged on failure.
    /// - Returns: true when it fit.
    /// - Ownership: nothing is retained.
    /// - Fails: returns false, placing nothing, when the rectangle with its padding is larger than the page or no shelf
    /// has room.
    bool Place(u32 w, u32 h, Rect* out);

    /// Returns the area in pixels of all rectangles placed (padding not counted).
    ///
    /// - Fails: never.
    u64 UsedArea() const { return used_area_; }
    /// Returns the page width.
    ///
    /// - Fails: never.
    u32 Width() const { return width_; }
    /// Returns the page height.
    ///
    /// - Fails: never.
    u32 Height() const { return height_; }

private:
    struct Shelf
    {
        u32 y = 0;       ///< Top row of the shelf.
        u32 height = 0;  ///< Height class of the shelf.
        u32 x = 0;       ///< First free column.
    };
    u32 width_;
    u32 height_;
    u32 padding_;
    u32 next_y_;
    u64 used_area_ = 0;
    Vector<Shelf> shelves_;
};

}  // namespace engine_placeholder::text
