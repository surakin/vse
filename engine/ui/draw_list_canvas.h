#pragma once

#include "containers/vector.h"
#include "text/glyph_atlas.h"
#include "types.h"
#include "ui/canvas.h"
#include "ui/draw_list.h"

//! The canvas that builds a [[ui::DrawList]] (Phase 15, Day 189).

namespace engine_placeholder::ui {

/// A [[ui::Canvas]] that converts logical units to physical pixels, snaps as the canvas contract says, and appends vertices, indices and commands to a draw list.
///
/// A frame is `list.Clear()`, `Begin(scale, width, height)`, any number of draws, then `list.SortLayers()`. Clips and layers are physical-pixel rectangles on stacks that
/// keep their capacity. A draw whose rectangle does not meet the current clip, or that is fully transparent, makes no vertex. Consecutive draws with the same clip, layer and
/// texture share a command; a draw that needs no texture joins any command.
///
/// Thread-safety: not-thread-safe; owner-thread only.
/// Ownership: borrows the draw list and the glyph atlas, which must outlive it.
class DrawListCanvas final : public Canvas {
public:
    /// Makes a canvas that appends to `list` and takes glyphs from `atlas`.
    /// Param list: the draw list to build; borrowed.
    /// Param atlas: the glyph atlas for text; borrowed; null makes every glyph count as dropped.
    /// Aborts: `list` is null.
    /// Ownership: the list and the atlas are borrowed.
    DrawListCanvas(DrawList* list, text::GlyphAtlas* atlas);

    /// Starts a frame: sets the scale and the surface size and resets the clip and layer stacks (the draw list is the caller's to `Clear`).
    /// Param scale: physical pixels per logical unit; must be above 0.
    /// Param width: the surface width in physical pixels.
    /// Param height: the surface height in physical pixels.
    /// Aborts: `scale` is not above 0.
    void Begin(f32 scale, u32 width, u32 height);

    /// Returns the scale given to `Begin`.
    f32 Scale() const override { return scale_; }
    void DrawRect(const Rect& rect, u32 color) override;
    void DrawRoundedRect(const Rect& rect, const CornerRadii& radii, u32 color) override;
    void DrawBorder(const Rect& rect, const CornerRadii& radii, f32 width, u32 color) override;
    void DrawLine(Point a, Point b, f32 width, u32 color) override;
    void DrawTriangle(Point a, Point b, Point c, u32 color) override;
    void DrawImage(const Rect& rect, ImageId image, const Rect& uv, u32 tint) override;
    void DrawText(const text::TextLayout& layout, const text::TextStyle& style, Point origin, u32 color) override;
    void DrawShadow(const Rect& rect, f32 radius, f32 blur, u32 color) override;
    void PushClip(const Rect& rect) override;
    void PopClip() override;
    void BeginLayer(i32 order) override;
    void EndLayer() override;

    /// Returns the current clip in physical pixels.
    ///
    /// Fails: never.
    PixelRect CurrentClip() const { return clips_[clips_.Size() - 1]; }

private:
    struct LayerFrame {
        i32 order = 0;
        usize clip_base = 0;
    };

    bool Admit(const PixelRect& bounds, u32 color, usize vertex_count) const;
    PixelRect BoundsOf(f32 x0, f32 y0, f32 x1, f32 y1) const;
    DrawCommand& CommandFor(TextureRef texture);
    void EmitQuad(const Vertex v[4], TextureRef texture);
    void EmitRectModeQuad(const Rect& px, const CornerRadii& radii_px, f32 border, u32 flags, u32 color, f32 grow, TextureRef texture);

    DrawList* list_;
    text::GlyphAtlas* atlas_;
    f32 scale_ = 1.0f;
    PixelRect surface_;
    Vector<PixelRect> clips_;
    Vector<LayerFrame> layers_;
};

} // namespace engine_placeholder::ui
