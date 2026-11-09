#include "ui/text_run.h"

#include <cmath>

#include "ui/surface.h"

namespace engine_placeholder::ui {

bool TextRun::SetText(StringView text) {
    if (text_.View() == text) return false;
    text_ = String<HeapAllocator>(HeapAllocator::Default(), text);
    full_valid_ = false;
    cut_valid_ = false;
    return true;
}

bool TextRun::SetAlign(text::Align align) {
    if (align_ == align) return false;
    align_ = align;
    full_valid_ = false;
    cut_valid_ = false;
    return true;
}

bool TextRun::Ensure(text::Font* face, u32 family, f32 size_px, f32 wrap_px, f32 scale) {
    if (face == nullptr) return false;
    const u32 pixel_size = static_cast<u32>(size_px);
    if (full_valid_ && wrap_px_ == wrap_px && pixel_size_ == pixel_size && family_ == family && font_ == face && scale_ == scale) return true;
    style_.font = face;
    style_.pixel_size = pixel_size;
    text::LayoutOptions options;
    options.max_width = wrap_px;
    options.align = align_;
    full_valid_ = false;
    cut_valid_ = false;
    if (!text::Layout(text_.View(), style_, options, &full_).Ok()) return false;
    full_valid_ = true;
    wrap_px_ = wrap_px;
    pixel_size_ = pixel_size;
    family_ = family;
    font_ = face;
    scale_ = scale;
    ++layout_count_;
    return true;
}

bool TextRun::EnsureForStyle(Surface* surface, const ResolvedStyle& style, f32 wrap_px) {
    if (surface == nullptr || surface->GetFontProvider() == nullptr) return false;
    f32 size_px = std::floor(style.font.size + 0.5f);
    if (size_px < 1) size_px = 1;
    return Ensure(surface->GetFontProvider()->Resolve(style.font.family), style.font.family, size_px, wrap_px, surface->Scale());
}

const text::TextLayout& TextRun::Fit(f32 limit_px) {
    if (!(limit_px > 0) || full_.width <= limit_px) return full_;
    if (cut_valid_ && cut_limit_ == limit_px) return cut_;
    static const char kEllipsis[] = "\xE2\x80\xA6";
    text::LayoutOptions options;
    options.align = align_;
    text::TextLayout ellipsis;
    f32 room = limit_px;
    if (text::Layout(StringView(kEllipsis, 3), style_, options, &ellipsis).Ok()) room -= ellipsis.width;
    // The longest prefix of whole clusters on the first line that leaves room for the ellipsis.
    u32 cut = 0;
    for (usize i = 0; i < full_.clusters.Size(); ++i) {
        const text::ClusterInfo& c = full_.clusters[i];
        if (c.line != 0 || c.kind == text::ClusterKind::Break) break;
        if (c.x + c.width > room) break;
        cut = c.byte_end;
    }
    String<HeapAllocator> shown(HeapAllocator::Default(), StringView(text_.CStr(), cut));
    shown.Append(StringView(kEllipsis, 3));
    cut_valid_ = text::Layout(shown.View(), style_, options, &cut_).Ok();
    cut_limit_ = limit_px;
    return cut_valid_ ? cut_ : full_;
}

} // namespace engine_placeholder::ui
