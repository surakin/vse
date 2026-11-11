#include "ui/widgets/value_widgets.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui
{

// ---- Slider ----

void Slider::SetRange(const ValueRange& range)
{
    range_ = range;
    value_ = range_.Snap(value_);
    MarkDirty(kDrawDirty);
}

void Slider::SetValue(f64 value)
{
    const f64 next = range_.Apply(value, value_);
    if (next == value_)
    {
        return;
    }
    value_ = next;
    MarkDirty(kDrawDirty);
}

void Slider::SetTicks(u32 count)
{
    ticks_ = count;
    MarkDirty(kDrawDirty);
}

f64 Slider::Page() const
{
    return page_ > 0 ? page_ : (range_.max - range_.min) * kPageFraction;
}

f64 Slider::KeyStep() const
{
    const f64 step = range_.EffectiveStep();
    return step > 0 ? step : (range_.max - range_.min) * kKeyFraction;
}

f32 Slider::ThumbLength() const
{
    const Surface* surface = GetSurface();
    return surface != nullptr ? const_cast<Surface*>(surface)->Themes().Metric(MetricId::IconSize, 1.0f) : 0.0f;
}

f32 Slider::TrackLength() const
{
    return vertical_ ? Bounds().h : Bounds().w;
}

// Position of the thumb's start along the track for `value`, in local units.
f32 Slider::PositionOf(f64 value) const
{
    const f64 span = range_.max - range_.min;
    const f32 room = TrackLength() - ThumbLength();
    if (!(span > 0) || !(room > 0))
    {
        return 0;
    }
    f32 t = static_cast<f32>((value - range_.min) / span);
    if (vertical_)
    {
        t = 1.0f - t;  // the minimum is at the bottom
    }
    return room * t;
}

// The value for a pointer at `along` (local), the thumb centered on it.
f64 Slider::ValueAt(f32 along) const
{
    const f32 room = TrackLength() - ThumbLength();
    if (!(room > 0))
    {
        return range_.min;
    }
    f32 t = (along - ThumbLength() / 2) / room;
    t = FMin(FMax(t, 0.0f), 1.0f);
    if (vertical_)
    {
        t = 1.0f - t;
    }
    return range_.min + (range_.max - range_.min) * static_cast<f64>(t);
}

Rect Slider::ThumbRect() const
{
    const Rect& b = Bounds();
    const f32 pos = PositionOf(value_);
    Rect r = b;
    if (vertical_)
    {
        r.y = b.y + pos;
        r.h = ThumbLength();
    }
    else
    {
        r.x = b.x + pos;
        r.w = ThumbLength();
    }
    return r;
}

void Slider::UserSet(f64 value)
{
    const f64 next = range_.Apply(value, value_);
    if (next == value_)
    {
        return;
    }
    value_ = next;
    MarkDirty(kDrawDirty);
    on_changed_.Invoke(this);
}

Size Slider::Measure(const Constraints&)
{
    const f32 thumb = ThumbLength();
    const f32 length = thumb * kLengthInThumbs;
    return vertical_ ? Size{thumb, length} : Size{length, thumb};
}

void Slider::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const ResolvedStyle track = surface->Themes().Resolve(widget_kind::kSlider, VariantId(0), State(), scale);
    const Rect& b = Bounds();
    const f32 thick = FMax(surface->Themes().Metric(MetricId::BorderWidth, 1.0f) * kTrackBorders, 1.0f);
    Rect bar = b;
    if (vertical_)
    {
        bar.x += (b.w - thick) / 2;
        bar.w = thick;
    }
    else
    {
        bar.y += (b.h - thick) / 2;
        bar.h = thick;
    }
    DrawStyleBox(canvas, bar, track, scale);
    if (ticks_ > 0)
    {
        const u32 ink = surface->Themes().Color(ColorId::Border);
        const f32 room = TrackLength() - ThumbLength();
        for (u32 i = 0; i <= ticks_; ++i)
        {
            const f32 at = ThumbLength() / 2 + room * static_cast<f32>(i) / static_cast<f32>(ticks_);
            Rect tick = b;
            if (vertical_)
            {
                tick.y = b.y + at;
                tick.h = 1;
                tick.x = bar.Right();
                tick.w = ThumbLength() * kTickFraction;
            }
            else
            {
                tick.x = b.x + at;
                tick.w = 1;
                tick.y = bar.Bottom();
                tick.h = ThumbLength() * kTickFraction;
            }
            canvas.DrawRect(tick, ink);
        }
    }
    u32 state = 0;
    if (dragging_)
    {
        state |= kStatePressed;
    }
    if ((State() & kStateHover) != 0)
    {
        state |= kStateHover;
    }
    if ((State() & kStateDisabled) != 0)
    {
        state |= kStateDisabled;
    }
    const ResolvedStyle thumb = surface->Themes().Resolve(widget_kind::kScrollbar, VariantId(0), state, scale);
    Rect t = ThumbRect();
    DrawStyleBox(canvas, t, thumb, scale);
}

void Slider::OnEvent(Event& event)
{
    switch (event.type)
    {
    case EventType::PointerDown:
    {
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        event.handled = true;
        const f32 along = vertical_ ? event.position.y : event.position.x;
        const f32 pos = PositionOf(value_);
        if (along >= pos && along < pos + ThumbLength())
        {
            dragging_ = true;
            grab_ = along - pos;
        }
        else if (track_pages_)
        {
            const f64 target = ValueAt(along);
            UserSet(target > value_ ? value_ + Page() : value_ - Page());
            on_commit_.Invoke(this);
        }
        else
        {
            dragging_ = true;
            grab_ = ThumbLength() / 2;
            UserSet(ValueAt(along));
        }
        MarkDirty(kDrawDirty);
        break;
    }
    case EventType::PointerMove:
        if (dragging_)
        {
            event.handled = true;
            if ((State() & kStatePressed) == 0)
            {
                dragging_ = false;
                break;
            }
            const f32 along = vertical_ ? event.position.y : event.position.x;
            UserSet(ValueAt(along - grab_ + ThumbLength() / 2));
        }
        break;
    case EventType::PointerUp:
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        event.handled = true;
        if (dragging_)
        {
            dragging_ = false;
            MarkDirty(kDrawDirty);
            on_commit_.Invoke(this);
        }
        break;
    case EventType::Wheel:
        if (wheel_)
        {
            event.handled = true;
            UserSet(value_ - static_cast<f64>(event.wheel_y) * KeyStep());
            on_commit_.Invoke(this);
        }
        break;
    case EventType::KeyDown:
    {
        f64 next = value_;
        bool used = true;
        switch (event.key)
        {
        case Key::Left:
        case Key::Down: next = value_ - KeyStep(); break;
        case Key::Right:
        case Key::Up: next = value_ + KeyStep(); break;
        case Key::PageDown: next = value_ - Page(); break;
        case Key::PageUp: next = value_ + Page(); break;
        case Key::Home: next = range_.min; break;
        case Key::End: next = range_.max; break;
        default: used = false; break;
        }
        if (used)
        {
            event.handled = true;
            UserSet(next);
            on_commit_.Invoke(this);
        }
        break;
    }
    default: break;
    }
}

// ---- SpinBox ----

SpinBox::SpinBox(StringView name) : Widget(name)
{
    range_.min = 0;
    range_.max = kDefaultMax;
    range_.step = 1;
    field_ = static_cast<TextField*>(AddChild(MakeUnique<TextField>(StringView("text"))));
    field_->SetFrame(false);
    field_->SetValidator(&SpinBox::Accepts, this);
    field_->OnSubmit().Connect(&SpinBox::Submitted, this);
    field_->OnCancel().Connect(&SpinBox::Cancelled, this);
    field_->OnFocusLost().Connect(&SpinBox::FocusLeft, this);
    SyncText();
}

void SpinBox::SyncText()
{
    field_->SetText(ValueText().View());
}

void SpinBox::SetRange(const ValueRange& range)
{
    range_ = range;
    value_ = range_.Snap(value_);
    SyncText();
    MarkDirty(kLayoutDirty);
}

void SpinBox::SetValue(f64 value)
{
    const f64 next = range_.Apply(value, value_);
    if (next == value_)
    {
        return;
    }
    value_ = next;
    SyncText();
    MarkDirty(kLayoutDirty);
}

void SpinBox::SetDecimals(int decimals)
{
    decimals_ = decimals < 0 ? 0 : (decimals > kMaxDecimals ? kMaxDecimals : decimals);
    SyncText();
    MarkDirty(kLayoutDirty);
}

f64 SpinBox::Step() const
{
    const f64 step = range_.EffectiveStep();
    return step > 0 ? step : 1.0;
}

String<HeapAllocator> SpinBox::ValueText() const
{
    String<HeapAllocator> out(HeapAllocator::Default(), StringView());
    StringBuilder<HeapAllocator> sb(HeapAllocator::Default());
    sb.AppendFloat(value_, decimals_);
    out.Append(sb.View());
    return out;
}

f32 SpinBox::ButtonWidth() const
{
    return GetSurface()->Themes().Metric(MetricId::IconSize, 1.0f) * kButtonIcons;
}

void SpinBox::UserSet(f64 value)
{
    const f64 next = range_.Apply(value, value_);
    if (next == value_)
    {
        SyncText();  // typed text that the range refused goes back
        return;
    }
    value_ = next;
    SyncText();
    MarkDirty(kLayoutDirty);
    on_changed_.Invoke(this);
}

bool SpinBox::Accepts(void* user, const TextField*, StringView proposed)
{
    const SpinBox* box = static_cast<const SpinBox*>(user);
    usize i = 0;
    if (i < proposed.Length() && (proposed[i] == '+' || (proposed[i] == '-' && box->range_.min < 0)))
    {
        ++i;
    }
    int digits_after = -1;  // -1: no decimal point yet
    for (; i < proposed.Length(); ++i)
    {
        const char c = proposed[i];
        if (IsAsciiDigit(c))
        {
            if (digits_after >= 0)
            {
                ++digits_after;
            }
        }
        else if (c == '.' && digits_after < 0 && box->decimals_ > 0)
        {
            digits_after = 0;
        }
        else
        {
            return false;
        }
    }
    return digits_after <= box->decimals_;
}

bool SpinBox::Parse(StringView text, f64* out) const
{
    usize i = 0;
    bool negative = false;
    if (i < text.Length() && (text[i] == '+' || text[i] == '-'))
    {
        negative = text[i] == '-';
        ++i;
    }
    f64 mantissa = 0;
    f64 scale = 1;
    bool any = false;
    bool after_point = false;
    for (; i < text.Length(); ++i)
    {
        const char c = text[i];
        if (c == '.' && !after_point)
        {
            after_point = true;
        }
        else if (IsAsciiDigit(c))
        {
            any = true;
            mantissa = mantissa * kDecimalBase + static_cast<f64>(c - '0');
            if (after_point)
            {
                scale *= kDecimalBase;
            }
        }
        else
        {
            return false;
        }
    }
    if (!any)
    {
        return false;
    }
    *out = (negative ? -mantissa : mantissa) / scale;
    return true;
}

void SpinBox::Commit()
{
    f64 parsed = 0;
    if (Parse(field_->Text(), &parsed))
    {
        UserSet(parsed);
    }
    else
    {
        SyncText();
    }
}

void SpinBox::Submitted(void* user, TextField*)
{
    static_cast<SpinBox*>(user)->Commit();
}

void SpinBox::Cancelled(void* user, TextField*)
{
    static_cast<SpinBox*>(user)->SyncText();
}

void SpinBox::FocusLeft(void* user, TextField*)
{
    static_cast<SpinBox*>(user)->Commit();
}

Size SpinBox::Measure(const Constraints&)
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return Size();
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    // The widest of the two ends of the range.
    StringBuilder<HeapAllocator> lo(HeapAllocator::Default());
    StringBuilder<HeapAllocator> hi(HeapAllocator::Default());
    lo.AppendFloat(range_.min, decimals_);
    hi.AppendFloat(range_.max, decimals_);
    f32 text_w = 0;
    f32 text_h = 0;
    TextRun probe;
    for (int i = 0; i < 2; ++i)
    {
        probe.SetText(i == 0 ? lo.View() : hi.View());
        if (probe.EnsureForStyle(surface, style, 0))
        {
            text_w = FMax(text_w, probe.Width());
            text_h = FMax(text_h, probe.Height());
        }
    }
    Size size;
    size.w = text_w + style.padding_x / scale * 2 + ButtonWidth();
    size.h = text_h + style.padding_y / scale * 2;
    return size;
}

void SpinBox::Arrange(const Rect& rect)
{
    SetBounds(rect);
    Rect inner = rect;
    inner.w = FMax(rect.w - ButtonWidth(), 0.0f);
    field_->Arrange(inner);
}

void SpinBox::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    u32 state = State();
    if (surface->Focus() == field_ && surface->WindowFocused())
    {
        state |= kStateFocused;
    }
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), state, scale);
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, style, scale);
    const f32 button_w = ButtonWidth();
    // The buttons: a divider and two triangles.
    const f32 x = b.Right() - button_w;
    const u32 ink = (State() & kStateDisabled) != 0 ? surface->Themes().Color(ColorId::TextDisabled)
                                                    : surface->Themes().Color(ColorId::Text);
    canvas.DrawRect(Rect{x, b.y, 1, b.h}, surface->Themes().Color(ColorId::Border));
    const f32 mid_x = x + button_w / 2;
    const f32 half = b.h / 2;
    const f32 reach = button_w * kArrowReach;
    const f32 up_y = b.y + half / 2;
    const f32 down_y = b.y + half + half / 2;
    canvas.DrawTriangle(Point{mid_x, up_y - reach / 2}, Point{mid_x + reach, up_y + reach / 2},
                        Point{mid_x - reach, up_y + reach / 2}, ink);
    canvas.DrawTriangle(Point{mid_x - reach, down_y - reach / 2}, Point{mid_x + reach, down_y - reach / 2},
                        Point{mid_x, down_y + reach / 2}, ink);
}

void SpinBox::OnEvent(Event& event)
{
    switch (event.type)
    {
    case EventType::PointerDown:
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        event.handled = true;
        if (event.position.x >= Bounds().w - ButtonWidth())
        {
            field_->RequestFocus();
            UserSet(value_ + (event.position.y < Bounds().h / 2 ? Step() : -Step()));
        }
        break;
    case EventType::Wheel:
    {
        const f64 before = value_;
        UserSet(value_ - static_cast<f64>(event.wheel_y) * Step());
        event.handled = value_ !=
                        before;  // at the end of its range the wheel goes on to the scroll view under the spin box
        break;
    }
    case EventType::KeyDown:
        switch (event.key)
        {
        case Key::Up:
            UserSet(value_ + Step());
            event.handled = true;
            break;
        case Key::Down:
            UserSet(value_ - Step());
            event.handled = true;
            break;
        case Key::PageUp:
            UserSet(value_ + Step() * kPageSteps);
            event.handled = true;
            break;
        case Key::PageDown:
            UserSet(value_ - Step() * kPageSteps);
            event.handled = true;
            break;
        default: break;
        }
        break;
    default: break;
    }
}

}  // namespace engine_placeholder::ui
