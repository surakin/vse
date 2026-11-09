#include "ui/surface.h"

#include <cmath>

#include "algorithm.h"
#include "log/assert.h"
#include "ui/draw_list_canvas.h"
#include "ui/widgets/modal_dim.h"
#include "ui/widgets/tooltip.h"

namespace engine_placeholder::ui {

namespace {

bool IsPointer(EventType t) { return t == EventType::PointerMove || t == EventType::PointerDown || t == EventType::PointerUp || t == EventType::Wheel; }

} // namespace

Surface::Surface(f32 width, f32 height, f32 scale) : width_(width), height_(height), scale_(scale) {
    ENGINE_ASSERT(scale > 0, "Surface: the scale must be above 0");
    root_ = MakeUnique<Widget>(StringView("root"));
    modals_ = MakeUnique<Widget>(StringView("modals"));
    modals_->SetPassThrough(true);
    modals_->SetClipsChildren(false);
    popups_ = MakeUnique<Widget>(StringView("popups"));
    popups_->SetPassThrough(true);
    popups_->SetClipsChildren(false);
    tips_ = MakeUnique<Widget>(StringView("tooltips"));
    tips_->SetPassThrough(true);
    tips_->SetClipsChildren(false);
    root_->AttachSurface(this);
    modals_->AttachSurface(this);
    popups_->AttachSurface(this);
    tips_->AttachSurface(this);
    Rect all;
    all.w = width;
    all.h = height;
    root_->SetBounds(all);
    modals_->SetBounds(all);
    popups_->SetBounds(all);
    tips_->SetBounds(all);
}

Surface::~Surface() {
    root_.Reset();
    modals_.Reset();
    popups_.Reset();
    tips_.Reset();
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
    modals_->SetBounds(all);
    popups_->SetBounds(all);
    tips_->SetBounds(all);
    for (usize i = 0; i < modal_infos_.Size(); ++i) { // keep the dialogs inside the smaller surface
        Widget* d = modal_infos_[i].dialog;
        if (d == nullptr) continue;
        Rect r = d->Bounds();
        if (r.x + r.w > width) r.x = width - r.w;
        if (r.y + r.h > height) r.y = height - r.h;
        if (r.x < 0) r.x = 0;
        if (r.y < 0) r.y = 0;
        d->Arrange(r);
    }
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
    if (lock_owner_ == widget) ReleasePointerLock(widget);
    if (hover_ == widget) hover_ = nullptr;
    if (drag_.source == widget) drag_.source = nullptr;
    if (drag_.visual == widget) drag_.visual = nullptr;
    if (drag_.hover_widget == widget) drag_.hover_widget = nullptr;
    if (drag_.target == widget) {
        drag_.target = nullptr;
        drag_.action = DragAction::Nothing;
    }
    if (saved_focus_ == widget) saved_focus_ = nullptr;
    if (menu_saved_focus_ == widget) menu_saved_focus_ = nullptr;
    for (usize i = 0; i < modal_infos_.Size(); ++i) {
        if (modal_infos_[i].dialog == widget) modal_infos_[i].dialog = nullptr; // closed at the next boundary
        if (modal_infos_[i].dim == widget) modal_infos_[i].dim = nullptr;
        if (modal_infos_[i].saved_focus == widget) modal_infos_[i].saved_focus = nullptr;
    }
    if (menu_key_target_ == widget) menu_key_target_ = nullptr;
    if (tooltip_ == widget) tooltip_ = nullptr;
    if (tooltip_target_ == widget) {
        tooltip_target_ = nullptr;
        if (tooltip_ != nullptr) RemoveTooltip();
    }
    for (usize i = popup_infos_.Size(); i > 0; --i) {
        if (popup_infos_[i - 1].popup == widget) {
            popup_infos_.Remove(i - 1);
        } else if (popup_infos_[i - 1].owner == widget) {
            popup_infos_[i - 1].owner = nullptr;
            popup_infos_[i - 1].orphaned = true; // closed at the next boundary: this may be inside the removal of a popup
        }
    }
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
    if (!modal_infos_.Empty()) return modals_->HitTest(point); // the shade covers everything, so nothing beneath is reached
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
    hover_since_ = time_;
    RemoveTooltip();
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
        for (Widget* a = widget->parent_; a != nullptr && focus_ == widget; a = a->parent_) a->OnDescendantFocused(widget);
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

void Surface::SetPointerLockHost(PointerLockHost* host) {
    if (host == lock_host_) return;
    if (lock_owner_ != nullptr) ReleasePointerLock(lock_owner_); // the old host frees its pointer
    lock_host_ = host;
}

bool Surface::RequestPointerLock(Widget* owner) {
    if (owner == nullptr || lock_host_ == nullptr || lock_owner_ != nullptr || !lock_host_->LockSupported() || !lock_host_->SetLocked(true)) return false;
    lock_owner_ = owner;
    return true;
}

void Surface::ReleasePointerLock(Widget* owner) {
    if (owner == nullptr || lock_owner_ != owner) return;
    lock_owner_ = nullptr;
    if (lock_host_ != nullptr) lock_host_->SetLocked(false);
}

void Surface::ReleasePointer() {
    if (lock_owner_ != nullptr) ReleasePointerLock(lock_owner_); // losing the pointer (focus lost, a modal dialog opened) frees a lock
    if (drag_.active && !drag_.external && !drag_.handed_off) EndDrag(false); // losing the pointer (focus lost, a modal dialog opened) ends a drag held here
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
        popup_infos_.Clear();
        for (usize i = popups_->ChildCount(); i > 0; --i) popups_->RemoveChild(popups_->ChildAt(i - 1));
        HideTooltip();
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
    CollectTabStops(popups_->ChildCount() > 0 ? popups_.Get() : (TopModal() != nullptr ? TopModal() : root_.Get()), stops);
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
    if (drag_.active && !drag_.external) return CursorForDragAction(drag_.action);
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
    for (;;) { // a popup whose owner died goes, with the ones above it
        usize orphan = popup_infos_.Size();
        for (usize i = 0; i < popup_infos_.Size() && orphan == popup_infos_.Size(); ++i) {
            if (popup_infos_[i].orphaned) orphan = i;
        }
        if (orphan == popup_infos_.Size()) break;
        ClosePopup(popup_infos_[orphan].popup);
    }
    const usize n = popups_->ChildCount();
    for (;;) { // a modal dialog that died takes the dialogs above it with it
        usize dead = modal_infos_.Size();
        for (usize i = 0; i < modal_infos_.Size() && dead == modal_infos_.Size(); ++i) {
            if (modal_infos_[i].dialog == nullptr) dead = i;
        }
        if (dead == modal_infos_.Size()) break;
        CloseModalAt(dead);
    }
    if (n == popup_count_) return;
    const usize before = popup_count_;
    popup_count_ = n;
    if (before == 0) {
        saved_focus_ = focus_;
    } else if (n == 0) {
        Widget* back = saved_focus_;
        saved_focus_ = nullptr;
        // The menu bar only held the focus to run its menu: the focus goes to whoever had it before (see SaveFocus).
        if (back != nullptr && back == menu_key_target_ && menu_saved_focus_ != nullptr) {
            back = menu_saved_focus_;
            menu_saved_focus_ = nullptr;
        }
        // Give the focus back unless the program has moved it to something that is still alive outside the popups.
        if (back != nullptr && focus_ == nullptr && back->Shown() && back->EffectivelyEnabled()) SetFocus(back);
    }
}

void Surface::Dispatch(Event e) {
    e.time = time_;
    ++depth_;
    TrackPopups();
    if (drag_.active && !drag_.external && !drag_.handed_off && e.type == EventType::KeyDown && e.key == Key::Escape) {
        EndDrag(false); // Escape cancels a drag and goes no further
        if (--depth_ == 0) FlushRemovals();
        return;
    }
    if (drag_.active && !drag_.external && !drag_.handed_off && (IsPointer(e.type) || e.type == EventType::PointerLeave)) {
        DragPointer(e);
        if (--depth_ == 0) FlushRemovals();
        return;
    }
    if (e.type == EventType::RelativeMotion) {
        if (lock_owner_ != nullptr) Deliver(lock_owner_, e, false);
    } else if (e.type == EventType::WindowFocusGained || e.type == EventType::WindowFocusLost) {
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
                hover_since_ = time_; // the delay counts from the last movement
                tooltip_blocked_ = false;
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                break;
            }
            case EventType::PointerDown: {
                SetHover(hit, e);
                HideTooltip();
                if (capture_ == nullptr && PressOutsidePopups(hit)) break; // closed the popups, and the press goes no further
                if (capture_ == nullptr) {
                    // Click to focus: the nearest focusable widget under the pointer takes the keyboard focus, and a click on nothing focusable clears it.
                    Widget* focus_target = hit;
                    while (focus_target != nullptr && !(focus_target->Focusable() && focus_target->EffectivelyEnabled())) focus_target = focus_target->Parent();
                    if (focus_target != nullptr || modal_infos_.Empty()) SetFocus(focus_target); // a click on the shade or on a dialog's background keeps the focus
                    if (hit != nullptr) {
                        capture_ = hit;
                        hit->state_ |= kStatePressed;
                        hit->MarkDirty(kDrawDirty);
                    }
                }
                buttons_down_ |= e.button;
                if (capture_ != nullptr) Deliver(capture_, e, true);
                if (e.button == os::kMouseRight) {
                    for (Widget* w = hit; w != nullptr; w = w->parent_) {
                        if (w->context_function_ != nullptr && w->EffectivelyEnabled()) {
                            w->context_function_(w->context_user_, w, e.position);
                            break;
                        }
                    }
                }
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
                HideTooltip();
                // The wheel goes to the hovered widget (the capturer during a drag) and bubbles to the first ancestor that handles it; a popup is hit first, so nothing behind it sees it.
                Widget* target = capture_ != nullptr ? capture_ : hit;
                if (target != nullptr) Deliver(target, e, true);
                break;
            }
        }
    } else if (e.type == EventType::KeyDown || e.type == EventType::KeyUp || e.type == EventType::Text) {
        modifiers_ = e.modifiers;
        HideTooltip();
        const bool is_alt = e.key == Key::LeftAlt || e.key == Key::RightAlt;
        if (e.type == EventType::KeyDown) alt_alone_ = is_alt && !e.repeat;
        bool handled = false;
        Widget* key_target = KeyTarget();
        if (window_focused_ && key_target != nullptr && (e.type != EventType::Text || key_target->AcceptsText())) handled = Deliver(key_target, e, true);
        if (window_focused_ && !handled && e.type == EventType::KeyDown) RunKeyFallbacks(e);
        if (window_focused_ && !handled && e.type == EventType::KeyUp && is_alt && alt_alone_ && menu_key_target_ != nullptr) {
            alt_alone_ = false;
            Deliver(menu_key_target_, e, false); // a lone Alt, pressed and released: the menu bar takes the focus
        }
    }
    if (--depth_ == 0) FlushRemovals();
    TrackPopups();
}

bool Surface::InsidePopups(const Widget* widget) const {
    for (const Widget* w = widget; w != nullptr; w = w->parent_) {
        if (w->parent_ == popups_.Get()) return true;
    }
    return false;
}

bool Surface::PressOutsidePopups(Widget* hit) {
    if (popup_infos_.Empty() || InsidePopups(hit)) return false;
    const PopupInfo top = popup_infos_[popup_infos_.Size() - 1];
    bool on_owner = false;
    for (usize i = 0; i < popup_infos_.Size() && !on_owner; ++i) {
        for (Widget* w = hit; w != nullptr && popup_infos_[i].owner != nullptr; w = w->parent_) {
            if (w == popup_infos_[i].owner) on_owner = true;
        }
    }
    CloseAllPopups();
    return on_owner || top.consume;
}

bool Surface::RunKeyFallbacks(const Event& e) {
    if (e.key == Key::Tab && (e.modifiers & (os::kModCtrl | os::kModAlt)) == 0) {
        FocusNext((e.modifiers & os::kModShift) != 0);
        return true;
    }
    if (e.key == Key::Escape && !popup_infos_.Empty()) {
        ClosePopup(popup_infos_[popup_infos_.Size() - 1].popup);
        return true;
    }
    if (!modal_infos_.Empty()) return false; // a modal dialog blocks the accelerators and the menu bar
    const u32 mask = e.modifiers & (os::kModCtrl | os::kModShift | os::kModAlt);
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        const Accelerator& a = accelerators_[i];
        if (a.enabled && a.chord.key == e.key && a.chord.modifiers == mask) {
            a.function(a.user);
            return true;
        }
    }
    if (menu_key_target_ != nullptr && (e.key == Key::F10 || (mask == os::kModAlt && e.key != Key::LeftAlt && e.key != Key::RightAlt))) return Deliver(menu_key_target_, e, false);
    return false;
}

Widget* Surface::OpenPopup(UniquePtr<Widget> widget, const Rect& anchor, const PopupOptions& options) {
    ENGINE_ASSERT(widget.Get() != nullptr, "Surface::OpenPopup: the widget is null");
    HideTooltip();
    Widget* raw = widget.Get();
    popups_->AddChild(static_cast<UniquePtr<Widget>&&>(widget));
    TrackPopups(); // remembers the focus owner before the popup takes the focus
    Constraints limits;
    limits.max_w = width_;
    limits.max_h = height_;
    Rect all;
    all.w = width_;
    all.h = height_;
    raw->Arrange(PlacePopup(raw->MeasureTotal(limits), anchor, options.placement, all));
    PopupInfo info;
    info.popup = raw;
    info.owner = options.owner;
    info.consume = options.consume_outside_click;
    info.take_focus = options.take_focus;
    popup_infos_.Push(info);
    if (options.take_focus && raw->Focusable()) SetFocus(raw);
    return raw;
}

Widget* Surface::ShowContextMenu(UniquePtr<Widget> menu, Point position) {
    Rect at;
    at.x = position.x;
    at.y = position.y;
    PopupOptions options;
    options.placement = Placement::Below;
    return OpenPopup(static_cast<UniquePtr<Widget>&&>(menu), at, options);
}

bool Surface::IsPopupOpen(const Widget* popup) const {
    if (popup == nullptr) return false;
    for (usize i = 0; i < popup_infos_.Size(); ++i) {
        if (popup_infos_[i].popup == popup) return true;
    }
    return false;
}

void Surface::ClosePopup(Widget* popup) {
    usize index = popup_infos_.Size();
    for (usize i = 0; i < popup_infos_.Size(); ++i) {
        if (popup_infos_[i].popup == popup) index = i;
    }
    if (index == popup_infos_.Size()) return;
    bool focus_inside = false;
    for (const Widget* w = focus_; w != nullptr; w = w->parent_) {
        for (usize i = index; i < popup_infos_.Size(); ++i) focus_inside = focus_inside || w == popup_infos_[i].popup;
    }
    Vector<Widget*> closing;
    for (usize i = index; i < popup_infos_.Size(); ++i) closing.Push(popup_infos_[i].popup);
    popup_infos_.RemoveRange(index, popup_infos_.Size() - index);
    for (usize i = closing.Size(); i > 0; --i) popups_->RemoveChild(closing[i - 1]);
    // The popup below, if any, gets the focus the closed one had.
    if (focus_inside && !popup_infos_.Empty()) {
        Widget* below = popup_infos_[popup_infos_.Size() - 1].popup;
        if (popup_infos_[popup_infos_.Size() - 1].take_focus && below->Focusable()) SetFocus(below);
    }
}

Widget* Surface::KeyTarget() const {
    Widget* top = TopModal();
    if (top == nullptr) return focus_;
    for (const Widget* w = focus_; w != nullptr; w = w->parent_) {
        if (w == top) return focus_;
    }
    return InsidePopups(focus_) ? focus_ : top; // keys never reach what lies beneath a modal dialog
}

Widget* Surface::ShowModal(UniquePtr<Widget> dialog) {
    ENGINE_ASSERT(dialog.Get() != nullptr, "Surface::ShowModal: the dialog is null");
    HideTooltip();
    CloseAllPopups();
    ReleasePointer();
    ModalInfo info;
    info.saved_focus = focus_;
    UniquePtr<ModalDim> dim = MakeUnique<ModalDim>();
    info.dim = dim.Get();
    modals_->AddChild(UniquePtr<Widget>(dim.Release()));
    Widget* raw = dialog.Get();
    modals_->AddChild(static_cast<UniquePtr<Widget>&&>(dialog));
    info.dialog = raw;
    Constraints limits;
    limits.max_w = width_;
    limits.max_h = height_;
    const Size size = raw->MeasureTotal(limits);
    Rect at;
    at.w = size.w;
    at.h = size.h;
    at.x = (width_ - size.w) / 2;
    at.y = (height_ - size.h) / 2;
    if (at.x < 0) at.x = 0;
    if (at.y < 0) at.y = 0;
    raw->Arrange(at);
    modal_infos_.Push(info);
    Vector<Widget*> stops;
    CollectTabStops(raw, stops);
    SetFocus(!stops.Empty() ? stops[0] : (raw->Focusable() ? raw : nullptr));
    RefreshHover();
    return raw;
}

bool Surface::IsModalOpen(const Widget* dialog) const {
    if (dialog == nullptr) return false;
    for (usize i = 0; i < modal_infos_.Size(); ++i) {
        if (modal_infos_[i].dialog == dialog) return true;
    }
    return false;
}

void Surface::CloseModal(Widget* dialog) {
    for (usize i = 0; i < modal_infos_.Size(); ++i) {
        if (modal_infos_[i].dialog == dialog && dialog != nullptr) {
            CloseModalAt(i);
            return;
        }
    }
}

void Surface::CloseModalAt(usize index) {
    Widget* back = modal_infos_[index].saved_focus;
    bool focus_inside = focus_ == nullptr;
    Vector<Widget*> gone;
    for (usize i = modal_infos_.Size(); i > index; --i) {
        const ModalInfo& m = modal_infos_[i - 1];
        if (m.dialog != nullptr) {
            for (const Widget* w = focus_; w != nullptr && !focus_inside; w = w->parent_) focus_inside = w == m.dialog;
            gone.Push(m.dialog);
        }
        if (m.dim != nullptr) gone.Push(m.dim);
    }
    modal_infos_.RemoveRange(index, modal_infos_.Size() - index);
    CloseAllPopups();
    for (usize i = 0; i < gone.Size(); ++i) modals_->RemoveChild(gone[i]);
    if (focus_inside) {
        SetFocus(back != nullptr && back->Shown() && back->EffectivelyEnabled() ? back : nullptr);
    }
    RefreshHover();
}

void Surface::CloseAllPopups() {
    if (!popup_infos_.Empty()) ClosePopup(popup_infos_[0].popup);
}

u32 Surface::RegisterAccelerator(const KeyChord& chord, void (*function)(void* user), void* user) {
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        if (accelerators_[i].chord == chord) return 0;
    }
    Accelerator a;
    a.id = ++next_accelerator_;
    a.chord = chord;
    a.function = function;
    a.user = user;
    accelerators_.Push(a);
    return a.id;
}

void Surface::UnregisterAccelerator(u32 id) {
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        if (accelerators_[i].id == id) {
            accelerators_.Remove(i);
            return;
        }
    }
}

void Surface::SetAcceleratorEnabled(u32 id, bool enabled) {
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        if (accelerators_[i].id == id) accelerators_[i].enabled = enabled;
    }
}

bool Surface::AcceleratorEnabled(u32 id) const {
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        if (accelerators_[i].id == id) return accelerators_[i].enabled;
    }
    return false;
}

KeyChord Surface::AcceleratorChord(u32 id) const {
    for (usize i = 0; i < accelerators_.Size(); ++i) {
        if (accelerators_[i].id == id) return accelerators_[i].chord;
    }
    return KeyChord();
}

void Surface::RestoreSavedFocus() {
    Widget* back = menu_saved_focus_;
    menu_saved_focus_ = nullptr;
    SetFocus(back != nullptr && back->Shown() && back->EffectivelyEnabled() ? back : nullptr);
}

void Surface::HideTooltip() {
    tooltip_blocked_ = true;
    RemoveTooltip();
}

void Surface::RemoveTooltip() {
    if (tooltip_ == nullptr) return;
    Widget* t = tooltip_;
    tooltip_ = nullptr;
    tooltip_target_ = nullptr;
    tips_->RemoveChild(t);
}

void Surface::ShowTooltip(Widget* target) {
    UniquePtr<TooltipBubble> bubble = MakeUnique<TooltipBubble>();
    bubble->SetText(target->Tooltip());
    Widget* raw = bubble.Get();
    tips_->AddChild(UniquePtr<Widget>(bubble.Release()));
    Constraints limits;
    limits.max_w = width_;
    limits.max_h = height_;
    Rect at;
    at.x = pointer_position_.x;
    at.y = pointer_position_.y;
    at.h = themes_.Metric(MetricId::IconSize, 1.0f);
    Rect all;
    all.w = width_;
    all.h = height_;
    raw->Arrange(PlacePopup(raw->MeasureTotal(limits), at, Placement::Below, all));
    tooltip_ = raw;
    tooltip_target_ = target;
}

void Surface::UpdateTooltip() {
    if (tooltip_ != nullptr || tooltip_blocked_ || hover_ == nullptr || !window_focused_ || !pointer_inside_ || buttons_down_ != 0 || !popup_infos_.Empty()) return;
    if (hover_->Tooltip().Length() == 0 || time_ - hover_since_ < kTooltipDelay) return;
    ShowTooltip(hover_);
}

namespace {

bool AnyWantsUpdate(const Widget* widget) {
    if (!widget->Shown()) return false;
    if (widget->WantsUpdate()) return true;
    for (usize i = 0; i < widget->ChildCount(); ++i) {
        if (AnyWantsUpdate(widget->ChildAt(i))) return true;
    }
    return false;
}

} // namespace

bool Surface::NeedsFrame() const {
    if (layout_dirty_ || draw_dirty_ || !queue_.Empty() || drag_.active) return true;
    if (hover_ != nullptr && hover_->Tooltip().Length() > 0 && tooltip_ == nullptr && !tooltip_blocked_ && window_focused_ && pointer_inside_ && buttons_down_ == 0 && popup_infos_.Empty()) {
        return true; // a tooltip is waiting for its delay
    }
    return AnyWantsUpdate(root_.Get()) || AnyWantsUpdate(modals_.Get()) || AnyWantsUpdate(popups_.Get()) || AnyWantsUpdate(tips_.Get());
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
    // Two buffers swap places, so a frame allocates nothing once both have grown: events posted while these are dispatched go to the other buffer and wait for the next frame.
    Vector<Event> swap = static_cast<Vector<Event>&&>(queue_);
    queue_ = static_cast<Vector<Event>&&>(processing_);
    processing_ = static_cast<Vector<Event>&&>(swap);
    for (usize i = 0; i < processing_.Size(); ++i) Dispatch(processing_[i]);
    processing_.Clear();
    UpdateWidgets(root_.Get(), dt);
    UpdateDrag(dt);
    UpdateWidgets(modals_.Get(), dt);
    UpdateWidgets(popups_.Get(), dt);
    if (--depth_ == 0) FlushRemovals();
    TrackPopups();
    UpdateTooltip();
    for (const Widget* w = lock_owner_; w != nullptr; w = w->Parent()) {
        if (!w->Shown()) {
            ReleasePointerLock(lock_owner_); // the widget that holds the pointer lock was hidden or collapsed: the pointer is freed
            break;
        }
    }

    if (layout_dirty_) {
        // At most two passes: the second one is for a widget whose size depends on the width it was given (it invalidates layout when that changes).
        Rect all;
        all.w = width_;
        all.h = height_;
        for (int pass = 0; pass < 2; ++pass) {
            layout_dirty_ = false;
            root_->ClearDirtyTree(kLayoutDirty);
            modals_->ClearDirtyTree(kLayoutDirty);
            popups_->ClearDirtyTree(kLayoutDirty);
            tips_->ClearDirtyTree(kLayoutDirty);
            ++layout_generation_;
            root_->Arrange(all);
            modals_->Arrange(all);
            popups_->Arrange(all);
            tips_->Arrange(all);
            if (!layout_dirty_) break;
        }
        if (layout_dirty_) {
            ++layout_overruns_;
            layout_dirty_ = false;
            root_->ClearDirtyTree(kLayoutDirty);
            modals_->ClearDirtyTree(kLayoutDirty);
            popups_->ClearDirtyTree(kLayoutDirty);
            tips_->ClearDirtyTree(kLayoutDirty);
        }
        ++layout_passes_;
        draw_dirty_ = true;
    }
    if (draw_dirty_) RefreshHover(); // the widget under a stationary pointer may have changed (layout, visibility, a popup)
    if (list == nullptr || !(draw_dirty_ || list->commands.Empty())) return false;
    list->Clear();
    if (canvas_.Get() == nullptr) {
        canvas_ = MakeUnique<DrawListCanvas>(list, atlas);
    } else {
        canvas_->Rebind(list, atlas);
    }
    DrawListCanvas& canvas = *canvas_;
    canvas.Begin(scale_, static_cast<u32>(std::ceil(width_ * scale_)), static_cast<u32>(std::ceil(height_ * scale_)));
    root_->DrawTree(canvas);
    modals_->DrawTree(canvas);
    popups_->DrawTree(canvas);
    tips_->DrawTree(canvas);
    list->SortLayers();
    root_->ClearDirtyTree(kDrawDirty);
    modals_->ClearDirtyTree(kDrawDirty);
    popups_->ClearDirtyTree(kDrawDirty);
    tips_->ClearDirtyTree(kDrawDirty);
    draw_dirty_ = false;
    return true;
}

} // namespace engine_placeholder::ui
