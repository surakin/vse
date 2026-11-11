#include "ui/testing/recording_canvas.h"

#include "log/assert.h"

namespace engine_placeholder::ui
{

CanvasCall& RecordingCanvas::Add(CanvasCallKind kind, u32 color)
{
    CanvasCall call;
    call.kind = kind;
    call.color = color;
    call.clip_depth = clip_depth_;
    call.layer = layers_.Empty() ? 0 : layers_[layers_.Size() - 1];
    calls_.Push(call);
    return calls_[calls_.Size() - 1];
}

void RecordingCanvas::DrawRect(const Rect& rect, u32 color)
{
    Add(CanvasCallKind::Rect, color).rect = rect;
}

void RecordingCanvas::DrawRoundedRect(const Rect& rect, const CornerRadii& radii, u32 color)
{
    CanvasCall& c = Add(CanvasCallKind::RoundedRect, color);
    c.rect = rect;
    c.radii = radii;
}

void RecordingCanvas::DrawBorder(const Rect& rect, const CornerRadii& radii, f32 width, u32 color)
{
    CanvasCall& c = Add(CanvasCallKind::Border, color);
    c.rect = rect;
    c.radii = radii;
    c.width = width;
}

void RecordingCanvas::DrawLine(Point a, Point b, f32 width, u32 color)
{
    CanvasCall& c = Add(CanvasCallKind::Line, color);
    c.p0 = a;
    c.p1 = b;
    c.width = width;
}

void RecordingCanvas::DrawTriangle(Point a, Point b, Point c, u32 color)
{
    CanvasCall& call = Add(CanvasCallKind::Triangle, color);
    call.p0 = a;
    call.p1 = b;
    call.p2 = c;
}

void RecordingCanvas::DrawImage(const Rect& rect, ImageId image, const Rect& uv, u32 tint)
{
    CanvasCall& c = Add(CanvasCallKind::Image, tint);
    c.rect = rect;
    c.image = image;
    c.uv = uv;
}

void RecordingCanvas::DrawText(const text::TextLayout& layout, const text::TextStyle& style, Point origin, u32 color)
{
    ENGINE_ASSERT(style.font != nullptr && style.font->IsOpen(),
                  "RecordingCanvas::DrawText: the style has no open font");
    CanvasCall& c = Add(CanvasCallKind::Text, color);
    c.rect.x = origin.x;
    c.rect.y = origin.y;
    c.glyph_count = static_cast<u32>(layout.glyphs.Size());
}

void RecordingCanvas::DrawShadow(const Rect& rect, f32 radius, f32 blur, u32 color)
{
    CanvasCall& c = Add(CanvasCallKind::Shadow, color);
    c.rect = rect;
    c.radii.top_left = radius;
    c.width = blur;
}

void RecordingCanvas::PushClip(const Rect&)
{
    ++clip_depth_;
}

void RecordingCanvas::PopClip()
{
    ENGINE_ASSERT(clip_depth_ > 0, "RecordingCanvas::PopClip: no clip was pushed in this layer");
    --clip_depth_;
}

void RecordingCanvas::BeginLayer(i32 order)
{
    clip_depths_.Push(clip_depth_);
    layers_.Push(order);
    clip_depth_ = 0;  // a layer starts unclipped
}

void RecordingCanvas::EndLayer()
{
    ENGINE_ASSERT(!layers_.Empty(), "RecordingCanvas::EndLayer: no layer is open");
    clip_depth_ = clip_depths_[clip_depths_.Size() - 1];
    clip_depths_.Pop();
    layers_.Pop();
}

}  // namespace engine_placeholder::ui
