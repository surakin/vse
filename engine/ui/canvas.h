#pragma once

#include "lang/scalar.h"

#include "text/layout.h"
#include "types.h"
#include "ui/geometry.h"

//! The canvas widgets draw on (Phase 15, Day 189): an abstract 2D drawing surface in logical units.
//!
//! A widget never builds vertices: it calls the operations of [[ui::Canvas]], and the implementation decides what that means. The implementation of the toolkit is
//! [[ui::DrawListCanvas]] (it appends to a [[ui::DrawList]] that the renderer draws); [[ui::RecordingCanvas]] records the calls for tests. A vector canvas, a software
//! rasteriser or an exporter can be added without touching a widget. No draw-list, GPU or RHI type appears in this header.
//!
//! **Units and snapping (the contract of every implementation).** Widgets work in logical units; `Scale()` physical pixels make one unit (1, 1.25, 2, ...).
//! - A rectangle is snapped to whole pixels by rounding its **edges**: `left = round(x * scale)`, `right = round((x + w) * scale)`, so two rectangles that share an edge in
//!   logical units share it in pixels, with no gap and no overlap. A non-empty rectangle is never thinner than one pixel.
//! - A border, a stroke width or any other thickness is scaled and then rounded to whole pixels, never below one pixel while it is above zero: 1 unit is 1 pixel at scale 1,
//!   2 pixels at scale 2, and 1 pixel at scale 1.25 (never 1.25).
//! - Corner radii and blur distances are scaled but not rounded; a radius larger than half of the shorter side is reduced to it.
//! - Lines and triangles have arbitrary angles: their points are scaled, not snapped.
//! - Text is laid out by the caller **in physical pixels** (`text::TextStyle::pixel_size` is the logical size times `Scale()`); its origin is logical and is scaled, and
//!   the glyphs then land on whole pixels as `renderer::TextRenderer` places them.
//! - A colour is packed RGBA with red in the lowest byte; a fully transparent colour draws nothing.

namespace engine_placeholder::ui {

/// Names an image the renderer can bind. 0 means "no image"; the meaning of other values belongs to the renderer.
using ImageId = u32;

/// The 2D drawing surface widgets draw on.
///
/// Drawing order is call order inside a layer; layers are drawn in the order of their numbers (see [[ui::Canvas::BeginLayer]]). Clips (see [[ui::Canvas::PushClip]]) apply to
/// everything drawn while they are on the stack.
///
/// Thread-safety: not-thread-safe; owner-thread only.
/// Ownership: owns nothing by itself; implementations document what they borrow. Not copyable.
class Canvas {
public:
    Canvas() = default;
    virtual ~Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    /// Returns the number of physical pixels in one logical unit.
    ///
    /// Fails: never.
    virtual f32 Scale() const = 0;

    /// Fills a rectangle.
    /// Param rect: the rectangle in logical units.
    /// Param color: packed RGBA.
    virtual void DrawRect(const Rect& rect, u32 color) = 0;
    /// Fills a rectangle with rounded corners.
    /// Param rect: the rectangle in logical units.
    /// Param radii: the radius of each corner in logical units.
    /// Param color: packed RGBA.
    virtual void DrawRoundedRect(const Rect& rect, const CornerRadii& radii, u32 color) = 0;
    /// Strokes the inside edge of a rounded rectangle (the fill, if any, is a separate call).
    /// Param rect: the outer rectangle in logical units.
    /// Param radii: the outer radius of each corner in logical units.
    /// Param width: the border thickness in logical units (rounded to whole pixels).
    /// Param color: packed RGBA.
    virtual void DrawBorder(const Rect& rect, const CornerRadii& radii, f32 width, u32 color) = 0;
    /// Draws a straight line with flat ends.
    /// Param a: the start point in logical units.
    /// Param b: the end point in logical units.
    /// Param width: the thickness in logical units (rounded to whole pixels).
    /// Param color: packed RGBA.
    virtual void DrawLine(Point a, Point b, f32 width, u32 color) = 0;
    /// Fills a triangle (arrows of combo boxes and menus).
    /// Param a: first corner in logical units.
    /// Param b: second corner.
    /// Param c: third corner.
    /// Param color: packed RGBA.
    virtual void DrawTriangle(Point a, Point b, Point c, u32 color) = 0;
    /// Draws an image stretched over a rectangle.
    /// Param rect: the rectangle in logical units.
    /// Param image: the image; 0 draws nothing.
    /// Param uv: the part of the image shown, in texture coordinates (`x`, `y` the top-left, `w`, `h` the size; 0 to 1 is the whole image).
    /// Param tint: packed RGBA multiplied into the image.
    virtual void DrawImage(const Rect& rect, ImageId image, const Rect& uv, u32 tint) = 0;
    /// Draws laid-out text.
    /// Param layout: the layout, made in physical pixels with `style`.
    /// Param style: the font and pixel size the layout was made with; its colour is ignored.
    /// Param origin: where the layout's top-left corner goes, in logical units.
    /// Param color: packed RGBA of the glyphs.
    /// Aborts: `style.font` is null or not open.
    virtual void DrawText(const text::TextLayout& layout, const text::TextStyle& style, Point origin, u32 color) = 0;
    /// Draws a soft shadow behind a rounded rectangle (popups): a quad larger than the rectangle with a blurred edge.
    /// Param rect: the rectangle that casts it, in logical units.
    /// Param radius: its corner radius in logical units.
    /// Param blur: how far the shadow fades out beyond the rectangle, in logical units.
    /// Param color: packed RGBA at the rectangle's edge.
    virtual void DrawShadow(const Rect& rect, f32 radius, f32 blur, u32 color) = 0;

    /// Restricts drawing to the intersection of the current clip and `rect`.
    /// Param rect: the clip in logical units, snapped like a rectangle.
    virtual void PushClip(const Rect& rect) = 0;
    /// Removes the clip pushed last.
    /// Aborts: there is none in the current layer.
    virtual void PopClip() = 0;
    /// Starts a layer: what is drawn until [[ui::Canvas::EndLayer]] comes above everything drawn in lower layers, whatever the order of the calls. Equal layers keep call
    /// order. A layer starts with no clip (the whole surface), so a popup is not cut by the clip of the widget that opened it; the clips of the outer layer return at the end.
    /// Param order: the layer number; 0 is the base layer.
    virtual void BeginLayer(i32 order) = 0;
    /// Ends the layer started last and restores the clips that were active before it.
    /// Aborts: no layer is open.
    virtual void EndLayer() = 0;

    /// Returns `logical` in physical pixels, scaled but not rounded (radii, blur).
    ///
    /// Fails: never.
    f32 ToPixels(f32 logical) const { return logical * Scale(); }
    /// Returns a thickness in whole physical pixels: scaled, rounded, and at least 1 while `logical` is above 0; 0 otherwise.
    ///
    /// Fails: never.
    f32 SnapLength(f32 logical) const {
        if (!(logical > 0)) return 0;
        const f32 px = Floor(logical * Scale() + 0.5f);
        return px < 1.0f ? 1.0f : px;
    }
    /// Returns `rect` in physical pixels with its edges rounded to whole pixels (see the contract above); an empty rectangle stays empty.
    ///
    /// Fails: never.
    Rect SnapRect(const Rect& rect) const {
        if (rect.Empty()) return Rect();
        const f32 s = Scale();
        const f32 x0 = Floor(rect.x * s + 0.5f);
        const f32 y0 = Floor(rect.y * s + 0.5f);
        f32 x1 = Floor((rect.x + rect.w) * s + 0.5f);
        f32 y1 = Floor((rect.y + rect.h) * s + 0.5f);
        if (x1 < x0 + 1.0f) x1 = x0 + 1.0f;
        if (y1 < y0 + 1.0f) y1 = y0 + 1.0f;
        Rect out;
        out.x = x0;
        out.y = y0;
        out.w = x1 - x0;
        out.h = y1 - y0;
        return out;
    }
};

} // namespace engine_placeholder::ui
