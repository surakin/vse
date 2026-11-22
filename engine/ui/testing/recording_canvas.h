#pragma once

#include "containers/vector.h"
#include "types.h"
#include "ui/canvas.h"

//! A canvas that records the calls instead of drawing them, so a widget test can say "this button drew a rounded rect
//! with the hover colour".

namespace vse::ui
{

/// What a recorded call was.
enum class CanvasCallKind : u8
{
    Rect,         ///< `DrawRect`.
    RoundedRect,  ///< `DrawRoundedRect`.
    Border,       ///< `DrawBorder`.
    Line,         ///< `DrawLine`.
    Triangle,     ///< `DrawTriangle`.
    Image,        ///< `DrawImage`.
    Text,         ///< `DrawText`.
    Shadow,       ///< `DrawShadow`.
};

/// One recorded drawing call, in the logical units the widget passed.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct CanvasCall
{
    CanvasCallKind kind = CanvasCallKind::Rect;  ///< Which operation.
    Rect rect;  ///< The rectangle (`DrawRect`, `DrawRoundedRect`, `DrawBorder`, `DrawImage`, `DrawShadow`); for text
                ///< the origin in `x`, `y`.
    CornerRadii radii;    ///< The corner radii; for a shadow `top_left` holds its radius.
    Point p0;             ///< `DrawLine`, `DrawTriangle`: first point.
    Point p1;             ///< Second point.
    Point p2;             ///< `DrawTriangle`: third point.
    f32 width = 0;        ///< `DrawBorder`, `DrawLine`: thickness; `DrawShadow`: blur.
    u32 color = 0;        ///< The colour or tint.
    ImageId image = 0;    ///< `DrawImage`: the image.
    Rect uv;              ///< `DrawImage`: the texture rectangle.
    u32 glyph_count = 0;  ///< `DrawText`: glyphs in the layout.
    u32 clip_depth = 0;   ///< Clips on the stack when the call was made (in the current layer).
    i32 layer = 0;        ///< The layer order the call was made in.
};

/// A [[ui::Canvas]] that appends a [[ui::CanvasCall]] per drawing call and draws nothing.
///
/// - Thread-safety: not-thread-safe; owner-thread only.
/// - Ownership: owns the recorded calls.
class RecordingCanvas final : public Canvas
{
public:
    /// Makes a recording canvas.
    /// - Param scale: the value `Scale()` returns.
    explicit RecordingCanvas(f32 scale = 1.0f) : scale_(scale) {}

    /// Returns the calls recorded so far, in call order.
    ///
    /// - Fails: never.
    const Vector<CanvasCall>& Calls() const { return calls_; }
    /// Forgets the recorded calls and resets the clip and layer state.
    ///
    /// - Fails: never.
    void Clear()
    {
        calls_.Clear();
        clip_depths_.Clear();
        layers_.Clear();
        clip_depth_ = 0;
    }

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

private:
    CanvasCall& Add(CanvasCallKind kind, u32 color);

    f32 scale_;
    Vector<CanvasCall> calls_;
    Vector<u32> clip_depths_;  // the clip depth to return to at the end of each open layer
    Vector<i32> layers_;
    u32 clip_depth_ = 0;
};

}  // namespace vse::ui
