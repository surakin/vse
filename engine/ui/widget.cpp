#include "ui/widget.h"

#include "log/assert.h"
#include "ui/surface.h"
#include "ui/theme.h"

namespace engine_placeholder::ui {

namespace {

u64 HashName(StringView name) {
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < name.Length(); ++i) h = (h ^ static_cast<u8>(name[i])) * 1099511628211ull;
    return h;
}

u64 MixId(u64 parent, u64 key) {
    u64 h = (parent ^ key) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return h == 0 ? 1 : h;
}

bool Contains(const Rect& r, Point p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }

} // namespace

Widget::Widget(StringView name) : name_(HeapAllocator::Default(), name) { id_ = MixId(0, HashName(name)); }

void Widget::NotifyDestroyed() {
    if (surface_ != nullptr) surface_->WidgetDestroyed(this);
}

void Widget::RecomputeIds(u64 parent_id, usize index) {
    id_ = MixId(parent_id, name_.Length() > 0 ? HashName(name_.View()) : static_cast<u64>(index) + 1);
    for (usize i = 0; i < children_.Size(); ++i) children_[i]->RecomputeIds(id_, i);
}

void Widget::AttachSurface(Surface* surface) {
    surface_ = surface;
    for (usize i = 0; i < children_.Size(); ++i) children_[i]->AttachSurface(surface);
}

Widget* Widget::AddChild(UniquePtr<Widget> child) {
    ENGINE_ASSERT(child.Get() != nullptr && child->parent_ == nullptr && child.Get() != this, "Widget::AddChild: the child is null, has a parent, or is this widget");
    Widget* raw = child.Get();
    raw->parent_ = this;
    raw->RecomputeIds(id_, children_.Size());
    raw->AttachSurface(surface_);
    children_.Push(static_cast<UniquePtr<Widget>&&>(child));
    MarkDirty(kLayoutDirty | kDrawDirty);
    return raw;
}

void Widget::RemoveChild(Widget* child) {
    usize index = children_.Size();
    for (usize i = 0; i < children_.Size(); ++i) {
        if (children_[i].Get() == child) index = i;
    }
    ENGINE_ASSERT(index < children_.Size(), "Widget::RemoveChild: not a child of this widget");
    if (surface_ != nullptr && surface_->Dispatching()) {
        surface_->QueueRemoval(child);
        return;
    }
    MarkDirty(kLayoutDirty | kDrawDirty);
    children_.Remove(index);
}

void Widget::RemoveFromParent() {
    if (parent_ != nullptr) parent_->RemoveChild(this);
}

Widget* Widget::ChildAt(usize index) const {
    ENGINE_ASSERT(index < children_.Size(), "Widget::ChildAt: index out of range");
    return children_[index].Get();
}

Widget* Widget::FindById(u64 id) {
    if (id_ == id) return this;
    for (usize i = 0; i < children_.Size(); ++i) {
        if (Widget* found = children_[i]->FindById(id)) return found;
    }
    return nullptr;
}

Widget* Widget::FindByName(StringView name) {
    if (name_.View() == name) return this;
    for (usize i = 0; i < children_.Size(); ++i) {
        if (Widget* found = children_[i]->FindByName(name)) return found;
    }
    return nullptr;
}

Size Widget::MeasureTotal(const Constraints& constraints) {
    if (surface_ != nullptr && measure_generation_ == surface_->LayoutGeneration() && measure_constraints_ == constraints) return measure_result_;
    Size s = Measure(constraints);
    if (props_.width >= 0) s.w = props_.width;
    if (props_.height >= 0) s.h = props_.height;
    if (s.w < props_.min.w) s.w = props_.min.w;
    if (s.h < props_.min.h) s.h = props_.min.h;
    if (s.w > props_.max.w) s.w = props_.max.w;
    if (s.h > props_.max.h) s.h = props_.max.h;
    s = constraints.Clamp(s);
    if (surface_ != nullptr) {
        measure_generation_ = surface_->LayoutGeneration();
        measure_constraints_ = constraints;
        measure_result_ = s;
    }
    return s;
}

void Widget::SetProps(const LayoutProps& props) {
    props_ = props;
    MarkDirty(kLayoutDirty);
}

void Widget::SetCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) return;
    collapsed_ = collapsed;
    MarkDirty(kLayoutDirty | kDrawDirty);
}

void Widget::SetBounds(const Rect& bounds) {
    if (bounds.x == bounds_.x && bounds.y == bounds_.y && bounds.w == bounds_.w && bounds.h == bounds_.h) return;
    bounds_ = bounds;
    MarkDirty(kDrawDirty);
}

void Widget::SetVisible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    MarkDirty(kLayoutDirty | kDrawDirty);
}

void Widget::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    MarkDirty(kDrawDirty);
}

bool Widget::EffectivelyEnabled() const {
    for (const Widget* w = this; w != nullptr; w = w->parent_) {
        if (!w->enabled_) return false;
    }
    return true;
}

void Widget::SetClipsChildren(bool clip) {
    if (clip_children_ == clip) return;
    clip_children_ = clip;
    MarkDirty(kDrawDirty);
}

void Widget::RequestCapture() {
    if (surface_ != nullptr) surface_->SetCapture(this);
}

void Widget::ReleaseCapture() {
    if (surface_ != nullptr && surface_->Capture() == this) surface_->SetCapture(nullptr);
}

void Widget::RequestFocus() {
    if (surface_ != nullptr && focusable_ && Shown() && EffectivelyEnabled()) surface_->SetFocus(this);
}

u32 Widget::State() const { return state_ | (EffectivelyEnabled() ? 0u : kStateDisabled); }

void Widget::SetChecked(bool checked) {
    const u32 next = checked ? (state_ | kStateChecked) : (state_ & ~kStateChecked);
    if (next == state_) return;
    state_ = next;
    MarkDirty(kDrawDirty);
}

void Widget::MarkDirty(u32 flags) {
    if ((flags & kLayoutDirty) != 0) flags |= kDrawDirty;
    // Invariant: a flag set on a widget is set on all its ancestors, so the walk can stop at the first widget that has it all.
    for (Widget* w = this; w != nullptr && (w->dirty_ & flags) != flags; w = w->parent_) w->dirty_ |= flags;
    if (surface_ != nullptr) surface_->NoteDirty(flags);
}

void Widget::ClearDirtyTree(u32 flags) {
    if ((dirty_ & flags) == 0) return;
    dirty_ &= ~flags;
    for (usize i = 0; i < children_.Size(); ++i) children_[i]->ClearDirtyTree(flags);
}

Widget* Widget::HitTest(Point point) {
    if (!Shown()) return nullptr;
    if (clip_children_ && !Contains(bounds_, point)) return nullptr; // the widget and its children are cut by the bounds
    for (usize i = children_.Size(); i > 0; --i) {
        if (Widget* hit = children_[i - 1]->HitTest(point)) return hit;
    }
    if (!pass_through_ && Contains(bounds_, point)) return this;
    return nullptr;
}

void Widget::DrawTree(Canvas& canvas) {
    if (!Shown()) return;
    Draw(canvas);
    if (children_.Empty()) return;
    if (clip_children_) canvas.PushClip(bounds_);
    for (usize i = 0; i < children_.Size(); ++i) children_[i]->DrawTree(canvas);
    if (clip_children_) canvas.PopClip();
}

} // namespace engine_placeholder::ui
