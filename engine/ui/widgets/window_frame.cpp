#include "ui/widgets/window_frame.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace vse::ui
{

WindowFrame::WindowFrame(StringView name, StringView title) : Widget(name)
{
    title_.SetText(title);
    SetClipsChildren(false);
}

void WindowFrame::SetTitle(StringView title)
{
    if (title_.SetText(title))
    {
        MarkDirty(kLayoutDirty);
    }
}

void WindowFrame::SetContent(UniquePtr<Widget> content)
{
    if (content_ != nullptr)
    {
        RemoveChild(content_);
    }
    content_ = content.Get() != nullptr ? AddChild(static_cast<UniquePtr<Widget>&&>(content)) : nullptr;
    MarkDirty(kLayoutDirty);
}

void WindowFrame::SetFooter(UniquePtr<Widget> footer)
{
    if (footer_ != nullptr)
    {
        RemoveChild(footer_);
    }
    footer_ = footer.Get() != nullptr ? AddChild(static_cast<UniquePtr<Widget>&&>(footer)) : nullptr;
    MarkDirty(kLayoutDirty);
}

void WindowFrame::SetClosable(bool closable)
{
    closable_ = closable;
    MarkDirty(kLayoutDirty);
}

void WindowFrame::CloseRequested()
{
    on_close_.Invoke(this);
}

f32 WindowFrame::TitleHeight()
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return 0;
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTitleBar, VariantId(0), 0, scale);
    f32 h = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    if (title_.EnsureForStyle(surface, style, 0))
    {
        h = FMax(h, title_.Height());
    }
    return h + style.padding_y / scale * 2;
}

Size WindowFrame::Measure(const Constraints& constraints)
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return Size();
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle frame = surface->Themes().Resolve(widget_kind::kWindow, VariantId(0), 0, scale);
    const ResolvedStyle bar = surface->Themes().Resolve(widget_kind::kTitleBar, VariantId(0), 0, scale);
    const f32 pad_x = frame.padding_x / scale;
    const f32 pad_y = frame.padding_y / scale;
    const f32 gap = surface->Themes().Metric(MetricId::Spacing, 1.0f);
    const f32 icon = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    title_h_ = TitleHeight();
    Constraints inner = constraints;
    const f32 widest = icon * kMaxWidthIcons;
    inner.max_w = FMin(constraints.max_w, widest) - pad_x * 2;
    if (inner.max_w < 0)
    {
        inner.max_w = 0;
    }
    inner.max_h = FMax(constraints.max_h - title_h_ - pad_y * 2, 0.0f);
    Size content;
    if (content_ != nullptr)
    {
        content = content_->MeasureTotal(inner);
    }
    Size footer;
    if (footer_ != nullptr)
    {
        footer = footer_->MeasureTotal(inner);
    }
    footer_h_ = footer.h;
    f32 title_w = bar.padding_x / scale * 2 + (title_.Text().Length() > 0 ? title_.Width() : 0.0f);
    if (closable_)
    {
        title_w += gap + icon;
    }
    Size size;
    size.w = FMax(FMax(content.w, footer.w) + pad_x * 2, title_w);
    size.h = title_h_ + pad_y * 2 + content.h + (footer_ != nullptr ? gap + footer.h : 0.0f);
    return size;
}

void WindowFrame::Arrange(const Rect& rect)
{
    SetBounds(rect);
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return;
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle frame = surface->Themes().Resolve(widget_kind::kWindow, VariantId(0), 0, scale);
    const f32 pad_x = frame.padding_x / scale;
    const f32 pad_y = frame.padding_y / scale;
    const f32 gap = surface->Themes().Metric(MetricId::Spacing, 1.0f);
    title_h_ = TitleHeight();
    const f32 inner_w = FMax(rect.w - pad_x * 2, 0.0f);
    if (footer_ != nullptr)
    {
        Constraints c;
        c.max_w = inner_w;
        c.max_h = rect.h;
        footer_h_ = footer_->MeasureTotal(c).h;
    }
    else
    {
        footer_h_ = 0;
    }
    const f32 top = rect.y + title_h_ + pad_y;
    const f32 bottom = rect.Bottom() - pad_y - (footer_ != nullptr ? footer_h_ + gap : 0.0f);
    if (content_ != nullptr)
    {
        content_->Arrange(Rect{rect.x + pad_x, top, inner_w, FMax(bottom - top, 0.0f)});
    }
    if (footer_ != nullptr)
    {
        footer_->Arrange(Rect{rect.x + pad_x, rect.Bottom() - pad_y - footer_h_, inner_w, footer_h_});
    }
}

Rect WindowFrame::TitleBarRect()
{
    title_h_ = TitleHeight();
    return Rect{Bounds().x, Bounds().y, Bounds().w, title_h_};
}

Rect WindowFrame::CloseButtonRect()
{
    if (!closable_)
    {
        return Rect();
    }
    const Rect bar = TitleBarRect();
    return Rect{bar.Right() - bar.h, bar.y, bar.h, bar.h};
}

Rect WindowFrame::ContentRect()
{
    return content_ != nullptr ? content_->Bounds() : Rect();
}

void WindowFrame::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const Rect& b = Bounds();
    u32 state = State();
    if (surface->Focus() != nullptr)
    {
        for (const Widget* w = surface->Focus(); w != nullptr; w = w->Parent())
        {
            if (w == this)
            {
                state |= kStateFocused;
            }
        }
    }
    DrawStyleBox(canvas, b, surface->Themes().Resolve(widget_kind::kWindow, VariantId(0), state & kStateFocused, scale),
                 scale);
    const Rect bar = TitleBarRect();
    const ResolvedStyle bar_style = surface->Themes().Resolve(widget_kind::kTitleBar, VariantId(0), 0, scale);
    if (IsVisibleColor(bar_style.background))
    {
        canvas.DrawRoundedRect(
            bar,
            CornerRadii(StyleRadius(surface->Themes().Resolve(widget_kind::kWindow, VariantId(0), 0, scale), scale)),
            bar_style.background);
    }
    if (title_.Text().Length() > 0 && title_.EnsureForStyle(surface, bar_style, 0))
    {
        canvas.DrawText(title_.Full(), title_.Style(),
                        Point{bar.x + bar_style.padding_x / scale, bar.y + (bar.h - title_.Height()) / 2},
                        bar_style.text_color);
    }
    if (closable_)
    {
        const Rect c = CloseButtonRect();
        if (close_hover_)
        {
            canvas.DrawRect(
                c, surface->Themes().Resolve(widget_kind::kTitleBar, VariantId(0), kStateHover, scale).background);
        }
        const f32 s = surface->Themes().Metric(MetricId::IconSize, 1.0f) * kCloseGlyph;
        const Point mid{c.x + c.w / 2, c.y + c.h / 2};
        canvas.DrawLine(Point{mid.x - s / 2, mid.y - s / 2}, Point{mid.x + s / 2, mid.y + s / 2}, 1.0f,
                        bar_style.text_color);
        canvas.DrawLine(Point{mid.x - s / 2, mid.y + s / 2}, Point{mid.x + s / 2, mid.y - s / 2}, 1.0f,
                        bar_style.text_color);
    }
}

u32 WindowFrame::EdgesAt(Point local) const
{
    if (!resizable_)
    {
        return 0;
    }
    const Rect& b = Bounds();
    u32 edges = 0;
    if (local.x < kEdge)
    {
        edges |= kLeft;
    }
    if (local.x >= b.w - kEdge)
    {
        edges |= kRight;
    }
    if (local.y < kEdge)
    {
        edges |= kTop;
    }
    if (local.y >= b.h - kEdge)
    {
        edges |= kBottom;
    }
    return edges;
}

Size WindowFrame::Floor()
{
    const f32 icon = GetSurface()->Themes().Metric(MetricId::IconSize, 1.0f);
    Size floor = min_size_;
    if (floor.w <= 0)
    {
        floor.w = icon * kMinWidthIcons;
    }
    if (floor.h <= 0)
    {
        floor.h = TitleHeight() * 2;
    }
    return floor;
}

void WindowFrame::OnEvent(Event& event)
{
    const Rect& b = Bounds();
    const Point at{event.position.x + b.x,
                   event.position.y + b.y};  // surface coordinates, while the frame has not moved
    switch (event.type)
    {
    case EventType::PointerMove:
    {
        if ((dragging_ || close_pressed_) && (State() & kStatePressed) == 0)
        {
            dragging_ = false;  // the capture ended without a release (focus lost, a modal opened)
            close_pressed_ = false;
        }
        if (dragging_)
        {
            event.handled = true;
            const Rect limit = Parent() != nullptr ? Parent()->Bounds() : Rect{0, 0, kUnbounded, kUnbounded};
            Rect r = b;
            if (drag_edges_ == 0)
            {
                r.x = FMin(FMax(at.x - grab_.x, limit.x), FMax(limit.Right() - r.w, limit.x));
                r.y = FMin(FMax(at.y - grab_.y, limit.y), FMax(limit.Bottom() - r.h, limit.y));
            }
            else
            {
                const Size floor = Floor();
                const f32 px = FMin(FMax(at.x, limit.x), limit.Right());
                const f32 py = FMin(FMax(at.y, limit.y), limit.Bottom());
                f32 left = b.x;
                f32 right = b.Right();
                f32 top = b.y;
                f32 bottom = b.Bottom();
                if ((drag_edges_ & kLeft) != 0)
                {
                    left = FMin(px, right - floor.w);
                }
                if ((drag_edges_ & kRight) != 0)
                {
                    right = FMax(px, left + floor.w);
                }
                if ((drag_edges_ & kTop) != 0)
                {
                    top = FMin(py, bottom - floor.h);
                }
                if ((drag_edges_ & kBottom) != 0)
                {
                    bottom = FMax(py, top + floor.h);
                }
                r = Rect{left, top, right - left, bottom - top};
            }
            Arrange(r);
            MarkDirty(kDrawDirty);
            break;
        }
        const bool over_close = closable_ && CloseButtonRect().Contains(at);
        if (over_close != close_hover_)
        {
            close_hover_ = over_close;
            MarkDirty(kDrawDirty);
        }
        const u32 edges = EdgesAt(event.position);
        if (edges == 0)
        {
            ClearCursor();
        }
        else if (edges == (kLeft | kTop) || edges == (kRight | kBottom))
        {
            SetCursor(os::CursorShape::ResizeDiagonalNWSE);
        }
        else if (edges == (kRight | kTop) || edges == (kLeft | kBottom))
        {
            SetCursor(os::CursorShape::ResizeDiagonalNESW);
        }
        else if ((edges & (kLeft | kRight)) != 0)
        {
            SetCursor(os::CursorShape::ResizeHorizontal);
        }
        else
        {
            SetCursor(os::CursorShape::ResizeVertical);
        }
        break;
    }
    case EventType::PointerLeave:
        close_hover_ = false;
        ClearCursor();
        MarkDirty(kDrawDirty);
        break;
    case EventType::PointerDown:
    {
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        const u32 edges = EdgesAt(event.position);
        if (closable_ && CloseButtonRect().Contains(at))
        {
            event.handled = true;
            close_pressed_ = true;
        }
        else if (edges != 0)
        {
            event.handled = true;
            dragging_ = true;
            drag_edges_ = edges;
        }
        else if (movable_ && TitleBarRect().Contains(at))
        {
            event.handled = true;
            dragging_ = true;
            drag_edges_ = 0;
            grab_ = event.position;
        }
        break;
    }
    case EventType::PointerUp:
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        if (dragging_)
        {
            event.handled = true;
            dragging_ = false;
            drag_edges_ = 0;
        }
        if (close_pressed_)
        {
            event.handled = true;
            close_pressed_ = false;
            if (CloseButtonRect().Contains(at))
            {
                CloseRequested();
            }
        }
        break;
    default: break;
    }
}

}  // namespace vse::ui
