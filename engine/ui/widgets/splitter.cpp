#include "ui/widgets/splitter.h"

#include "lang/scalar.h"

#include "log/assert.h"
#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui
{

Splitter::Splitter(Orientation orientation, StringView name) : Widget(name), orientation_(orientation) {}

usize Splitter::AddPane(UniquePtr<Widget> pane, f32 weight)
{
    ENGINE_ASSERT(pane.Get() != nullptr, "Splitter::AddPane: the pane is null");
    ENGINE_ASSERT(weight > 0, "Splitter::AddPane: the weight must be above 0");
    PaneInfo info;
    info.widget = AddChild(static_cast<UniquePtr<Widget>&&>(pane));
    info.fraction = weight;
    info.initial = weight;
    panes_.Push(info);
    MarkDirty(kLayoutDirty);
    return panes_.Size() - 1;
}

Widget* Splitter::Pane(usize index) const
{
    ENGINE_ASSERT(index < panes_.Size(), "Splitter::Pane: index out of range");
    return panes_[index].widget;
}

void Splitter::SetMinSize(usize index, f32 min)
{
    ENGINE_ASSERT(index < panes_.Size(), "Splitter::SetMinSize: index out of range");
    panes_[index].min = min < 0 ? 0.0f : min;
    MarkDirty(kLayoutDirty);
}

void Splitter::SetCollapsible(usize index, bool collapsible)
{
    ENGINE_ASSERT(index < panes_.Size(), "Splitter::SetCollapsible: index out of range");
    panes_[index].collapsible = collapsible;
}

void Splitter::SetPaneCollapsed(usize index, bool collapsed)
{
    ENGINE_ASSERT(index < panes_.Size(), "Splitter::SetPaneCollapsed: index out of range");
    if (panes_[index].collapsed == collapsed)
    {
        return;
    }
    panes_[index].collapsed = collapsed;
    MarkDirty(kLayoutDirty);
}

void Splitter::Fractions(Vector<f32>* out) const
{
    out->Clear();
    f32 sum = 0;
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        if (!panes_[i].collapsed)
        {
            sum += panes_[i].fraction;
        }
    }
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        out->Push(sum > 0 ? panes_[i].fraction / sum : 0.0f);
    }
}

void Splitter::SetFractions(Span<const f32> fractions)
{
    if (fractions.Size() != panes_.Size())
    {
        return;
    }
    for (usize i = 0; i < fractions.Size(); ++i)
    {
        if (!(fractions[i] > 0))
        {
            return;
        }
    }
    for (usize i = 0; i < fractions.Size(); ++i)
    {
        panes_[i].fraction = fractions[i];
    }
    MarkDirty(kLayoutDirty);
}

void Splitter::Reset()
{
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        panes_[i].fraction = panes_[i].initial;
        panes_[i].collapsed = false;
    }
    MarkDirty(kLayoutDirty);
}

f32 Splitter::Room() const
{
    const f32 room = Length() - kHandle * static_cast<f32>(panes_.Size() > 0 ? panes_.Size() - 1 : 0);
    return room > 0 ? room : 0.0f;
}

void Splitter::Solve()
{
    // Sizes in proportion to the fractions of the open panes, then the minimums: a pane under its minimum gets it and
    // the others share what is left.
    const f32 room = Room();
    Vector<u8> fixed;
    fixed.Resize(panes_.Size());
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        panes_[i].size = 0;
        fixed[i] = panes_[i].collapsed ? 1 : 0;
    }
    f32 left = room;
    for (usize round = 0; round <= panes_.Size(); ++round)
    {
        f32 sum = 0;
        for (usize i = 0; i < panes_.Size(); ++i)
        {
            if (fixed[i] == 0)
            {
                sum += panes_[i].fraction;
            }
        }
        if (sum <= 0)
        {
            break;
        }
        bool clamped = false;
        for (usize i = 0; i < panes_.Size() && !clamped; ++i)
        {
            if (fixed[i] != 0)
            {
                continue;
            }
            if (left * panes_[i].fraction / sum < panes_[i].min)
            {
                panes_[i].size = panes_[i].min;
                fixed[i] = 1;
                left -= panes_[i].min;
                if (left < 0)
                {
                    left = 0;
                }
                clamped = true;
            }
        }
        if (clamped)
        {
            continue;
        }
        for (usize i = 0; i < panes_.Size(); ++i)
        {
            if (fixed[i] == 0)
            {
                panes_[i].size = left * panes_[i].fraction / sum;
            }
        }
        break;
    }
}

f32 Splitter::StartOf(usize pane) const
{
    f32 x = 0;
    for (usize i = 0; i < pane; ++i)
    {
        x += panes_[i].size + kHandle;
    }
    return x;
}

Rect Splitter::HandleRect(usize index) const
{
    if (index + 1 >= panes_.Size())
    {
        return Rect();
    }
    const f32 at = StartOf(index) + panes_[index].size;
    const Rect& b = Bounds();
    return Horizontal() ? Rect{b.x + at, b.y, kHandle, b.h} : Rect{b.x, b.y + at, b.w, kHandle};
}

Size Splitter::Measure(const Constraints& constraints)
{
    f32 along = kHandle * static_cast<f32>(panes_.Size() > 0 ? panes_.Size() - 1 : 0);
    f32 across = 0;
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        if (panes_[i].collapsed)
        {
            continue;
        }
        const Size s = panes_[i].widget->MeasureTotal(constraints);
        along += FMax(Horizontal() ? s.w : s.h, panes_[i].min);
        across = FMax(across, Horizontal() ? s.h : s.w);
    }
    return Horizontal() ? Size{along, across} : Size{across, along};
}

void Splitter::Arrange(const Rect& rect)
{
    SetBounds(rect);
    Solve();
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        PaneInfo& p = panes_[i];
        p.widget->SetCollapsed(p.collapsed);
        if (p.collapsed)
        {
            continue;
        }
        const f32 at = StartOf(i);
        p.widget->Arrange(Horizontal() ? Rect{rect.x + at, rect.y, p.size, rect.h}
                                       : Rect{rect.x, rect.y + at, rect.w, p.size});
    }
}

usize Splitter::HandleAt(Point local) const
{
    const f32 along = Horizontal() ? local.x : local.y;
    const f32 across = Horizontal() ? local.y : local.x;
    if (across < 0 || across >= (Horizontal() ? Bounds().h : Bounds().w))
    {
        return kNone;
    }
    for (usize i = 0; i + 1 < panes_.Size(); ++i)
    {
        const f32 at = StartOf(i) + panes_[i].size;
        if (along >= at && along < at + kHandle)
        {
            return i;
        }
    }
    return kNone;
}

void Splitter::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    for (usize i = 0; i + 1 < panes_.Size(); ++i)
    {
        u32 state = 0;
        if (i == drag_)
        {
            state = kStatePressed;
        }
        else if (i == hover_ && drag_ == kNone)
        {
            state = kStateHover;
        }
        DrawStyleBox(canvas, HandleRect(i),
                     surface->Themes().Resolve(widget_kind::kSplitter, VariantId(0), state, scale), scale);
    }
}

void Splitter::UpdateFractions()
{
    const f32 room = Room();
    if (room <= 0)
    {
        return;
    }
    for (usize i = 0; i < panes_.Size(); ++i)
    {
        if (!panes_[i].collapsed)
        {
            panes_[i].fraction = panes_[i].size / room > 0 ? panes_[i].size / room : panes_[i].fraction;
        }
    }
}

void Splitter::DragTo(f32 boundary)
{
    // The boundary after pane `drag_`, in splitter coordinates. The two panes beside the handle share their total; the
    // others keep their pixel sizes.
    const usize a = drag_;
    const usize b = drag_ + 1;
    PaneInfo& pa = panes_[a];
    PaneInfo& pb = panes_[b];
    const f32 start = StartOf(a);
    const f32 total = pa.size + pb.size;
    f32 want = boundary - start;  // the size pane `a` would have
    want = FMin(FMax(want, 0.0f), total);
    const bool collapse_a = pa.collapsible && want < pa.min * kCollapseFraction;
    const bool collapse_b = pb.collapsible && (total - want) < pb.min * kCollapseFraction;
    if (collapse_a && !collapse_b)
    {
        pa.collapsed = true;
        pb.collapsed = false;
        pb.size = total;
    }
    else if (collapse_b && !collapse_a)
    {
        pb.collapsed = true;
        pa.collapsed = false;
        pa.size = total;
    }
    else
    {
        pa.collapsed = false;
        pb.collapsed = false;
        const f32 lo = FMin(pa.min, total);
        const f32 hi = FMax(total - pb.min, lo);
        pa.size = FMin(FMax(want, lo), hi);
        pb.size = total - pa.size;
    }
    // Panes that were collapsed and are open again get their size from the pair; every open pane's fraction follows its
    // size.
    UpdateFractions();
    MarkDirty(kLayoutDirty);
}

void Splitter::OnEvent(Event& event)
{
    const f32 along = Horizontal() ? event.position.x : event.position.y;
    switch (event.type)
    {
    case EventType::PointerMove:
    {
        if (drag_ != kNone && (State() & kStatePressed) == 0)
        {
            drag_ = kNone;  // the capture ended without a release
        }
        if (drag_ != kNone)
        {
            DragTo(along - grab_);
            on_changed_.Invoke(this);
            event.handled = true;
            break;
        }
        const usize h = HandleAt(event.position);
        if (h != hover_)
        {
            hover_ = h;
            MarkDirty(kDrawDirty);
        }
        if (h != kNone)
        {
            SetCursor(Horizontal() ? os::CursorShape::ResizeHorizontal : os::CursorShape::ResizeVertical);
        }
        else
        {
            ClearCursor();
        }
        break;
    }
    case EventType::PointerLeave:
        if (hover_ != kNone)
        {
            hover_ = kNone;
            MarkDirty(kDrawDirty);
        }
        ClearCursor();
        break;
    case EventType::PointerDown:
    {
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        const usize h = HandleAt(event.position);
        if (h == kNone)
        {
            break;
        }
        event.handled = true;
        const bool near = Abs(event.position.x - last_press_.x) <= kClickSlop &&
                          Abs(event.position.y - last_press_.y) <= kClickSlop;
        if (event.time - last_press_time_ <= kDoubleClickSeconds && near)
        {
            last_press_time_ = -1e9;
            Reset();
            on_changed_.Invoke(this);
            break;
        }
        last_press_time_ = event.time;
        last_press_ = event.position;
        drag_ = h;
        grab_ = along - (StartOf(h) + panes_[h].size);  // where in the handle the pointer took hold
        MarkDirty(kDrawDirty);
        break;
    }
    case EventType::PointerUp:
        if (event.button != os::kMouseLeft || drag_ == kNone)
        {
            break;
        }
        event.handled = true;
        drag_ = kNone;
        hover_ = HandleAt(event.position);
        MarkDirty(kDrawDirty);
        break;
    default: break;
    }
}

}  // namespace engine_placeholder::ui
