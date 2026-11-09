#include "ui/text_block.h"

#include <cmath>

#include "ui/surface.h"

namespace engine_placeholder::ui {

TextBlock::TextBlock(StringView name, StringView text) : Widget(name), text_(HeapAllocator::Default(), text) {}

void TextBlock::SetText(StringView text) {
    if (text_.View() == text) return;
    text_ = String<HeapAllocator>(HeapAllocator::Default(), text);
    cached_ = false;
    MarkDirty(kLayoutDirty);
}

void TextBlock::SetFont(FontRef font) {
    font_ = font;
    font_set_ = true;
    cached_ = false;
    MarkDirty(kLayoutDirty);
}

void TextBlock::SetColor(u32 abgr) {
    color_ = abgr;
    color_set_ = true;
    MarkDirty(kDrawDirty);
}

void TextBlock::SetWrap(bool wrap) {
    if (wrap_ == wrap) return;
    wrap_ = wrap;
    cached_ = false;
    MarkDirty(kLayoutDirty);
}

// Lays the text out for `wrap_width` logical units (kUnbounded or a non-wrapping block: no wrapping). Returns false when there is no font.
bool TextBlock::EnsureLayout(f32 wrap_width) {
    Surface* surface = GetSurface();
    if (surface == nullptr || surface->GetFontProvider() == nullptr) return false;
    const FontRef font = font_set_ ? font_ : surface->Themes().Font(FontId::Body, 1.0f);
    text::Font* face = surface->GetFontProvider()->Resolve(font.family);
    if (face == nullptr) return false;
    const f32 scale = surface->Scale();
    f32 size_px = std::floor(font.size * scale + 0.5f);
    if (size_px < 1) size_px = 1;
    f32 wrap_px = (wrap_ && wrap_width < kUnbounded) ? std::floor(wrap_width * scale) : 0.0f;
    if (wrap_ && wrap_width < kUnbounded && wrap_px < 1) wrap_px = 1; // 0 would mean "no wrapping" to the text layout
    if (cached_ && cached_wrap_px_ == wrap_px && cached_pixel_size_ == static_cast<u32>(size_px) && cached_family_ == font.family && cached_font_ == face && scale_ == scale) return true;
    style_.font = face;
    style_.pixel_size = static_cast<u32>(size_px);
    text::LayoutOptions options;
    options.max_width = wrap_px;
    if (!text::Layout(text_.View(), style_, options, &layout_).Ok()) return false;
    cached_ = true;
    cached_wrap_px_ = wrap_px;
    cached_pixel_size_ = style_.pixel_size;
    cached_family_ = font.family;
    cached_font_ = face;
    scale_ = scale;
    ++layout_count_;
    return true;
}

Size TextBlock::Measure(const Constraints& constraints) {
    if (!EnsureLayout(constraints.max_w)) return Size();
    return Size{layout_.width / scale_, layout_.height / scale_};
}

void TextBlock::Draw(Canvas& canvas) {
    if (!EnsureLayout(Bounds().w)) return;
    Surface* surface = GetSurface();
    const u32 color = color_set_ ? color_ : surface->Themes().Color(ColorId::Text);
    canvas.DrawText(layout_, style_, Point{Bounds().x, Bounds().y}, color);
}

} // namespace engine_placeholder::ui
