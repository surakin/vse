#include "ui/draw_list_canvas.h"

#include "lang/scalar.h"

#include "algorithm.h"
#include "log/assert.h"

namespace vse::ui
{

namespace
{

constexpr u32 kAlphaMask = 0xFF000000u;

// A float coordinate as a pixel number: a scrolled-away or broken (NaN, infinite) value is pinned to a range the
// integer conversion defines.
i32 ToPixelInt(f32 v)
{
    constexpr f32 kLimit = 1.0e9f;
    if (!(v == v))
    {
        return 0;
    }
    return static_cast<i32>(Clamp<f32>(v, -kLimit, kLimit));
}

}  // namespace

DrawListCanvas::DrawListCanvas(DrawList* list, text::GlyphAtlas* atlas) : list_(list), atlas_(atlas)
{
    ENGINE_ASSERT(list != nullptr, "DrawListCanvas: the draw list is null");
    clips_.Push(surface_);
}

void DrawListCanvas::Rebind(DrawList* list, text::GlyphAtlas* atlas)
{
    ENGINE_ASSERT(list != nullptr, "DrawListCanvas::Rebind: the draw list is null");
    list_ = list;
    atlas_ = atlas;
}

void DrawListCanvas::Begin(f32 scale, u32 width, u32 height)
{
    ENGINE_ASSERT(scale > 0, "DrawListCanvas::Begin: the scale must be above 0");
    scale_ = scale;
    surface_ = PixelRect();
    surface_.w = static_cast<i32>(width);
    surface_.h = static_cast<i32>(height);
    clips_.Clear();
    clips_.Push(surface_);
    layers_.Clear();
}

PixelRect DrawListCanvas::BoundsOf(f32 x0, f32 y0, f32 x1, f32 y1) const
{
    PixelRect r;
    r.x = ToPixelInt(Floor(x0));
    r.y = ToPixelInt(Floor(y0));
    r.w = ToPixelInt(Ceil(x1)) - r.x;
    r.h = ToPixelInt(Ceil(y1)) - r.y;
    return r;
}

bool DrawListCanvas::Admit(const PixelRect& bounds, u32 color, usize vertex_count) const
{
    if ((color & kAlphaMask) == 0 || bounds.Empty() || bounds.Intersect(CurrentClip()).Empty())
    {
        ++list_->stats.culled;
        return false;
    }
    if (list_->vertices.Size() + vertex_count > list_->max_vertices)
    {
        ++list_->stats.overflowed;
        return false;
    }
    return true;
}

DrawCommand& DrawListCanvas::CommandFor(TextureRef texture)
{
    const i32 layer = layers_.Empty() ? 0 : layers_[layers_.Size() - 1].order;
    const PixelRect clip = CurrentClip();
    if (!list_->commands.Empty())
    {
        DrawCommand& last = list_->commands[list_->commands.Size() - 1];
        const bool same_texture = texture.kind == TextureKind::None || last.texture.kind == TextureKind::None ||
                                  last.texture == texture;
        if (last.layer == layer && last.clip == clip && same_texture)
        {
            if (last.texture.kind == TextureKind::None)
            {
                last.texture = texture;
            }
            return last;
        }
    }
    DrawCommand command;
    command.first_index = static_cast<u32>(list_->indices.Size());
    command.clip = clip;
    command.texture = texture;
    command.layer = layer;
    list_->commands.Push(command);
    return list_->commands[list_->commands.Size() - 1];
}

void DrawListCanvas::EmitQuad(const Vertex v[4], TextureRef texture)
{
    DrawCommand& command = CommandFor(texture);
    const u32 base = static_cast<u32>(list_->vertices.Size());
    for (int i = 0; i < 4; ++i)
    {
        list_->vertices.Push(v[i]);
    }
    // Corners in the order top-left, top-right, bottom-left, bottom-right; triangles (tl, bl, tr) and (tr, bl, br), as
    // the text renderer winds its glyphs.
    const u32 order[6] = {0, 2, 1, 1, 2, 3};
    for (int i = 0; i < 6; ++i)
    {
        list_->indices.Push(base + order[i]);
    }
    command.index_count += 6;
}

// A rectangle-mode quad over `px` (physical, whole pixels) grown by `grow` pixels on every side; the box the shader
// measures against stays `px`.
void DrawListCanvas::EmitRectModeQuad(const Rect& px, const CornerRadii& radii_px, f32 border, u32 flags, u32 color,
                                      f32 grow, TextureRef texture)
{
    const f32 hw = px.w * 0.5f;
    const f32 hh = px.h * 0.5f;
    const f32 cx = px.x + hw;
    const f32 cy = px.y + hh;
    const f32 xs[4] = {-hw - grow, hw + grow, -hw - grow, hw + grow};
    const f32 ys[4] = {-hh - grow, -hh - grow, hh + grow, hh + grow};
    const f32 radius[4] = {radii_px.top_left, radii_px.top_right, radii_px.bottom_left, radii_px.bottom_right};
    Vertex v[4];
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = cx + xs[i];
        v[i].y = cy + ys[i];
        v[i].u = xs[i];
        v[i].v = ys[i];
        v[i].color = color;
        v[i].half_w = hw;
        v[i].half_h = hh;
        v[i].radius = radius[i];
        v[i].border = border;
        v[i].flags = flags;
    }
    EmitQuad(v, texture);
}

void DrawListCanvas::DrawRect(const Rect& rect, u32 color)
{
    const Rect px = SnapRect(rect);
    if (!Admit(BoundsOf(px.x, px.y, px.Right(), px.Bottom()), color, 4))
    {
        return;
    }
    Vertex v[4];
    const f32 xs[4] = {px.x, px.Right(), px.x, px.Right()};
    const f32 ys[4] = {px.y, px.y, px.Bottom(), px.Bottom()};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].color = color;
        v[i].flags = kModeSolid;
    }
    EmitQuad(v, TextureRef());
}

namespace
{

CornerRadii ScaleRadii(const CornerRadii& r, f32 scale, const Rect& px)
{
    const f32 limit = Min<f32>(px.w, px.h) * 0.5f;
    CornerRadii out;
    out.top_left = Min<f32>(Max<f32>(r.top_left * scale, 0.0f), limit);
    out.top_right = Min<f32>(Max<f32>(r.top_right * scale, 0.0f), limit);
    out.bottom_right = Min<f32>(Max<f32>(r.bottom_right * scale, 0.0f), limit);
    out.bottom_left = Min<f32>(Max<f32>(r.bottom_left * scale, 0.0f), limit);
    return out;
}

}  // namespace

void DrawListCanvas::DrawRoundedRect(const Rect& rect, const CornerRadii& radii, u32 color)
{
    if (radii.AllZero())
    {
        DrawRect(rect, color);
        return;
    }
    const Rect px = SnapRect(rect);
    if (!Admit(BoundsOf(px.x, px.y, px.Right(), px.Bottom()), color, 4))
    {
        return;
    }
    EmitRectModeQuad(px, ScaleRadii(radii, scale_, px), 0.0f, kModeRect, color, 0.0f, TextureRef());
}

void DrawListCanvas::DrawBorder(const Rect& rect, const CornerRadii& radii, f32 width, u32 color)
{
    const Rect px = SnapRect(rect);
    const f32 w = Min<f32>(SnapLength(width), Min<f32>(px.w, px.h) * 0.5f);
    if (!(w > 0) || !Admit(BoundsOf(px.x, px.y, px.Right(), px.Bottom()), color, 4))
    {
        if (!(w > 0))
        {
            ++list_->stats.culled;
        }
        return;
    }
    EmitRectModeQuad(px, ScaleRadii(radii, scale_, px), w, kModeRect | kFlagRing, color, 0.0f, TextureRef());
}

void DrawListCanvas::DrawShadow(const Rect& rect, f32 radius, f32 blur, u32 color)
{
    const Rect px = SnapRect(rect);
    const f32 blur_px = Max<f32>(ToPixels(blur), 0.0f);
    if (px.Empty() || !(blur_px > 0))
    {
        ++list_->stats.culled;
        return;
    }
    if (!Admit(BoundsOf(px.x - blur_px, px.y - blur_px, px.Right() + blur_px, px.Bottom() + blur_px), color, 4))
    {
        return;
    }
    EmitRectModeQuad(px, ScaleRadii(CornerRadii(radius), scale_, px), blur_px, kModeRect | kFlagShadow, color, blur_px,
                     TextureRef());
}

void DrawListCanvas::DrawLine(Point a, Point b, f32 width, u32 color)
{
    const f32 ax = a.x * scale_;
    const f32 ay = a.y * scale_;
    const f32 dx = b.x * scale_ - ax;
    const f32 dy = b.y * scale_ - ay;
    const f32 length = Sqrt(dx * dx + dy * dy);
    const f32 w = SnapLength(width);
    if (!(length > 0) || !(w > 0))
    {
        ++list_->stats.culled;
        return;
    }
    // The line is a box of length x w centred between the points, turned to their direction; the shader sees it as an
    // unrounded box.
    const f32 ux = dx / length;
    const f32 uy = dy / length;
    const f32 nx = -uy * w * 0.5f;
    const f32 ny = ux * w * 0.5f;
    const f32 bx = b.x * scale_;
    const f32 by = b.y * scale_;
    const f32 px[4] = {ax + nx, bx + nx, ax - nx,
                       bx - nx};  // top-left, top-right, bottom-left, bottom-right of the box
    const f32 py[4] = {ay + ny, by + ny, ay - ny, by - ny};
    f32 minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for (int i = 1; i < 4; ++i)
    {
        minx = Min<f32>(minx, px[i]);
        maxx = Max<f32>(maxx, px[i]);
        miny = Min<f32>(miny, py[i]);
        maxy = Max<f32>(maxy, py[i]);
    }
    if (!Admit(BoundsOf(minx, miny, maxx, maxy), color, 4))
    {
        return;
    }
    const f32 hw = length * 0.5f;
    const f32 hh = w * 0.5f;
    const f32 us[4] = {-hw, hw, -hw, hw};
    const f32 vs[4] = {-hh, -hh, hh, hh};
    Vertex v[4];
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = px[i];
        v[i].y = py[i];
        v[i].u = us[i];
        v[i].v = vs[i];
        v[i].color = color;
        v[i].half_w = hw;
        v[i].half_h = hh;
        v[i].flags = kModeRect;
    }
    EmitQuad(v, TextureRef());
}

void DrawListCanvas::DrawTriangle(Point a, Point b, Point c, u32 color)
{
    const f32 xs[3] = {a.x * scale_, b.x * scale_, c.x * scale_};
    const f32 ys[3] = {a.y * scale_, b.y * scale_, c.y * scale_};
    const f32 minx = Min<f32>(xs[0], Min<f32>(xs[1], xs[2]));
    const f32 maxx = Max<f32>(xs[0], Max<f32>(xs[1], xs[2]));
    const f32 miny = Min<f32>(ys[0], Min<f32>(ys[1], ys[2]));
    const f32 maxy = Max<f32>(ys[0], Max<f32>(ys[1], ys[2]));
    if (!Admit(BoundsOf(minx, miny, maxx, maxy), color, 3))
    {
        return;
    }
    DrawCommand& command = CommandFor(TextureRef());
    const u32 base = static_cast<u32>(list_->vertices.Size());
    for (int i = 0; i < 3; ++i)
    {
        Vertex v;
        v.x = xs[i];
        v.y = ys[i];
        v.color = color;
        v.flags = kModeSolid;
        list_->vertices.Push(v);
        list_->indices.Push(base + static_cast<u32>(i));
    }
    command.index_count += 3;
}

void DrawListCanvas::DrawImage(const Rect& rect, ImageId image, const Rect& uv, u32 tint)
{
    const Rect px = SnapRect(rect);
    if (image == 0 || !Admit(BoundsOf(px.x, px.y, px.Right(), px.Bottom()), tint, 4))
    {
        if (image == 0)
        {
            ++list_->stats.culled;
        }
        return;
    }
    const f32 xs[4] = {px.x, px.Right(), px.x, px.Right()};
    const f32 ys[4] = {px.y, px.y, px.Bottom(), px.Bottom()};
    const f32 us[4] = {uv.x, uv.Right(), uv.x, uv.Right()};
    const f32 vs[4] = {uv.y, uv.y, uv.Bottom(), uv.Bottom()};
    Vertex v[4];
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].u = us[i];
        v[i].v = vs[i];
        v[i].color = tint;
        v[i].flags = kModeImage;
    }
    TextureRef texture;
    texture.kind = TextureKind::Image;
    texture.id = image;
    EmitQuad(v, texture);
}

void DrawListCanvas::DrawText(const text::TextLayout& layout, const text::TextStyle& style, Point origin, u32 color)
{
    ENGINE_ASSERT(style.font != nullptr && style.font->IsOpen(),
                  "DrawListCanvas::DrawText: the style has no open font");
    if ((color & kAlphaMask) == 0)
    {
        ++list_->stats.culled;
        return;
    }
    // The same rule as renderer::TextRenderer: above the atlas maximum, glyphs are rasterised at the maximum and their
    // quads scaled up.
    const bool large = style.pixel_size > text::kMaxAtlasPixelSize;
    const u32 raster_size = large ? text::kMaxAtlasPixelSize : style.pixel_size;
    const f32 glyph_scale = large ? static_cast<f32>(style.pixel_size) / static_cast<f32>(text::kMaxAtlasPixelSize)
                                  : 1.0f;
    if (atlas_ == nullptr || !style.font->SetPixelSize(raster_size).Ok())
    {
        list_->stats.glyphs_dropped += layout.glyphs.Size();
        return;
    }
    const f32 ox = origin.x * scale_;
    const f32 oy = origin.y * scale_;
    for (usize i = 0; i < layout.glyphs.Size(); ++i)
    {
        const text::PositionedGlyph& g = layout.glyphs[i];
        text::GlyphEntry entry;
        if (!atlas_->GetOrAdd(*style.font, g.glyph, &entry).Ok() || !entry.valid)
        {
            ++list_->stats.glyphs_dropped;
            continue;
        }
        if (!entry.has_pixels)
        {
            continue;
        }
        const text::GlyphQuad q = text::PlaceGlyph(ox, oy, g.x, g.y, entry, glyph_scale);
        if (!Admit(BoundsOf(q.left, q.top, q.right, q.bottom), color, 4))
        {
            continue;
        }
        if (large)
        {
            ++list_->stats.scaled_large_glyphs;
        }
        const f32 xs[4] = {q.left, q.right, q.left, q.right};
        const f32 ys[4] = {q.top, q.top, q.bottom, q.bottom};
        const f32 us[4] = {entry.u0, entry.u1, entry.u0, entry.u1};
        const f32 vs[4] = {entry.v0, entry.v0, entry.v1, entry.v1};
        Vertex v[4];
        for (int k = 0; k < 4; ++k)
        {
            v[k].x = xs[k];
            v[k].y = ys[k];
            v[k].u = us[k];
            v[k].v = vs[k];
            v[k].color = color;
            v[k].flags = kModeGlyph;
        }
        TextureRef texture;
        texture.kind = TextureKind::Atlas;
        texture.id = entry.page;
        EmitQuad(v, texture);
    }
}

void DrawListCanvas::PushClip(const Rect& rect)
{
    const Rect px = SnapRect(rect);
    PixelRect clip;
    clip.x = ToPixelInt(px.x);
    clip.y = ToPixelInt(px.y);
    clip.w = ToPixelInt(px.Right()) -
             clip.x;  // the edges are pinned, not the sizes, so a huge rectangle keeps its far edge
    clip.h = ToPixelInt(px.Bottom()) - clip.y;
    clips_.Push(clip.Intersect(CurrentClip()));
}

void DrawListCanvas::PopClip()
{
    const usize base = layers_.Empty() ? 0 : layers_[layers_.Size() - 1].clip_base;
    ENGINE_ASSERT(clips_.Size() > base + 1, "DrawListCanvas::PopClip: no clip was pushed in this layer");
    clips_.Pop();
}

void DrawListCanvas::BeginLayer(i32 order)
{
    LayerFrame frame;
    frame.order = order;
    frame.clip_base = clips_.Size();
    layers_.Push(frame);
    clips_.Push(surface_);  // a layer starts unclipped
}

void DrawListCanvas::EndLayer()
{
    ENGINE_ASSERT(!layers_.Empty(), "DrawListCanvas::EndLayer: no layer is open");
    const usize base = layers_[layers_.Size() - 1].clip_base;
    while (clips_.Size() > base)
    {
        clips_.Pop();
    }
    layers_.Pop();
}

}  // namespace vse::ui
