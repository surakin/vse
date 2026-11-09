#include "ui/widgets/scroll.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

// ---- ScrollBar ----

void ScrollBar::SetRange(f64 content, f64 view) {
    content_ = content < 0 ? 0 : content;
    view_ = view < 0 ? 0 : view;
    if (value_ > MaxValue()) value_ = MaxValue();
    MarkDirty(kDrawDirty);
}

void ScrollBar::SetValue(f64 value) {
    if (!(value > 0)) value = 0;
    if (value > MaxValue()) value = MaxValue();
    if (value == value_) return;
    value_ = value;
    MarkDirty(kDrawDirty);
}

void ScrollBar::SetArrows(bool arrows) {
    if (arrows_ == arrows) return;
    arrows_ = arrows;
    MarkDirty(kLayoutDirty);
}

void ScrollBar::SetAutoHide(bool auto_hide) {
    auto_hide_ = auto_hide;
    MarkDirty(kDrawDirty);
}

ScrollBar::Geometry ScrollBar::Measure2() const {
    Geometry g;
    const Rect& b = Bounds();
    const f32 along = vertical_ ? b.h : b.w;
    const f32 across = vertical_ ? b.w : b.h;
    g.arrow = arrows_ ? across : 0.0f;
    if (g.arrow * 2 > along) g.arrow = 0;
    g.start = g.arrow;
    g.length = along - g.arrow * 2;
    const Surface* surface = GetSurface();
    const f32 min_thumb = surface != nullptr ? const_cast<Surface*>(surface)->Themes().Metric(MetricId::MinHitSize, 1.0f) : 0.0f;
    f32 thumb = g.length;
    if (content_ > view_ && content_ > 0) thumb = g.length * static_cast<f32>(view_ / content_);
    const f32 floor_len = FMin(min_thumb, g.length);
    if (thumb < floor_len) thumb = floor_len;
    if (thumb > g.length) thumb = g.length;
    g.thumb = thumb;
    const f64 max_value = MaxValue();
    g.thumb_pos = g.start + (max_value > 0 ? (g.length - thumb) * static_cast<f32>(value_ / max_value) : 0.0f);
    return g;
}

Rect ScrollBar::ThumbRect() const {
    const Geometry g = Measure2();
    const Rect& b = Bounds();
    Rect r = b;
    if (vertical_) {
        r.y = b.y + g.thumb_pos;
        r.h = g.thumb;
    } else {
        r.x = b.x + g.thumb_pos;
        r.w = g.thumb;
    }
    return r;
}

bool ScrollBar::WantsUpdate() const {
    const Surface* surface = GetSurface();
    return auto_hide_ && surface != nullptr && content_ > view_ && surface->Time() - changed_at_ < kFadeSeconds + 0.25f; // a little longer than the fade, so that the frame that hides the bar is painted
}

bool ScrollBar::Visible2() const {
    if (!auto_hide_) return true;
    if (!(content_ > view_)) return false;
    const Surface* surface = GetSurface();
    return dragging_ || (State() & kStateHover) != 0 || (surface != nullptr && surface->Time() - changed_at_ < kFadeSeconds);
}

Size ScrollBar::Measure(const Constraints&) {
    Surface* surface = GetSurface();
    const f32 thickness = surface != nullptr ? surface->Themes().Metric(MetricId::ScrollbarThickness, 1.0f) : 0.0f;
    return vertical_ ? Size{thickness, 0} : Size{0, thickness};
}

void ScrollBar::Change(f64 value) {
    if (!(value > 0)) value = 0;
    if (value > MaxValue()) value = MaxValue();
    if (value == value_) return;
    value_ = value;
    changed_at_ = GetSurface() != nullptr ? GetSurface()->Time() : 0.0;
    MarkDirty(kDrawDirty);
    on_changed_.Invoke(this);
}

void ScrollBar::Draw(Canvas& canvas) {
    if (!Visible2()) return;
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const Rect& b = Bounds();
    const f32 round = surface->Themes().Metric(MetricId::CornerRadius, 1.0f);
    canvas.DrawRoundedRect(b, CornerRadii(round), surface->Themes().Color(ColorId::Panel));
    const Geometry g = Measure2();
    if (g.arrow > 0) {
        const u32 ink = surface->Themes().Color(ColorId::Text);
        const f32 inset = g.arrow * 0.3f;
        const f32 far = g.arrow * 0.7f;
        const f32 mid = g.arrow * 0.5f;
        if (vertical_) {
            canvas.DrawTriangle(Point{b.x + mid, b.y + inset}, Point{b.x + far, b.y + far}, Point{b.x + inset, b.y + far}, ink);
            const f32 end = b.h - g.arrow;
            canvas.DrawTriangle(Point{b.x + inset, b.y + end + inset}, Point{b.x + far, b.y + end + inset}, Point{b.x + mid, b.y + end + far}, ink);
        } else {
            canvas.DrawTriangle(Point{b.x + far, b.y + inset}, Point{b.x + far, b.y + far}, Point{b.x + inset, b.y + mid}, ink);
            const f32 end = b.w - g.arrow;
            canvas.DrawTriangle(Point{b.x + end + inset, b.y + inset}, Point{b.x + end + far, b.y + mid}, Point{b.x + end + inset, b.y + far}, ink);
        }
    }
    u32 state = 0;
    if (dragging_) state |= kStatePressed;
    if ((State() & kStateHover) != 0) state |= kStateHover;
    const ResolvedStyle thumb_style = surface->Themes().Resolve(widget_kind::kScrollbar, VariantId(0), state, scale);
    DrawStyleBox(canvas, ThumbRect(), thumb_style, scale);
}

void ScrollBar::OnEvent(Event& event) {
    if (event.type == EventType::PointerDown && event.button == os::kMouseLeft) {
        event.handled = true;
        const Geometry g = Measure2();
        const f32 along = Along(event.position);
        const f32 length = vertical_ ? Bounds().h : Bounds().w;
        if (g.arrow > 0 && along < g.arrow) {
            Change(value_ - LineStep());
        } else if (g.arrow > 0 && along >= length - g.arrow) {
            Change(value_ + LineStep());
        } else if (along >= g.thumb_pos && along < g.thumb_pos + g.thumb) {
            dragging_ = true;
            grab_ = along - g.thumb_pos;
            MarkDirty(kDrawDirty);
        } else {
            Change(along < g.thumb_pos ? value_ - view_ : value_ + view_);
        }
    } else if (event.type == EventType::PointerMove && dragging_) {
        event.handled = true;
        if ((State() & kStatePressed) == 0) {
            dragging_ = false; // the capture ended without a release
            return;
        }
        const Geometry g = Measure2();
        const f32 room = g.length - g.thumb;
        if (room > 0) Change(MaxValue() * static_cast<f64>((Along(event.position) - grab_ - g.start) / room));
    } else if (event.type == EventType::PointerUp && event.button == os::kMouseLeft) {
        event.handled = true;
        dragging_ = false;
        MarkDirty(kDrawDirty);
    }
}

// ---- ScrollView ----

ScrollView::ScrollView(StringView name) : Widget(name) {
    vbar_ = static_cast<ScrollBar*>(AddChild(MakeUnique<ScrollBar>(StringView("vbar"), true)));
    hbar_ = static_cast<ScrollBar*>(AddChild(MakeUnique<ScrollBar>(StringView("hbar"), false)));
    vbar_->OnChanged().Connect(&ScrollView::BarMoved, this);
    hbar_->OnChanged().Connect(&ScrollView::BarMoved, this);
    vbar_->SetCollapsed(true);
    hbar_->SetCollapsed(true);
}

void ScrollView::BarMoved(void* user, ScrollBar* bar) {
    ScrollView* view = static_cast<ScrollView*>(user);
    if (bar == view->vbar_) {
        view->offset_.y = static_cast<f32>(bar->Value());
    } else {
        view->offset_.x = static_cast<f32>(bar->Value());
    }
    view->MarkDirty(kLayoutDirty);
}

Widget* ScrollView::SetContent(UniquePtr<Widget> content) {
    if (content_ != nullptr) RemoveChild(content_);
    content_ = AddChild(static_cast<UniquePtr<Widget>&&>(content));
    vbar_->BringToFront();
    hbar_->BringToFront();
    return content_;
}

void ScrollView::SetPolicy(ScrollPolicy horizontal, ScrollPolicy vertical) {
    h_policy_ = horizontal;
    v_policy_ = vertical;
    MarkDirty(kLayoutDirty);
}

f32 ScrollView::Step() const {
    if (line_step_ > 0) return line_step_;
    const Surface* surface = GetSurface();
    return surface != nullptr ? const_cast<Surface*>(surface)->Themes().Metric(MetricId::MinHitSize, 1.0f) : 1.0f;
}

void ScrollView::SetOffset(Point offset) {
    const f32 max_x = FMax(content_size_.w - viewport_.w, 0.0f);
    const f32 max_y = FMax(content_size_.h - viewport_.h, 0.0f);
    offset.x = FMin(FMax(offset.x, 0.0f), max_x);
    offset.y = FMin(FMax(offset.y, 0.0f), max_y);
    if (offset.x == offset_.x && offset.y == offset_.y) return;
    offset_ = offset;
    MarkDirty(kLayoutDirty);
}

Size ScrollView::Measure(const Constraints&) { return Size(); }

void ScrollView::Arrange(const Rect& rect) {
    SetBounds(rect);
    Surface* surface = GetSurface();
    const f32 thickness = surface != nullptr ? surface->Themes().Metric(MetricId::ScrollbarThickness, 1.0f) : 0.0f;
    bool v_shown = v_policy_ == ScrollPolicy::Always;
    bool h_shown = h_policy_ == ScrollPolicy::Always;
    Rect view = rect;
    Size content = Size();
    auto compute = [&]() {
        view.w = FMax(rect.w - (v_shown ? thickness : 0.0f), 0.0f);
        view.h = FMax(rect.h - (h_shown ? thickness : 0.0f), 0.0f);
        content = Size{view.w, view.h};
        if (content_ == nullptr || content_->Collapsed()) return;
        Constraints c;
        if (h_policy_ == ScrollPolicy::Never) c.max_w = view.w;
        if (v_policy_ == ScrollPolicy::Never) c.max_h = view.h;
        const Size pref = content_->MeasureTotal(c);
        if (h_policy_ != ScrollPolicy::Never) content.w = FMax(pref.w, view.w);
        if (v_policy_ != ScrollPolicy::Never) content.h = FMax(pref.h, view.h);
    };
    for (int pass = 0; pass < 2; ++pass) {
        compute();
        const bool v_next = v_policy_ == ScrollPolicy::Always || (v_policy_ == ScrollPolicy::Auto && content.h > view.h);
        const bool h_next = h_policy_ == ScrollPolicy::Always || (h_policy_ == ScrollPolicy::Auto && content.w > view.w);
        if (v_next == v_shown && h_next == h_shown) break;
        v_shown = v_next;
        h_shown = h_next;
    }
    compute();
    view.x = rect.x;
    view.y = rect.y;
    viewport_ = view;
    content_size_ = content;
    offset_.x = FMin(FMax(offset_.x, 0.0f), FMax(content.w - view.w, 0.0f));
    offset_.y = FMin(FMax(offset_.y, 0.0f), FMax(content.h - view.h, 0.0f));
    if (content_ != nullptr && !content_->Collapsed()) {
        Rect at;
        at.x = rect.x - offset_.x;
        at.y = rect.y - offset_.y;
        at.w = content.w;
        at.h = content.h;
        content_->Arrange(at);
    }
    vbar_->SetCollapsed(!v_shown);
    hbar_->SetCollapsed(!h_shown);
    if (v_shown) {
        Rect r;
        r.x = rect.Right() - thickness;
        r.y = rect.y;
        r.w = thickness;
        r.h = view.h;
        vbar_->Arrange(r);
        vbar_->SetLineStep(Step());
        vbar_->SetRange(content.h, view.h);
        vbar_->SetValue(offset_.y);
    }
    if (h_shown) {
        Rect r;
        r.x = rect.x;
        r.y = rect.Bottom() - thickness;
        r.w = view.w;
        r.h = thickness;
        hbar_->Arrange(r);
        hbar_->SetLineStep(Step());
        hbar_->SetRange(content.w, view.w);
        hbar_->SetValue(offset_.x);
    }
}

void ScrollView::OnEvent(Event& event) {
    if (event.type != EventType::Wheel) return;
    Point next = offset_;
    next.x += event.wheel_x * Step();
    next.y += event.wheel_y * Step();
    const Point before = offset_;
    SetOffset(next);
    if (offset_.x != before.x || offset_.y != before.y) event.handled = true; // at the end of the content the wheel goes on to an outer view
}

bool ScrollView::DragAutoScroll(Point position, f32 dt) {
    const Point next(offset_.x + DragScrollDelta(position.x - (viewport_.x - Bounds().x), viewport_.w, dt),
                     offset_.y + DragScrollDelta(position.y - (viewport_.y - Bounds().y), viewport_.h, dt));
    if (next.x == offset_.x && next.y == offset_.y) return false;
    const Point before = offset_;
    SetOffset(next);
    return offset_.x != before.x || offset_.y != before.y;
}

bool ScrollView::ScrollTo(const Rect& rect) {
    Point next = offset_;
    if (rect.x < viewport_.x) {
        next.x -= viewport_.x - rect.x;
    } else if (rect.Right() > viewport_.Right()) {
        next.x += FMin(rect.Right() - viewport_.Right(), rect.x - viewport_.x);
    }
    if (rect.y < viewport_.y) {
        next.y -= viewport_.y - rect.y;
    } else if (rect.Bottom() > viewport_.Bottom()) {
        next.y += FMin(rect.Bottom() - viewport_.Bottom(), rect.y - viewport_.y);
    }
    const Point before = offset_;
    SetOffset(next);
    return offset_.x != before.x || offset_.y != before.y;
}

bool ScrollView::ScrollTo(const Widget* widget) { return widget != nullptr && ScrollTo(widget->Bounds()); }

void ScrollView::OnDescendantFocused(Widget* widget) {
    for (Widget* w = widget; w != nullptr; w = w->Parent()) {
        if (w == content_) {
            // Lay the content out again at once: an outer view that is asked next (it is an ancestor of this one) must see the
            // widget where this scroll put it, not where it was.
            if (ScrollTo(widget)) Arrange(Bounds());
            return;
        }
    }
}

} // namespace engine_placeholder::ui
