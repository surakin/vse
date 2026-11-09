#include "ui/surface.h"

#include <cmath>

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
    if ((flags & kLayoutDirty) != 0) layout_dirty_ = true;
    draw_dirty_ = true;
}

void Surface::WidgetDestroyed(Widget* widget) {
    if (focus_ == widget) focus_ = nullptr;
    if (capture_ == widget) capture_ = nullptr;
    if (hover_ == widget) hover_ = nullptr;
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

void Surface::Deliver(Widget* target, Event event, bool bubble) {
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
        if (!bubble || local.handled) break;
        event.handled = false;
    }
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
        widget->state_ |= kStateFocused;
        widget->MarkDirty(kDrawDirty);
        e.type = EventType::FocusGained;
        Deliver(widget, e, false);
    }
    if (--depth_ == 0) FlushRemovals();
}

void Surface::SetCapture(Widget* widget) { capture_ = widget; }

void Surface::Dispatch(Event e) {
    e.time = time_;
    ++depth_;
    if (e.type == EventType::PointerLeave) {
        SetHover(nullptr, e); // posted to the surface: the pointer left it altogether
    } else if (IsPointer(e.type)) {
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
                if (capture_ == nullptr && hit != nullptr) {
                    capture_ = hit;
                    hit->state_ |= kStatePressed;
                    hit->MarkDirty(kDrawDirty);
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
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                break;
            }
        }
    } else if (e.type == EventType::KeyDown || e.type == EventType::KeyUp || e.type == EventType::Text) {
        if (focus_ != nullptr) Deliver(focus_, e, true);
    }
    if (--depth_ == 0) FlushRemovals();
}

void Surface::UpdateWidgets(Widget* widget, f32 dt) {
    if (!widget->Visible()) return;
    if (widget->WantsUpdate()) widget->Update(dt);
    for (usize i = 0; i < widget->ChildCount(); ++i) UpdateWidgets(widget->ChildAt(i), dt);
}

void Surface::RunLayout(Widget* widget) {
    widget->Arrange();
    for (usize i = 0; i < widget->ChildCount(); ++i) RunLayout(widget->ChildAt(i));
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

    if (layout_dirty_) {
        RunLayout(root_.Get());
        RunLayout(popups_.Get());
        root_->ClearDirtyTree(kLayoutDirty);
        popups_->ClearDirtyTree(kLayoutDirty);
        layout_dirty_ = false;
        ++layout_passes_;
        draw_dirty_ = true;
    }
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
