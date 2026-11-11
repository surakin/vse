#include "ui/text_block.h"

#include "lang/scalar.h"

#include "ui/surface.h"

namespace engine_placeholder::ui
{

TextBlock::TextBlock(StringView name, StringView text) : Widget(name)
{
    run_.SetText(text);
}

void TextBlock::SetText(StringView text)
{
    if (run_.SetText(text))
    {
        MarkDirty(kLayoutDirty);
    }
}

void TextBlock::SetFont(FontRef font)
{
    font_ = font;
    font_set_ = true;
    MarkDirty(kLayoutDirty);
}

void TextBlock::SetColor(u32 abgr)
{
    color_ = abgr;
    color_set_ = true;
    MarkDirty(kDrawDirty);
}

void TextBlock::SetWrap(bool wrap)
{
    if (wrap_ == wrap)
    {
        return;
    }
    wrap_ = wrap;
    MarkDirty(kLayoutDirty);
}

void TextBlock::SetAlign(text::Align align)
{
    if (align_ == align)
    {
        return;
    }
    align_ = align;
    run_.SetAlign(align);
    MarkDirty(kLayoutDirty);
}

void TextBlock::SetEllipsis(bool ellipsis)
{
    if (ellipsis_ == ellipsis)
    {
        return;
    }
    ellipsis_ = ellipsis;
    MarkDirty(kDrawDirty);
}

void TextBlock::SetLook(WidgetKind kind, VariantId variant)
{
    kind_ = kind;
    variant_ = variant;
    MarkDirty(kLayoutDirty);
}

// Lays the text out for `wrap_width` logical units (kUnbounded or a non-wrapping block: no wrapping). Returns false
// when there is no font.
bool TextBlock::EnsureLayout(f32 wrap_width)
{
    Surface* surface = GetSurface();
    if (surface == nullptr || surface->GetFontProvider() == nullptr)
    {
        return false;
    }
    const f32 scale = surface->Scale();
    FontRef font;
    if (font_set_)
    {
        font = font_;
    }
    else if (kind_ != 0)
    {
        font = surface->Themes().Resolve(kind_, variant_, State(), scale).font;
        font.size /= scale;
    }
    else
    {
        font = surface->Themes().Font(FontId::Body, 1.0f);
    }
    text::Font* face = surface->GetFontProvider()->Resolve(font.family);
    if (face == nullptr)
    {
        return false;
    }
    f32 size_px = Floor(font.size * scale + 0.5f);
    if (size_px < 1)
    {
        size_px = 1;
    }
    f32 wrap_px = (wrap_ && wrap_width < kUnbounded) ? Floor(wrap_width * scale) : 0.0f;
    if (wrap_ && wrap_width < kUnbounded && wrap_px < 1)
    {
        wrap_px = 1;  // 0 would mean "no wrapping" to the text layout
    }
    return run_.Ensure(face, font.family, size_px, wrap_px, scale);
}

Size TextBlock::Measure(const Constraints& constraints)
{
    if (!EnsureLayout(constraints.max_w))
    {
        return Size();
    }
    return Size{run_.Width(), run_.Height()};
}

void TextBlock::Draw(Canvas& canvas)
{
    if (!EnsureLayout(Bounds().w))
    {
        return;
    }
    Surface* surface = GetSurface();
    u32 color;
    if (color_set_)
    {
        color = color_;
    }
    else if (kind_ != 0)
    {
        color = surface->Themes().Resolve(kind_, variant_, State(), surface->Scale()).text_color;
    }
    else
    {
        color = surface->Themes().Color(ColorId::Text);
    }
    const f32 scale = surface->Scale();
    const bool cut = ellipsis_ && !wrap_;
    const text::TextLayout& layout = cut ? run_.Fit(Bounds().w * scale) : run_.Full();
    f32 x = Bounds().x;
    if (!wrap_)
    {
        f32 spare = Bounds().w - layout.width / scale;
        if (spare < 0)
        {
            spare = 0;
        }
        if (align_ == text::Align::Center)
        {
            x += spare / 2;
        }
        if (align_ == text::Align::Right)
        {
            x += spare;
        }
    }
    canvas.DrawText(layout, run_.Style(), Point{x, Bounds().y}, color);
}

}  // namespace engine_placeholder::ui
