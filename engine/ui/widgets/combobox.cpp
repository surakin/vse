#include "ui/widgets/combobox.h"

#include "lang/scalar.h"

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

namespace {

/// The drop-down list: a list box at least as wide as the combo box.
class DropList final : public ListBox {
public:
    explicit DropList(f32 min_width) : ListBox(StringView("dropdown")), min_width_(min_width) {}
    Size Measure(const Constraints& constraints) override {
        Size size = ListBox::Measure(constraints);
        if (size.w < min_width_) size.w = min_width_;
        return size;
    }

private:
    f32 min_width_;
};

} // namespace

ComboBox::ComboBox(StringView name) : Widget(name) {
    SetFocusable(true);
    SetAcceptsText(true); // type-ahead
    field_ = static_cast<TextField*>(AddChild(MakeUnique<TextField>(StringView("text"))));
    field_->SetFrame(false);
    field_->SetVisible(false);
    field_->OnChanged().Connect(&ComboBox::FieldChanged, this);
}

f32 ComboBox::ButtonWidth() {
    Surface* surface = GetSurface();
    return surface == nullptr ? 0.0f : surface->Themes().Metric(MetricId::IconSize, 1.0f) * kButtonIcons;
}

void ComboBox::SetModel(ListModel* model) {
    Close();
    model_ = model;
    selected_ = kNone;
    ShowSelected();
}

void ComboBox::ModelChanged() {
    Close();
    if (model_ == nullptr || selected_ >= model_->ItemCount()) selected_ = kNone;
    ShowSelected();
}

void ComboBox::SetEditable(bool editable) {
    if (editable_ == editable) return;
    editable_ = editable;
    field_->SetVisible(editable);
    SetFocusable(!editable);
    SetAcceptsText(!editable);
    ShowSelected();
    MarkDirty(kLayoutDirty);
}

void ComboBox::Select(usize index) {
    selected_ = (model_ != nullptr && index < model_->ItemCount()) ? index : kNone;
    ShowSelected();
}

void ComboBox::ShowSelected() {
    const StringView text = selected_ != kNone ? model_->ItemText(selected_) : StringView();
    if (editable_) {
        // With no selection the typed text stays; choosing an item replaces it.
        if (selected_ != kNone) {
            syncing_ = true;
            field_->SetText(text);
            field_->SelectAll();
            syncing_ = false;
        }
    } else if (run_.SetText(text)) {
        MarkDirty(kLayoutDirty);
    }
    MarkDirty(kDrawDirty);
}

String<HeapAllocator> ComboBox::Text() const {
    const StringView text = editable_ ? field_->Text() : (selected_ != kNone ? model_->ItemText(selected_) : StringView());
    return String<HeapAllocator>(HeapAllocator::Default(), text);
}

void ComboBox::UserSelect(usize index) {
    if (model_ == nullptr || index >= model_->ItemCount() || index == selected_) return;
    selected_ = index;
    ShowSelected();
    on_selection_.Invoke(this);
}

void ComboBox::FieldChanged(void* user, TextField* field) {
    ComboBox* box = static_cast<ComboBox*>(user);
    if (box->syncing_ || box->model_ == nullptr) return;
    usize found = kNone;
    const usize n = box->model_->ItemCount();
    for (usize i = 0; i < n && found == kNone; ++i) {
        if (box->model_->ItemText(i) == field->Text()) found = i;
    }
    if (found == box->selected_) return;
    box->selected_ = found;
    box->MarkDirty(kDrawDirty);
    box->on_selection_.Invoke(box);
}

bool ComboBox::IsOpen() const { return list_ != nullptr && GetSurface() != nullptr && GetSurface()->IsPopupOpen(list_); }

void ComboBox::Open() {
    Surface* surface = GetSurface();
    if (surface == nullptr || model_ == nullptr || model_->ItemCount() == 0 || IsOpen()) return;
    UniquePtr<DropList> list = MakeUnique<DropList>(Bounds().w);
    list->SetModel(model_);
    list->SetActivateOnClick(true);
    list->SetVisibleRows(model_->ItemCount() < kDropRows ? model_->ItemCount() : kDropRows);
    list->OnActivate().Connect(&ComboBox::Activated, this);
    if (selected_ != kNone) list->Select(selected_);
    PopupOptions options;
    options.placement = Placement::Below;
    options.owner = this;
    list_ = static_cast<ListBox*>(surface->OpenPopup(UniquePtr<Widget>(list.Release()), Bounds(), options));
}

void ComboBox::Close() {
    Surface* surface = GetSurface();
    if (list_ != nullptr && surface != nullptr && surface->IsPopupOpen(list_)) surface->ClosePopup(list_);
    list_ = nullptr;
}

void ComboBox::Activated(void* user, ListBox* list) {
    ComboBox* box = static_cast<ComboBox*>(user);
    const usize index = list->Current();
    box->Close();
    box->UserSelect(index);
}

Size ComboBox::Measure(const Constraints&) {
    Surface* surface = GetSurface();
    if (surface == nullptr) return Size();
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    TextRun probe;
    probe.SetText(StringView("Ag"));
    f32 text_h = 0;
    if (probe.EnsureForStyle(surface, style, 0)) text_h = probe.Height();
    Size size;
    size.w = surface->Themes().Metric(MetricId::IconSize, 1.0f) * ListBox::kWidthInIcons + ButtonWidth();
    size.h = text_h + style.padding_y / scale * 2;
    return size;
}

void ComboBox::Arrange(const Rect& rect) {
    SetBounds(rect);
    Rect inner = rect;
    inner.w = FMax(rect.w - ButtonWidth(), 0.0f);
    field_->Arrange(inner);
}

void ComboBox::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    u32 state = State();
    if (surface->Focus() == field_ && surface->WindowFocused()) state |= kStateFocused;
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), state, scale);
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, style, scale);
    const f32 button_w = ButtonWidth();
    if (!editable_ && run_.Text().Length() > 0 && run_.EnsureForStyle(surface, style, 0)) {
        const f32 pad = style.padding_x / scale;
        const text::TextLayout& layout = run_.Fit(FMax(b.w - button_w - pad * 2, 0.0f) * scale);
        canvas.DrawText(layout, run_.Style(), Point{b.x + pad, b.y + (b.h - layout.height / scale) / 2}, style.text_color);
    }
    const f32 x = b.Right() - button_w;
    const u32 ink = (State() & kStateDisabled) != 0 ? surface->Themes().Color(ColorId::TextDisabled) : surface->Themes().Color(ColorId::Text);
    canvas.DrawRect(Rect{x, b.y, 1, b.h}, surface->Themes().Color(ColorId::Border));
    const f32 mid_x = x + button_w / 2;
    const f32 mid_y = b.y + b.h / 2;
    const f32 reach = button_w * kArrowReach;
    canvas.DrawTriangle(Point{mid_x - reach, mid_y - reach / 2}, Point{mid_x + reach, mid_y - reach / 2}, Point{mid_x, mid_y + reach / 2}, ink);
}

void ComboBox::TypeAhead(Event& event) {
    if (event.text_length != 1 || static_cast<u8>(event.text[0]) < static_cast<u8>(' ') || model_ == nullptr) return;
    const usize count = model_->ItemCount();
    if (count == 0) return;
    event.handled = true;
    const f64 now = GetSurface()->Time();
    if (now - typed_at_ > kTypeAheadSeconds) typed_.Clear();
    typed_.Push(AsciiLower(event.text[0]));
    typed_at_ = now;
    bool same = true;
    for (usize i = 1; i < typed_.Size(); ++i) same = same && typed_[i] == typed_[0];
    const usize length = same ? 1 : typed_.Size();
    usize start = selected_ == kNone ? 0 : selected_;
    if (same && selected_ != kNone) start = selected_ + 1;
    for (usize k = 0; k < count; ++k) {
        const usize i = (start + k) % count;
        const StringView text = model_->ItemText(i);
        if (text.Length() < length) continue;
        bool match = true;
        for (usize c = 0; c < length && match; ++c) match = AsciiLower(text[c]) == typed_[c];
        if (match) {
            UserSelect(i);
            return;
        }
    }
}

void ComboBox::OnEvent(Event& event) {
    const usize count = model_ != nullptr ? model_->ItemCount() : 0;
    switch (event.type) {
        case EventType::PointerDown:
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            if (!editable_) RequestFocus();
            Open();
            break;
        case EventType::Wheel:
            if (!wheel_selects_ || count == 0 || IsOpen()) break;
            event.handled = true;
            if (event.wheel_y > 0) {
                UserSelect(selected_ == kNone ? 0 : (selected_ + 1 < count ? selected_ + 1 : selected_));
            } else if (event.wheel_y < 0) {
                UserSelect(selected_ == kNone ? count - 1 : (selected_ > 0 ? selected_ - 1 : selected_));
            }
            break;
        case EventType::Text:
            if (!editable_) TypeAhead(event);
            break;
        case EventType::KeyDown: {
            const bool alt = (event.modifiers & os::kModAlt) != 0;
            switch (event.key) {
                case Key::F4:
                    event.handled = true;
                    Open();
                    break;
                case Key::Down:
                    event.handled = true;
                    if (alt || editable_) {
                        Open();
                    } else if (count > 0) {
                        UserSelect(selected_ == kNone ? 0 : (selected_ + 1 < count ? selected_ + 1 : selected_));
                    }
                    break;
                case Key::Up:
                    event.handled = true;
                    if (count > 0) UserSelect(selected_ == kNone ? count - 1 : (selected_ > 0 ? selected_ - 1 : selected_));
                    break;
                case Key::Home:
                    if (!editable_ && count > 0) {
                        event.handled = true;
                        UserSelect(0);
                    }
                    break;
                case Key::End:
                    if (!editable_ && count > 0) {
                        event.handled = true;
                        UserSelect(count - 1);
                    }
                    break;
                case Key::Enter:
                case Key::Space:
                    if (!editable_) {
                        event.handled = true;
                        Open();
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}

} // namespace engine_placeholder::ui
