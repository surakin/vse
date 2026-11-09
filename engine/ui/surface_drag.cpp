#include "lang/scalar.h"

#include "ui/surface.h"
#include "log/assert.h"
#include "ui/widgets/look.h"

//! Drag and drop routing of [[ui::Surface]] (Phase 15, Day 200).

namespace engine_placeholder::ui {

namespace {

Point LocalTo(const Widget* w, Point p) { return Point(p.x - w->Bounds().x, p.y - w->Bounds().y); }

DragAction DefaultAction(const DragPayload& payload, u32 modifiers) {
    const bool ctrl = (modifiers & os::kModCtrl) != 0;
    const bool shift = (modifiers & os::kModShift) != 0;
    DragAction order[3] = {DragAction::Move, DragAction::Copy, DragAction::Link};
    if (ctrl && shift) {
        order[0] = DragAction::Link;
        order[1] = DragAction::Copy;
        order[2] = DragAction::Move;
    } else if (ctrl) {
        order[0] = DragAction::Copy;
        order[1] = DragAction::Move;
        order[2] = DragAction::Link;
    }
    for (DragAction a : order) {
        if (payload.Allows(a)) return a;
    }
    return DragAction::Nothing;
}

} // namespace

f32 Surface::DragSlop() const { return const_cast<Surface*>(this)->themes_.Metric(MetricId::DragSlop, scale_) / scale_; }

bool Surface::PastDragSlop(Point from, Point to) const {
    const f32 slop = DragSlop();
    return Abs(to.x - from.x) > slop || Abs(to.y - from.y) > slop;
}

void Surface::UpdateDragRequested() {
    DragAction a = DefaultAction(drag_.payload, modifiers_);
    if (drag_.os_action != DragAction::Nothing && drag_.payload.Allows(drag_.os_action)) a = drag_.os_action;
    drag_.payload.requested_ = a;
}

void Surface::ClearDragTargetState() {
    if (drag_.target != nullptr) {
        drag_.target->state_ &= ~kStateDragOver;
        drag_.target->MarkDirty(kDrawDirty);
    }
}

void Surface::UpdateDragTarget() {
    UpdateDragRequested();
    Widget* hit = HitTestAll(drag_.position);
    Widget* found = nullptr;
    DragAction action = DragAction::Nothing;
    for (Widget* w = hit; w != nullptr && found == nullptr; w = w->parent_) {
        if (!w->EffectivelyEnabled()) continue;
        DragAction a = w->CanAccept(drag_.payload, LocalTo(w, drag_.position));
        if (a != DragAction::Nothing && !drag_.payload.Allows(a)) a = DragAction::Nothing;
        if (a != DragAction::Nothing) {
            found = w;
            action = a;
        }
    }
    bool files = false;
    if (found == nullptr && drag_.payload.Has(DragEntryKind::FileList) && on_files_dropped_.Count() > 0) {
        files = true; // the root's file drop takes it
        action = drag_.payload.Allows(DragAction::Copy) ? DragAction::Copy : drag_.payload.Requested();
    }
    if (found != drag_.target) {
        Widget* old = drag_.target;
        ClearDragTargetState();
        drag_.target = found;
        if (old != nullptr) old->OnDragLeave();
        if (found != nullptr) {
            found->state_ |= kStateDragOver;
            found->MarkDirty(kDrawDirty);
            found->OnDragEnter(drag_.payload, LocalTo(found, drag_.position));
        }
    }
    drag_.files_target = files;
    drag_.action = action;
    if (found != nullptr) found->OnDragOver(drag_.payload, LocalTo(found, drag_.position), action);
}

void Surface::MoveDragVisual() {
    if (drag_.visual == nullptr) return;
    Rect r;
    r.x = drag_.position.x + drag_.visual_offset.x;
    r.y = drag_.position.y + drag_.visual_offset.y;
    r.w = drag_.visual_size.w;
    r.h = drag_.visual_size.h;
    drag_.visual->Arrange(r);
}

bool Surface::BeginDrag(DragPayload payload, UniquePtr<Widget> visual, Point visual_offset) {
    if (drag_.active || buttons_down_ == 0 || payload.EntryCount() == 0 || payload.Allowed() == 0) return false;
    Widget* source = capture_;
    if (capture_ != nullptr) {
        capture_->state_ &= ~kStatePressed;
        capture_->MarkDirty(kDrawDirty);
        capture_ = nullptr;
    }
    HideTooltip();
    Event cause;
    cause.position = pointer_position_;
    SetHover(nullptr, cause);
    drag_ = DragState();
    drag_.active = true;
    drag_.payload = static_cast<DragPayload&&>(payload);
    drag_.source = source;
    drag_.position = pointer_position_;
    drag_.hover_position = pointer_position_;
    drag_.hover_since = time_;
    drag_.visual_offset = visual_offset;
    if (visual.Get() != nullptr) {
        visual->SetPassThrough(true);
        drag_.visual = tips_->AddChild(static_cast<UniquePtr<Widget>&&>(visual));
        Constraints limits;
        limits.max_w = width_;
        limits.max_h = height_;
        drag_.visual_size = drag_.visual->MeasureTotal(limits);
        MoveDragVisual();
    }
    UpdateDragTarget();
    NoteDirty(kDrawDirty);
    return true;
}

DragAction Surface::EndDrag(bool drop) {
    if (!drag_.active) return DragAction::Nothing;
    DragState state = static_cast<DragState&&>(drag_);
    drag_ = DragState();
    if (state.target != nullptr) {
        state.target->state_ &= ~kStateDragOver;
        state.target->MarkDirty(kDrawDirty);
    }
    if (state.visual != nullptr) tips_->RemoveChild(state.visual);
    DragAction taken = DragAction::Nothing;
    if (drop && state.target != nullptr && state.action != DragAction::Nothing) {
        taken = state.action;
        state.target->OnDrop(state.payload, LocalTo(state.target, state.position), taken);
    } else if (drop && state.files_target && state.action != DragAction::Nothing) {
        taken = state.action;
        const DragEntry* files = state.payload.Find(DragEntryKind::FileList);
        if (files != nullptr) on_files_dropped_.Invoke(this, files->TextView());
    } else if (state.target != nullptr) {
        state.target->OnDragLeave();
    }
    if (state.source != nullptr) state.source->OnDragEnded(taken);
    buttons_down_ = 0;
    NoteDirty(kDrawDirty);
    RefreshHover();
    return taken;
}

void Surface::DragPointer(const Event& e) {
    modifiers_ = e.modifiers;
    if (e.type == EventType::PointerLeave) {
        pointer_inside_ = false;
        return; // a drag keeps its target until the pointer comes back or the adapter hands the drag to the OS
    }
    pointer_inside_ = true;
    pointer_position_ = e.position;
    if (e.type == EventType::PointerMove) {
        drag_.position = e.position;
        MoveDragVisual();
        UpdateDragTarget();
    } else if (e.type == EventType::PointerDown) {
        buttons_down_ |= e.button;
    } else if (e.type == EventType::PointerUp) {
        buttons_down_ &= ~e.button;
        if (buttons_down_ == 0) {
            drag_.position = e.position;
            UpdateDragTarget();
            EndDrag(true);
        }
    }
}

void Surface::UpdateDrag(f32 dt) {
    if (!drag_.active || drag_.handed_off) return;
    const u32 before = static_cast<u32>(drag_.payload.requested_);
    UpdateDragRequested();
    Widget* hit = HitTestAll(drag_.position);
    bool changed = before != static_cast<u32>(drag_.payload.requested_);
    for (Widget* w = hit; w != nullptr; w = w->parent_) {
        if (w->EffectivelyEnabled() && w->DragAutoScroll(LocalTo(w, drag_.position), dt)) {
            changed = true;
            break;
        }
    }
    if (hit != drag_.hover_widget || PastDragSlop(drag_.hover_position, drag_.position)) {
        drag_.hover_widget = hit;
        drag_.hover_position = drag_.position;
        drag_.hover_since = time_;
        drag_.hover_fired = false;
    } else if (!drag_.hover_fired && time_ - drag_.hover_since >= kDragHoverSeconds) {
        drag_.hover_fired = true;
        for (Widget* w = hit; w != nullptr; w = w->parent_) {
            if (w->EffectivelyEnabled() && w->OnDragHoverDelay(LocalTo(w, drag_.position))) {
                changed = true;
                break;
            }
        }
    }
    if (changed) UpdateDragTarget();
}

void Surface::CancelDrag() {
    if (!drag_.active) return;
    ++depth_;
    EndDrag(false);
    if (--depth_ == 0) FlushRemovals();
}

void Surface::HandOffDrag() {
    if (!drag_.active || drag_.external || drag_.handed_off) return;
    ++depth_;
    ClearDragTargetState();
    if (drag_.target != nullptr) drag_.target->OnDragLeave();
    drag_.target = nullptr;
    drag_.files_target = false;
    drag_.action = DragAction::Nothing;
    if (drag_.visual != nullptr) {
        tips_->RemoveChild(drag_.visual);
        drag_.visual = nullptr;
    }
    drag_.handed_off = true;
    buttons_down_ = 0;
    if (--depth_ == 0) FlushRemovals();
    NoteDirty(kDrawDirty);
}

void Surface::EndHandedOffDrag(DragAction taken) {
    if (!drag_.active || !drag_.handed_off) return;
    ++depth_;
    Widget* source = drag_.source;
    drag_ = DragState();
    if (source != nullptr) source->OnDragEnded(taken);
    if (--depth_ == 0) FlushRemovals();
    RefreshHover();
}

void Surface::ExternalDragEnter(DragPayload payload, Point position, u8 allowed) {
    if (drag_.active) return;
    ++depth_;
    drag_ = DragState();
    drag_.active = true;
    drag_.external = true;
    drag_.payload = static_cast<DragPayload&&>(payload);
    drag_.payload.SetAllowed(allowed);
    drag_.position = position;
    drag_.hover_position = position;
    drag_.hover_since = time_;
    UpdateDragTarget();
    if (--depth_ == 0) FlushRemovals();
}

void Surface::ExternalDragOver(Point position, u8 allowed) {
    if (!drag_.active || !drag_.external) return;
    ++depth_;
    drag_.payload.SetAllowed(allowed);
    drag_.position = position;
    UpdateDragTarget();
    if (--depth_ == 0) FlushRemovals();
}

void Surface::ExternalDragLeave() {
    if (!drag_.active || !drag_.external) return;
    ++depth_;
    EndDrag(false);
    if (--depth_ == 0) FlushRemovals();
}

DragAction Surface::ExternalDrop(DragPayload complete, Point position, DragAction action) {
    if (!drag_.active || !drag_.external) return DragAction::Nothing;
    ++depth_;
    const u8 allowed = drag_.payload.Allowed();
    drag_.payload = static_cast<DragPayload&&>(complete);
    drag_.payload.SetAllowed(allowed);
    drag_.os_action = action;
    drag_.position = position;
    UpdateDragTarget();
    const DragAction taken = EndDrag(true);
    if (--depth_ == 0) FlushRemovals();
    return taken;
}

void Surface::ExternalDropFailed(StringView message) {
    if (!drag_.active || !drag_.external) return;
    ++depth_;
    EndDrag(false);
    on_drop_failed_.Invoke(this, message);
    if (--depth_ == 0) FlushRemovals();
}

} // namespace engine_placeholder::ui
