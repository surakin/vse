#include "ui/surface.h"

#include <cmath>

#include "algorithm.h"
#include "log/assert.h"
#include "ui/draw_list_canvas.h"

namespace engine_placeholder::ui {

namespace {

bool IsPointer(EventType t) { return t == EventType::PointerMove || t == EventType::PointerDown || t == EventType::PointerUp || t == EventType::Wheel; }

} // namespace

Surface::Surface(f32 width, f32 height, f32 scale) : width_(width), height_(height), scale_(scale) {
    ENGINE_ASSERT(scale > 0, "Surface: the scale must be above 0");
    root_ = MakeUnique<Widget>(StringView("root"));
    popups_ = MakeUnique<Widget>(StringView("popups"));
    popups_->SetPassThrough(true);
    popups_->SetClipsChildren(false);
    root_->AttachSurface(this);
    popups_->AttachSurface(this);
    Rect all;
    all.w = width;
    all.h = height;
    root_->SetBounds(all);
    popups_->SetBounds(all);
}

Surface::~Surface() {
    root_.Reset();
    popups_.Reset();
}

void Surface::Resize(f32 width, f32 height, f32 scale) {
    ENGINE_ASSERT(scale > 0, "Surface::Resize: the scale must be above 0");
    if (width == width_ && height == height_ && scale == scale_) return;
    width_ = width;
    height_ = height;
    scale_ = scale;
    Rect all;
    all.w = width;
    all.h = height;
    root_->SetBounds(all);
    popups_->SetBounds(all);
    NoteDirty(kLayoutDirty | kDrawDirty);
}

void Surface::SetTheme(const Theme* theme) {
    const bool metrics_changed = themes_.SetTheme(theme);
    NoteDirty(metrics_changed ? (kLayoutDirty | kDrawDirty) : kDrawDirty);
    root_->MarkDirty(metrics_changed ? (kLayoutDirty | kDrawDirty) : kDrawDirty);
}

void Surface::SetFontProvider(FontProvider* provider) {
    fonts_ = provider;
    root_->MarkDirty(kLayoutDirty | kDrawDirty);
}

void Surface::NoteDirty(u32 flags) {
    if ((flags & kLayoutDirty) != 0) {
        layout_dirty_ = true;
        ++layout_generation_;
    }
    draw_dirty_ = true;
}

void Surface::WidgetDestroyed(Widget* widget) {
    if (focus_ == widget) focus_ = nullptr;
    if (capture_ == widget) capture_ = nullptr;
    if (hover_ == widget) hover_ = nullptr;
    if (saved_focus_ == widget) saved_focus_ = nullptr;
    for (usize i = removals_.Size(); i > 0; --i) {
        if (removals_[i - 1] == widget) removals_.Remove(i - 1);
    }
}

void Surface::QueueRemoval(Widget* widget) {
    for (usize i = 0; i < removals_.Size(); ++i) {
        if (removals_[i] == widget) return;
    }
    removals_.Push(widget);
}

void Surface::FlushRemovals() {
    // A removed widget takes its queued descendants with it (WidgetDestroyed drops them from the queue), so every entry left is alive.
    while (!removals_.Empty()) {
        Widget* w = removals_[removals_.Size() - 1];
        removals_.Pop();
        if (w->parent_ != nullptr) w->parent_->RemoveChild(w);
    }
}

Widget* Surface::HitTestAll(Point point) {
    if (Widget* hit = popups_->HitTest(point)) return hit;
    return root_->HitTest(point);
}

void Surface::Post(const Event& event) { queue_.Push(event); }

bool Surface::Deliver(Widget* target, Event event, bool bubble) {
    event.time = time_;
    const Point surface_position = event.position;
    for (Widget* w = target; w != nullptr; w = w->parent_) {
        const bool always = event.type == EventType::PointerEnter || event.type == EventType::PointerLeave || event.type == EventType::FocusGained ||
                            event.type == EventType::FocusLost;
        if (!always && !w->EffectivelyEnabled()) break; // a disabled widget takes the event and does nothing with it
        Event local = event;
        local.position.x = surface_position.x - w->Bounds().x;
        local.position.y = surface_position.y - w->Bounds().y;
        w->OnEvent(local);
        if (local.handled) return true;
        if (!bubble) break;
    }
    return false;
}

void Surface::SetHover(Widget* widget, const Event& cause) {
    if (widget == hover_) return;
    Widget* old = hover_;
    hover_ = widget;
    Event e = cause;
    if (old != nullptr) {
        old->state_ &= ~kStateHover;
        old->MarkDirty(kDrawDirty);
        e.type = EventType::PointerLeave;
        Deliver(old, e, false);
    }
    if (widget != nullptr && hover_ == widget) {
        widget->state_ |= kStateHover;
        widget->MarkDirty(kDrawDirty);
        e.type = EventType::PointerEnter;
        Deliver(widget, e, false);
    }
}

void Surface::SetFocus(Widget* widget) {
    if (widget == focus_) return;
    Widget* old = focus_;
    focus_ = widget;
    ++depth_;
    Event e;
    if (old != nullptr) {
        old->state_ &= ~kStateFocused;
        old->MarkDirty(kDrawDirty);
        e.type = EventType::FocusLost;
        Deliver(old, e, false);
    }
    if (widget != nullptr && focus_ == widget) {
        if (window_focused_) widget->state_ |= kStateFocused; // a window without the focus shows no focus ring; it returns with WindowFocusGained
        widget->MarkDirty(kDrawDirty);
        e.type = EventType::FocusGained;
        Deliver(widget, e, false);
    }
    if (--depth_ == 0) FlushRemovals();
}

void Surface::SetCapture(Widget* widget) {
    if (capture_ != nullptr && capture_ != widget && (capture_->state_ & kStatePressed) != 0) {
        capture_->state_ &= ~kStatePressed;
        capture_->MarkDirty(kDrawDirty);
    }
    capture_ = widget;
}

void Surface::ReleasePointer() {
    buttons_down_ = 0;
    if (capture_ != nullptr) {
        capture_->state_ &= ~kStatePressed;
        capture_->MarkDirty(kDrawDirty);
        capture_ = nullptr;
    }
}

void Surface::SetWindowFocus(bool focused) {
    if (focused == window_focused_) return;
    window_focused_ = focused;
    Event e;
    if (!focused) {
        ReleasePointer();
        modifiers_ = 0;
        pointer_inside_ = false;
        SetHover(nullptr, e);
        for (usize i = popups_->ChildCount(); i > 0; --i) popups_->RemoveChild(popups_->ChildAt(i - 1));
    }
    if (focus_ != nullptr) {
        if (focused) {
            focus_->state_ |= kStateFocused;
        } else {
            focus_->state_ &= ~kStateFocused;
        }
        focus_->MarkDirty(kDrawDirty);
        e.type = focused ? EventType::FocusGained : EventType::FocusLost;
        Deliver(focus_, e, false);
    }
}

namespace {

// Appends the widgets under `widget` that Tab may stop at, in tree order.
void CollectTabStops(Widget* widget, Vector<Widget*>& out) {
    if (!widget->Shown()) return;
    if (widget->Focusable() && widget->TabIndex() >= 0 && widget->EffectivelyEnabled()) out.Push(widget);
    for (usize i = 0; i < widget->ChildCount(); ++i) CollectTabStops(widget->ChildAt(i), out);
}

} // namespace

bool Surface::FocusNext(bool backwards) {
    Vector<Widget*> stops;
    CollectTabStops(popups_->ChildCount() > 0 ? popups_.Get() : root_.Get(), stops);
    if (stops.Empty()) return false;
    // Positive indices first (ascending), then the zeros; the sort is stable, so the tree order breaks ties.
    StableSort(Span<Widget*>(stops.Data(), stops.Size()), [](Widget* a, Widget* b) {
        const i32 ka = a->TabIndex() > 0 ? a->TabIndex() : 0x7fffffff;
        const i32 kb = b->TabIndex() > 0 ? b->TabIndex() : 0x7fffffff;
        return ka < kb;
    });
    usize at = stops.Size();
    for (usize i = 0; i < stops.Size(); ++i) {
        if (stops[i] == focus_) at = i;
    }
    usize next;
    if (at == stops.Size()) {
        next = backwards ? stops.Size() - 1 : 0;
    } else if (backwards) {
        next = at == 0 ? stops.Size() - 1 : at - 1;
    } else {
        next = at + 1 == stops.Size() ? 0 : at + 1;
    }
    SetFocus(stops[next]);
    return focus_ == stops[next];
}

os::CursorShape Surface::RequestedCursor() const {
    for (const Widget* w = capture_ != nullptr ? capture_ : hover_; w != nullptr; w = w->Parent()) {
        if (w->HasCursor()) return w->Cursor();
    }
    return os::CursorShape::Arrow;
}

void Surface::RefreshHover() {
    if (!pointer_inside_) return;
    Event e;
    e.type = EventType::PointerMove;
    e.position = pointer_position_;
    e.time = time_;
    ++depth_;
    SetHover(HitTestAll(pointer_position_), e);
    if (--depth_ == 0) FlushRemovals();
}

void Surface::TrackPopups() {
    const usize n = popups_->ChildCount();
    if (n == popup_count_) return;
    const usize before = popup_count_;
    popup_count_ = n;
    if (before == 0) {
        saved_focus_ = focus_;
    } else if (n == 0) {
        Widget* back = saved_focus_;
        saved_focus_ = nullptr;
        // Give the focus back unless the program has moved it to something that is still alive outside the popups.
        if (back != nullptr && focus_ == nullptr && back->Shown() && back->EffectivelyEnabled()) SetFocus(back);
    }
}

void Surface::Dispatch(Event e) {
    e.time = time_;
    ++depth_;
    TrackPopups();
    if (e.type == EventType::WindowFocusGained || e.type == EventType::WindowFocusLost) {
        SetWindowFocus(e.type == EventType::WindowFocusGained);
    } else if (e.type == EventType::PointerLeave) {
        pointer_inside_ = false;
        SetHover(nullptr, e); // posted to the surface: the pointer left it altogether
    } else if (IsPointer(e.type)) {
        modifiers_ = e.modifiers;
        pointer_position_ = e.position;
        pointer_inside_ = true;
        Widget* hit = HitTestAll(e.position);
        switch (e.type) {
            case EventType::PointerMove: {
                SetHover(hit, e);
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                break;
            }
            case EventType::PointerDown: {
                SetHover(hit, e);
                if (capture_ == nullptr) {
                    // Click to focus: the nearest focusable widget under the pointer takes the keyboard focus, and a click on nothing focusable clears it.
                    Widget* focus_target = hit;
                    while (focus_target != nullptr && !(focus_target->Focusable() && focus_target->EffectivelyEnabled())) focus_target = focus_target->Parent();
                    SetFocus(focus_target);
                    if (hit != nullptr) {
                        capture_ = hit;
                        hit->state_ |= kStatePressed;
                        hit->MarkDirty(kDrawDirty);
                    }
                }
                buttons_down_ |= e.button;
                if (capture_ != nullptr) Deliver(capture_, e, true);
                break;
            }
            case EventType::PointerUp: {
                buttons_down_ &= ~e.button;
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                if (buttons_down_ == 0 && capture_ != nullptr) {
                    capture_->state_ &= ~kStatePressed;
                    capture_->MarkDirty(kDrawDirty);
                    capture_ = nullptr;
                }
                SetHover(HitTestAll(e.position), e);
                break;
            }
            default: {
                // The wheel goes to the hovered widget (the capturer during a drag) and bubbles to the first ancestor that handles it; a popup is hit first, so nothing behind it sees it.
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                break;
            }
        }
    } else if (e.type == EventType::KeyDown || e.type == EventType::KeyUp || e.type == EventType::Text) {
        modifiers_ = e.modifiers;
        if (window_focused_ && focus_ != nullptr && (e.type != EventType::Text || focus_->AcceptsText())) {
            const bool handled = Deliver(focus_, e, true);
            if (!handled && e.type == EventType::KeyDown && e.key == Key::Tab && (e.modifiers & (os::kModCtrl | os::kModAlt)) == 0) FocusNext((e.modifiers & os::kModShift) != 0);
        } else if (window_focused_ && focus_ == nullptr && e.type == EventType::KeyDown && e.key == Key::Tab && (e.modifiers & (os::kModCtrl | os::kModAlt)) == 0) {
            FocusNext((e.modifiers & os::kModShift) != 0);
        }
    }
    if (--depth_ == 0) FlushRemovals();
    TrackPopups();
}

void Surface::UpdateWidgets(Widget* widget, f32 dt) {
    if (!widget->Shown()) return;
    if (widget->WantsUpdate()) widget->Update(dt);
    for (usize i = 0; i < widget->ChildCount(); ++i) UpdateWidgets(widget->ChildAt(i), dt);
}

bool Surface::Frame(f32 dt, DrawList* list, text::GlyphAtlas* atlas) {
    ENGINE_ASSERT(dt >= 0, "Surface::Frame: dt must not be negative");
    time_ += dt;
    ++depth_;
    Vector<Event> events = static_cast<Vector<Event>&&>(queue_);
    queue_ = Vector<Event>();
    for (usize i = 0; i < events.Size(); ++i) Dispatch(events[i]);
    UpdateWidgets(root_.Get(), dt);
    UpdateWidgets(popups_.Get(), dt);
    if (--depth_ == 0) FlushRemovals();
    TrackPopups();

    if (layout_dirty_) {
        // At most two passes: the second one is for a widget whose size depends on the width it was given (it invalidates layout when that changes).
        Rect all;
        all.w = width_;
        all.h = height_;
        for (int pass = 0; pass < 2; ++pass) {
            layout_dirty_ = false;
            root_->ClearDirtyTree(kLayoutDirty);
            popups_->ClearDirtyTree(kLayoutDirty);
            ++layout_generation_;
            root_->Arrange(all);
            popups_->Arrange(all);
            if (!layout_dirty_) break;
        }
        if (layout_dirty_) {
            ++layout_overruns_;
            layout_dirty_ = false;
            root_->ClearDirtyTree(kLayoutDirty);
            popups_->ClearDirtyTree(kLayoutDirty);
        }
        ++layout_passes_;
        draw_dirty_ = true;
    }
    if (draw_dirty_) RefreshHover(); // the widget under a stationary pointer may have changed (layout, visibility, a popup)
    if (list == nullptr || !(draw_dirty_ || list->commands.Empty())) return false;
    list->Clear();
    DrawListCanvas canvas(list, atlas);
    canvas.Begin(scale_, static_cast<u32>(std::ceil(width_ * scale_)), static_cast<u32>(std::ceil(height_ * scale_)));
    root_->DrawTree(canvas);
    popups_->DrawTree(canvas);
    list->SortLayers();
    root_->ClearDirtyTree(kDrawDirty);
    popups_->ClearDirtyTree(kDrawDirty);
    draw_dirty_ = false;
    return true;
}

} // namespace engine_placeholder::ui
