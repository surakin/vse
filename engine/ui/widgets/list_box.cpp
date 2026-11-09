#include "ui/widgets/list_box.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

ListBox::ListBox(StringView name) : Widget(name) {
    SetFocusable(true);
    SetAcceptsText(true);
    bar_ = static_cast<ScrollBar*>(AddChild(MakeUnique<ScrollBar>(StringView("bar"), true)));
    bar_->OnChanged().Connect(&ListBox::BarMoved, this);
    bar_->SetCollapsed(true);
}

void ListBox::BarMoved(void* user, ScrollBar* bar) {
    ListBox* box = static_cast<ListBox*>(user);
    box->SetOffset(static_cast<f32>(bar->Value()));
}

void ListBox::SetModel(ListModel* model) {
    model_ = model;
    offset_ = 0;
    count_ = static_cast<usize>(-1); // force the sync to see a change
    SyncModel();
    MarkDirty(kLayoutDirty);
}

void ListBox::SyncModel() {
    const usize n = model_ != nullptr ? model_->ItemCount() : 0;
    if (n == count_) return;
    count_ = n;
    selected_.Clear();
    selected_.Resize(n);
    selected_count_ = 0;
    current_ = kNone;
    anchor_ = kNone;
    hover_ = kNone;
    ++revision_;
    const f32 max = MaxOffset();
    if (offset_ > max) offset_ = max;
    MarkDirty(kLayoutDirty);
}

void ListBox::ModelChanged() {
    count_ = static_cast<usize>(-1); // the items may have been replaced by as many others: the selection is cleared whatever the count
    SyncModel();
    MarkDirty(kLayoutDirty | kDrawDirty);
}

void ListBox::SetMode(SelectionMode mode) {
    mode_ = mode;
    if (mode_ == SelectionMode::Single && selected_count_ > 1) {
        ClearSelection();
        Notify();
    }
}

bool ListBox::IsSelected(usize index) const { return index < selected_.Size() && selected_[index] != 0; }

bool ListBox::MeasureRow() {
    Surface* surface = GetSurface();
    if (surface == nullptr) return false;
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), 0, scale);
    probe_.SetText(StringView("Ag"));
    if (!probe_.EnsureForStyle(surface, style, 0)) return false;
    row_height_ = probe_.Height() + style.padding_y / scale * 2;
    return true;
}

f32 ListBox::ViewHeight() const { return Bounds().h; }

f32 ListBox::MaxOffset() const {
    const f32 content = static_cast<f32>(count_) * row_height_;
    return content > ViewHeight() ? content - ViewHeight() : 0.0f;
}

void ListBox::SetOffset(f32 offset) {
    const f32 max = MaxOffset();
    offset = FMin(FMax(offset, 0.0f), max);
    if (offset == offset_) return;
    offset_ = offset;
    bar_->SetValue(offset_);
    MarkDirty(kDrawDirty);
}

usize ListBox::IndexAt(f32 local_y) const {
    if (!(row_height_ > 0) || local_y < 0) return kNone;
    const usize index = static_cast<usize>((local_y + offset_) / row_height_);
    return index < count_ ? index : kNone;
}

void ListBox::VisibleRange(usize* first, usize* end) const {
    *first = 0;
    *end = 0;
    if (!(row_height_ > 0) || count_ == 0) return;
    *first = static_cast<usize>(offset_ / row_height_);
    usize last = static_cast<usize>(Ceil((offset_ + ViewHeight()) / row_height_));
    if (last > count_) last = count_;
    *end = last > *first ? last : *first;
}

void ListBox::SetVisibleRows(usize rows) {
    visible_rows_ = rows == 0 ? 1 : rows;
    MarkDirty(kLayoutDirty);
}

void ListBox::ScrollToIndex(usize index) {
    if (index >= count_) return;
    if (!(row_height_ > 0)) {
        scroll_pending_ = index; // before the first layout the row height is unknown (a drop-down selects its item before it is shown): scrolled in Arrange
        return;
    }
    const f32 top = static_cast<f32>(index) * row_height_;
    if (top < offset_) {
        SetOffset(top);
    } else if (top + row_height_ > offset_ + ViewHeight()) {
        SetOffset(top + row_height_ - ViewHeight());
    }
}

bool ListBox::SetSelectedFlag(usize index, bool selected) {
    if (index >= selected_.Size()) return false;
    const bool was = selected_[index] != 0;
    if (was == selected) return false;
    selected_[index] = selected ? 1 : 0;
    if (selected) {
        ++selected_count_;
    } else {
        --selected_count_;
    }
    return true;
}

bool ListBox::ClearSelection() {
    if (selected_count_ == 0) return false;
    for (usize i = 0; i < selected_.Size(); ++i) selected_[i] = 0;
    selected_count_ = 0;
    return true;
}

bool ListBox::SelectOnly(usize index) {
    if (selected_count_ == 1 && IsSelected(index)) return false;
    ClearSelection();
    SetSelectedFlag(index, true);
    return true;
}

bool ListBox::SelectRange(usize a, usize b) {
    const usize lo = a < b ? a : b;
    const usize hi = a < b ? b : a;
    bool changed = ClearSelection();
    for (usize i = lo; i <= hi && i < count_; ++i) changed = SetSelectedFlag(i, true) || changed;
    return changed;
}

void ListBox::Notify() {
    MarkDirty(kDrawDirty);
    on_selection_.Invoke(this);
}

void ListBox::Select(usize index) {
    bool changed = false;
    if (index >= count_) {
        changed = ClearSelection();
        current_ = kNone;
        anchor_ = kNone;
    } else {
        changed = SelectOnly(index);
        current_ = index;
        anchor_ = index;
        ScrollToIndex(index);
    }
    MarkDirty(kDrawDirty);
    if (changed) Notify();
}

void ListBox::MoveCurrent(usize to, bool extend) {
    if (count_ == 0) return;
    if (to >= count_) to = count_ - 1;
    bool changed;
    if (extend && mode_ == SelectionMode::Multiple) {
        if (anchor_ == kNone) anchor_ = current_ == kNone ? to : current_;
        changed = SelectRange(anchor_, to);
    } else {
        changed = SelectOnly(to);
        anchor_ = to;
    }
    current_ = to;
    ScrollToIndex(to);
    MarkDirty(kDrawDirty);
    if (changed) Notify();
}

void ListBox::Click(usize index, u32 modifiers) {
    bool changed = false;
    if (mode_ == SelectionMode::Multiple && (modifiers & os::kModCtrl) != 0) {
        changed = SetSelectedFlag(index, !IsSelected(index));
        anchor_ = index;
    } else if (mode_ == SelectionMode::Multiple && (modifiers & os::kModShift) != 0) {
        if (anchor_ == kNone) anchor_ = index;
        changed = SelectRange(anchor_, index);
    } else {
        changed = SelectOnly(index);
        anchor_ = index;
    }
    current_ = index;
    ScrollToIndex(index);
    MarkDirty(kDrawDirty);
    if (changed) Notify();
}

void ListBox::TypeAhead(Event& event) {
    if (event.text_length != 1 || static_cast<u8>(event.text[0]) < static_cast<u8>(' ') || count_ == 0) return;
    event.handled = true;
    const f64 now = GetSurface()->Time();
    if (now - typed_at_ > kTypeAheadSeconds) typed_.Clear();
    typed_.Push(AsciiLower(event.text[0]));
    typed_at_ = now;
    bool same = true;
    for (usize i = 1; i < typed_.Size(); ++i) same = same && typed_[i] == typed_[0];
    const usize length = same ? 1 : typed_.Size(); // a letter repeated cycles through the items that start with it
    usize start = current_ == kNone ? 0 : current_;
    if (same && current_ != kNone) start = current_ + 1;
    for (usize k = 0; k < count_; ++k) {
        const usize i = (start + k) % count_;
        const StringView text = model_->ItemText(i);
        if (text.Length() < length) continue;
        bool match = true;
        for (usize c = 0; c < length && match; ++c) match = AsciiLower(text[c]) == typed_[c];
        if (match) {
            MoveCurrent(i, false);
            return;
        }
    }
}

Size ListBox::Measure(const Constraints&) {
    if (!MeasureRow()) return Size();
    Surface* surface = GetSurface();
    return Size{surface->Themes().Metric(MetricId::IconSize, 1.0f) * kWidthInIcons, row_height_ * (visible_rows_ != 0 ? static_cast<f32>(visible_rows_) : kPreferredRows)};
}

void ListBox::Arrange(const Rect& rect) {
    SetBounds(rect);
    MeasureRow();
    SyncModel();
    Surface* surface = GetSurface();
    const f32 thickness = surface != nullptr ? surface->Themes().Metric(MetricId::ScrollbarThickness, 1.0f) : 0.0f;
    const f32 content = static_cast<f32>(count_) * row_height_;
    const bool shown = content > rect.h;
    bar_->SetCollapsed(!shown);
    const f32 max = MaxOffset();
    if (offset_ > max) offset_ = max;
    if (shown) {
        Rect r;
        r.x = rect.Right() - thickness;
        r.y = rect.y;
        r.w = thickness;
        r.h = rect.h;
        bar_->Arrange(r);
        bar_->SetLineStep(row_height_);
        bar_->SetRange(content, rect.h);
        bar_->SetValue(offset_);
    }
    if (scroll_pending_ != kNone) {
        const usize index = scroll_pending_;
        scroll_pending_ = kNone;
        ScrollToIndex(index);
    }
}

void ListBox::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const ResolvedStyle box = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, box, scale);
    if (model_ == nullptr || !MeasureRow()) return;
    usize first = 0;
    usize end = 0;
    VisibleRange(&first, &end);
    if (end == first) return;
    const usize needed = end - first + 1;
    if (slots_.Size() < needed) slots_.Resize(needed);
    const f32 bar_w = bar_->Collapsed() ? 0.0f : bar_->Bounds().w;
    canvas.PushClip(b);
    for (usize i = first; i < end; ++i) {
        Slot& slot = slots_[i % slots_.Size()];
        if (slot.index != i || slot.revision != revision_) {
            slot.run.SetText(model_->ItemText(i));
            slot.index = i;
            slot.revision = revision_;
            ++row_layouts_;
        }
        u32 state = 0;
        if (IsSelected(i)) state |= kStateChecked;
        if (i == hover_) state |= kStateHover;
        if ((State() & kStateDisabled) != 0) state |= kStateDisabled;
        const ResolvedStyle row = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), state, scale);
        Rect r;
        r.x = b.x;
        r.y = b.y + static_cast<f32>(i) * row_height_ - offset_;
        r.w = b.w - bar_w;
        r.h = row_height_;
        if (IsVisibleColor(row.background)) canvas.DrawRect(r, row.background);
        if (slot.run.EnsureForStyle(surface, row, 0)) {
            const f32 pad = row.padding_x / scale;
            const text::TextLayout& layout = slot.run.Fit((r.w - pad * 2) * scale);
            canvas.DrawText(layout, slot.run.Style(), Point{r.x + pad, r.y + (r.h - layout.height / scale) / 2}, row.text_color);
        }
        if (i == current_ && (State() & kStateFocused) != 0) canvas.DrawBorder(r, CornerRadii(), 1.0f, surface->Themes().Color(ColorId::Accent));
    }
    if (drop_index_ != kNone) {
        const ResolvedStyle marker = surface->Themes().Resolve(widget_kind::kDropMarker, VariantId(0), 0, scale);
        Rect line;
        line.x = b.x;
        line.w = b.w - bar_w;
        line.h = kDropMarkerThickness;
        line.y = b.y + static_cast<f32>(drop_index_) * row_height_ - offset_ - kDropMarkerThickness / 2;
        canvas.DrawRect(line, marker.background);
    }
    canvas.PopClip();
}

usize ListBox::InsertionIndex(f32 local_y) const {
    if (row_height_ <= 0 || count_ == 0) return 0;
    const f32 rows = (local_y + offset_) / row_height_;
    if (rows <= 0) return 0;
    const usize index = static_cast<usize>(rows + 0.5f); // the nearer boundary between two rows
    return index > count_ ? count_ : index;
}

void ListBox::StartDrag(Point) {
    Surface* surface = GetSurface();
    DragPayload payload;
    if (reorderable_ && model_ != nullptr) payload.AddPointer(StringView(kDragTag), this);
    if (drag_out_ && model_ != nullptr) {
        String<HeapAllocator> text{HeapAllocator::Default()};
        for (usize i = 0; i < count_; ++i) {
            if (!IsSelected(i)) continue;
            if (text.Length() > 0) text.Append('\n');
            text.Append(model_->ItemText(i));
        }
        payload.AddText(text.View());
    }
    payload.SetAllowed(drag_out_ ? os::kAllDragActions : os::DragActionBit(DragAction::Move));
    press_pending_ = false;
    surface->BeginDrag(static_cast<DragPayload&&>(payload));
}

DragAction ListBox::CanAccept(const DragPayload& payload, Point) {
    if (!reorderable_ || model_ == nullptr || payload.PointerOf(StringView(kDragTag)) != this || !payload.Allows(DragAction::Move)) return DragAction::Nothing;
    return DragAction::Move;
}

void ListBox::OnDragOver(const DragPayload&, Point position, DragAction) {
    const usize index = InsertionIndex(position.y);
    if (index != drop_index_) {
        drop_index_ = index;
        MarkDirty(kDrawDirty);
    }
}

void ListBox::OnDragLeave() {
    drop_index_ = kNone;
    MarkDirty(kDrawDirty);
}

void ListBox::OnDragEnded(DragAction) {
    pressed_ = kNone;
    press_pending_ = false;
    drop_index_ = kNone;
    MarkDirty(kDrawDirty);
}

bool ListBox::DragAutoScroll(Point position, f32 dt) {
    const f32 delta = DragScrollDelta(position.y, ViewHeight(), dt);
    if (delta == 0.0f) return false;
    const f32 before = offset_;
    SetOffset(offset_ + delta);
    return offset_ != before;
}

void ListBox::OnDrop(const DragPayload&, Point position, DragAction) {
    const usize to = drop_index_ != kNone ? drop_index_ : InsertionIndex(position.y);
    drop_index_ = kNone;
    MarkDirty(kDrawDirty);
    if (model_ == nullptr || selected_count_ == 0) return;
    Vector<usize> indices;
    usize before = 0;
    for (usize i = 0; i < count_; ++i) {
        if (!IsSelected(i)) continue;
        indices.Push(i);
        if (i < to) ++before;
    }
    bool contiguous = true;
    for (usize k = 1; k < indices.Size(); ++k) contiguous = contiguous && indices[k] == indices[k - 1] + 1;
    if (contiguous && to >= indices[0] && to <= indices[indices.Size() - 1] + 1) return; // dropped on itself: nothing moves
    if (!model_->MoveItems(indices.Data(), indices.Size(), to)) return;
    ++revision_; // the rows' texts are read again
    const usize start = to - before;
    for (usize i = 0; i < selected_.Size(); ++i) selected_[i] = 0;
    for (usize k = 0; k < indices.Size(); ++k) selected_[start + k] = 1;
    selected_count_ = indices.Size();
    current_ = start;
    anchor_ = start;
    Notify();
    MarkDirty(kDrawDirty);
}

void ListBox::OnEvent(Event& event) {
    switch (event.type) {
        case EventType::PointerDown:
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            pressed_ = IndexAt(event.position.y);
            press_position_ = event.position;
            press_pending_ = false;
            if (pressed_ != kNone) {
                const bool plain = (event.modifiers & (os::kModCtrl | os::kModShift)) == 0;
                if ((reorderable_ || drag_out_) && plain && IsSelected(pressed_)) {
                    press_pending_ = true; // a press on the selection keeps it: it may be the start of a drag
                    current_ = pressed_;
                } else {
                    Click(pressed_, event.modifiers);
                }
            }
            break;
        case EventType::PointerUp:
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            if (press_pending_ && pressed_ != kNone && pressed_ == IndexAt(event.position.y)) Click(pressed_, event.modifiers); // a click on the selection: it becomes the one item
            press_pending_ = false;
            if (activate_on_click_ && pressed_ != kNone && pressed_ == IndexAt(event.position.y)) on_activate_.Invoke(this);
            pressed_ = kNone;
            break;
        case EventType::PointerMove: {
            Surface* surface = GetSurface();
            if (pressed_ != kNone && (reorderable_ || drag_out_) && surface != nullptr && surface->Capture() == this && surface->PastDragSlop(press_position_, event.position)) {
                StartDrag(event.position);
                break;
            }
            const usize i = IndexAt(event.position.y);
            if (i != hover_) {
                hover_ = i;
                MarkDirty(kDrawDirty);
            }
            break;
        }
        case EventType::PointerLeave:
            if (hover_ != kNone) {
                hover_ = kNone;
                MarkDirty(kDrawDirty);
            }
            break;
        case EventType::Wheel: {
            const f32 before = offset_;
            SetOffset(offset_ + event.wheel_y * kWheelRows * row_height_);
            if (offset_ != before) event.handled = true;
            break;
        }
        case EventType::Text:
            TypeAhead(event);
            break;
        case EventType::KeyDown: {
            const bool extend = (event.modifiers & os::kModShift) != 0;
            const usize page = row_height_ > 0 ? static_cast<usize>(ViewHeight() / row_height_) : 1;
            const usize step = page > 1 ? page - 1 : 1;
            const usize at = current_ == kNone ? 0 : current_;
            event.handled = true;
            switch (event.key) {
                case Key::Down:
                    MoveCurrent(current_ == kNone ? 0 : at + 1, extend);
                    break;
                case Key::Up:
                    MoveCurrent(at > 0 ? at - 1 : 0, extend);
                    break;
                case Key::PageDown:
                    MoveCurrent(at + step, extend);
                    break;
                case Key::PageUp:
                    MoveCurrent(at > step ? at - step : 0, extend);
                    break;
                case Key::Home:
                    MoveCurrent(0, extend);
                    break;
                case Key::End:
                    MoveCurrent(count_ > 0 ? count_ - 1 : 0, extend);
                    break;
                case Key::Space:
                    if (current_ != kNone) MoveCurrent(current_, false);
                    break;
                case Key::Enter:
                    if (current_ != kNone) on_activate_.Invoke(this);
                    break;
                case Key::A:
                    if (mode_ == SelectionMode::Multiple && (event.modifiers & os::kModCtrl) != 0 && count_ > 0) {
                        if (SelectRange(0, count_ - 1)) Notify();
                    } else {
                        event.handled = false;
                    }
                    break;
                default:
                    event.handled = false;
                    break;
            }
            break;
        }
        default:
            break;
    }
}

} // namespace engine_placeholder::ui
