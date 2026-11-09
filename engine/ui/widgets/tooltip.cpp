#include "ui/widgets/tooltip.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

TooltipBubble::TooltipBubble() : Widget(StringView("tooltip")) { SetPassThrough(true); }

void TooltipBubble::SetText(StringView text) {
    if (run_.SetText(text)) MarkDirty(kLayoutDirty);
}

Size TooltipBubble::Measure(const Constraints& constraints) {
    Surface* surface = GetSurface();
    if (surface == nullptr) return Size();
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTooltip, VariantId(0), 0, scale);
    const f32 pad_x = style.padding_x / scale;
    const f32 pad_y = style.padding_y / scale;
    f32 limit = surface->Themes().Metric(MetricId::IconSize, 1.0f) * kMaxWidthIcons;
    if (constraints.max_w - pad_x * 2 < limit) limit = constraints.max_w - pad_x * 2;
    if (!run_.EnsureForStyle(surface, style, limit * scale)) return Size();
    return Size{run_.Width() + pad_x * 2, run_.Height() + pad_y * 2};
}

void TooltipBubble::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTooltip, VariantId(0), 0, scale);
    DrawStyleBox(canvas, Bounds(), style, scale);
    canvas.DrawText(run_.Full(), run_.Style(), Point{Bounds().x + style.padding_x / scale, Bounds().y + style.padding_y / scale}, style.text_color);
}

} // namespace engine_placeholder::ui
