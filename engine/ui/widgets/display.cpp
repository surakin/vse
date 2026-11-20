#include "ui/widgets/display.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/label.h"
#include "ui/widgets/look.h"

namespace vse::ui
{

Size Label::Measure(const Constraints& constraints)
{
    return TextBlock::Measure(constraints);
}

// ---- Image ----

void Image::SetImage(ImageId image, Size natural)
{
    image_ = image;
    natural_ = natural;
    MarkDirty(kLayoutDirty);
}

void Image::SetFit(ImageFit fit)
{
    if (fit_ == fit)
    {
        return;
    }
    fit_ = fit;
    MarkDirty(kDrawDirty);
}

void Image::SetTint(u32 abgr)
{
    if (tint_ == abgr)
    {
        return;
    }
    tint_ = abgr;
    MarkDirty(kDrawDirty);
}

Size Image::Measure(const Constraints&)
{
    return image_ == 0 ? Size() : natural_;
}

void Image::Draw(Canvas& canvas)
{
    if (image_ == 0 || !(natural_.w > 0) || !(natural_.h > 0))
    {
        return;
    }
    const Rect& b = Bounds();
    if (!(b.w > 0) || !(b.h > 0))
    {
        return;
    }
    Rect dest = b;
    if (fit_ != ImageFit::Stretch)
    {
        const f32 fit_scale = FMin(b.w / natural_.w, b.h / natural_.h);
        const f32 fill_scale = FMax(b.w / natural_.w, b.h / natural_.h);
        const f32 s = fit_ == ImageFit::Contain ? fit_scale : (fit_ == ImageFit::Cover ? fill_scale : 1.0f);
        dest.w = natural_.w * s;
        dest.h = natural_.h * s;
        dest.x = b.x + (b.w - dest.w) / 2;
        dest.y = b.y + (b.h - dest.h) / 2;
    }
    // The part of the picture inside the bounds, as texture coordinates.
    const f32 left = FMax(dest.x, b.x);
    const f32 top = FMax(dest.y, b.y);
    const f32 right = FMin(dest.Right(), b.Right());
    const f32 bottom = FMin(dest.Bottom(), b.Bottom());
    if (!(right > left) || !(bottom > top))
    {
        return;
    }
    Rect uv;
    uv.x = (left - dest.x) / dest.w;
    uv.y = (top - dest.y) / dest.h;
    uv.w = (right - left) / dest.w;
    uv.h = (bottom - top) / dest.h;
    Rect shown;
    shown.x = left;
    shown.y = top;
    shown.w = right - left;
    shown.h = bottom - top;
    canvas.DrawImage(shown, image_, uv, tint_);
}

// ---- ProgressBar ----

void ProgressBar::SetValue(f32 value)
{
    if (!(value > 0))
    {
        value = 0;  // also a NaN
    }
    if (value > 1)
    {
        value = 1;
    }
    if (value == value_)
    {
        return;
    }
    value_ = value;
    MarkDirty(kDrawDirty);
}

void ProgressBar::SetIndeterminate(bool indeterminate)
{
    if (indeterminate_ == indeterminate)
    {
        return;
    }
    indeterminate_ = indeterminate;
    MarkDirty(kDrawDirty);
}

Size ProgressBar::Measure(const Constraints&)
{
    Surface* surface = GetSurface();
    const f32 icon = surface != nullptr ? surface->Themes().Metric(MetricId::IconSize, 1.0f) : 0.0f;
    return Size{icon * kLengthInIcons, icon / 2};
}

void ProgressBar::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const ResolvedStyle track = surface->Themes().Resolve(widget_kind::kPanel, VariantId(0), State(), scale);
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, track, scale);
    const f32 inset = track.border_width / scale;
    Rect inner;
    inner.x = b.x + inset;
    inner.y = b.y + inset;
    inner.w = b.w - inset * 2;
    inner.h = b.h - inset * 2;
    if (!(inner.w > 0) || !(inner.h > 0))
    {
        return;
    }
    const u32 fill = surface->Themes().Color(ColorId::Accent);
    f32 radius = StyleRadius(track, scale) - inset;
    if (radius < 0)
    {
        radius = 0;
    }
    canvas.PushClip(inner);
    Rect bar = inner;
    if (indeterminate_)
    {
        const f32 phase = Fmod(static_cast<f32>(surface->Time()) / kProgressPeriod, 1.0f);
        bar.w = inner.w * kSegment;
        bar.x = inner.x + (inner.w + bar.w) * phase - bar.w;
    }
    else
    {
        bar.w = inner.w * value_;
    }
    if (bar.w > 0)
    {
        if (radius > 0)
        {
            canvas.DrawRoundedRect(bar, CornerRadii(radius), fill);
        }
        else
        {
            canvas.DrawRect(bar, fill);
        }
    }
    canvas.PopClip();
}

// ---- Separator ----

Size Separator::Measure(const Constraints&)
{
    Surface* surface = GetSurface();
    const f32 thickness = surface != nullptr ? surface->Themes().Metric(MetricId::BorderWidth, 1.0f) : 0.0f;
    return vertical_ ? Size{thickness, 0} : Size{0, thickness};
}

void Separator::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const f32 thickness = FMax(surface->Themes().Metric(MetricId::BorderWidth, scale), 1.0f) / scale;
    Rect line = Bounds();
    if (vertical_)
    {
        line.x += (line.w - thickness) / 2;
        line.w = thickness;
    }
    else
    {
        line.y += (line.h - thickness) / 2;
        line.h = thickness;
    }
    canvas.DrawRect(line, surface->Themes().Color(ColorId::Border));
}

// ---- Panel ----

void Panel::SetVariant(StringView variant)
{
    variant_ = RegisterVariant(variant);
    MarkDirty(kDrawDirty);
}

void Panel::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    DrawStyleBox(canvas, Bounds(), surface->Themes().Resolve(widget_kind::kPanel, variant_, State(), scale), scale);
}

}  // namespace vse::ui
