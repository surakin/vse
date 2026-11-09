#include "ui/widgets/menu.h"

#include "lang/scalar.h"

#include "log/assert.h"
#include "text/cursor.h"
#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

// ---- Menu ----

Menu::Menu() {}

Menu::~Menu() {}

usize Menu::AddItem(StringView text, MenuAction action, void* user) {
    MenuItem item;
    item.text = String<HeapAllocator>(HeapAllocator::Default(), text);
    item.action = action;
    item.user = user;
    items_.Push(static_cast<MenuItem&&>(item));
    return items_.Size() - 1;
}

usize Menu::AddCheck(StringView text, bool checked, MenuAction action, void* user) {
    const usize index = AddItem(text, action, user);
    items_[index].kind = MenuItemKind::Check;
    items_[index].checked = checked;
    return index;
}

usize Menu::AddRadio(StringView text, bool checked, MenuAction action, void* user) {
    const usize index = AddItem(text, action, user);
    items_[index].kind = MenuItemKind::Radio;
    SetItemChecked(index, checked);
    return index;
}

usize Menu::AddSubmenu(StringView text, UniquePtr<Menu> submenu) {
    const usize index = AddItem(text);
    items_[index].kind = MenuItemKind::Submenu;
    items_[index].submenu = static_cast<UniquePtr<Menu>&&>(submenu);
    return index;
}

usize Menu::AddSeparator() {
    const usize index = AddItem(StringView());
    items_[index].kind = MenuItemKind::Separator;
    return index;
}

const MenuItem& Menu::Item(usize index) const {
    ENGINE_ASSERT(index < items_.Size(), "Menu::Item: index out of range");
    return items_[index];
}

void Menu::SetItemEnabled(usize index, bool enabled) {
    ENGINE_ASSERT(index < items_.Size(), "Menu::SetItemEnabled: index out of range");
    items_[index].enabled = enabled;
}

void Menu::SetItemChecked(usize index, bool checked) {
    ENGINE_ASSERT(index < items_.Size(), "Menu::SetItemChecked: index out of range");
    items_[index].checked = checked;
    if (checked && items_[index].kind == MenuItemKind::Radio) {
        usize lo = index;
        while (lo > 0 && items_[lo - 1].kind == MenuItemKind::Radio) --lo;
        for (usize i = lo; i < items_.Size() && items_[i].kind == MenuItemKind::Radio; ++i) {
            if (i != index) items_[i].checked = false;
        }
    }
}

void Menu::SetItemAccelerator(usize index, u32 id) {
    ENGINE_ASSERT(index < items_.Size(), "Menu::SetItemAccelerator: index out of range");
    items_[index].accelerator = id;
}

Menu* Menu::Submenu(usize index) const { return index < items_.Size() ? items_[index].submenu.Get() : nullptr; }

namespace {
void RunEditOn(void* user, Widget::EditCommand command) {
    Surface* surface = static_cast<Surface*>(user);
    Widget* target = surface->EditTarget();
    if (target != nullptr) target->RunEdit(command);
}
void EditCut(void* user, usize) { RunEditOn(user, Widget::EditCommand::Cut); }
void EditCopy(void* user, usize) { RunEditOn(user, Widget::EditCommand::Copy); }
void EditPaste(void* user, usize) { RunEditOn(user, Widget::EditCommand::Paste); }
void EditSelectAll(void* user, usize) { RunEditOn(user, Widget::EditCommand::SelectAll); }
} // namespace

usize AddEditItems(Menu& menu, Surface* surface) {
    const usize first = menu.AddItem(StringView("Cu&t"), &EditCut, surface);
    menu.AddItem(StringView("&Copy"), &EditCopy, surface);
    menu.AddItem(StringView("&Paste"), &EditPaste, surface);
    menu.AddSeparator();
    menu.AddItem(StringView("Select &All"), &EditSelectAll, surface);
    return first;
}

void RefreshEditItems(Menu& menu, usize first, const Surface& surface) {
    const Widget* target = surface.EditTarget();
    const Widget::EditCommand commands[kEditItemCount] = {Widget::EditCommand::Cut, Widget::EditCommand::Copy, Widget::EditCommand::Paste, Widget::EditCommand::SelectAll};
    for (usize i = 0; i < kEditItemCount; ++i) menu.SetItemEnabled(first + kEditItemOffsets[i], target != nullptr && target->CanEdit(commands[i]));
}

bool Menu::Choose(usize index) {
    if (index >= items_.Size()) return false;
    MenuItem& item = items_[index];
    if (!item.enabled || item.kind == MenuItemKind::Separator || item.kind == MenuItemKind::Submenu) return false;
    if (item.kind == MenuItemKind::Check) item.checked = !item.checked;
    if (item.kind == MenuItemKind::Radio) SetItemChecked(index, true);
    if (item.action != nullptr) item.action(item.user, index);
    return true;
}

char SplitMnemonic(StringView text, Vector<char>* label, u32* offset) {
    label->Clear();
    char mnemonic = 0;
    for (usize i = 0; i < text.Length(); ++i) {
        if (text[i] == '&' && i + 1 < text.Length()) {
            ++i;
            if (text[i] != '&' && mnemonic == 0) {
                *offset = static_cast<u32>(label->Size());
                const char c = text[i];
                mnemonic = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            }
        }
        label->Push(text[i]);
    }
    return mnemonic;
}

// ---- MenuView ----

MenuView::MenuView(Menu* menu) : Widget(StringView("menu")), menu_(menu) {
    ENGINE_ASSERT(menu != nullptr, "MenuView: the menu is null");
    SetFocusable(true);
}

MenuView::MenuView(UniquePtr<Menu> menu) : Widget(StringView("menu")), menu_(menu.Get()), owned_(static_cast<UniquePtr<Menu>&&>(menu)) {
    ENGINE_ASSERT(menu_ != nullptr, "MenuView: the menu is null");
    SetFocusable(true);
}

bool MenuView::SubmenuOpen() const { return sub_ != nullptr && GetSurface() != nullptr && GetSurface()->IsPopupOpen(sub_); }

bool MenuView::CanChoose(usize index) const {
    if (index >= menu_->ItemCount()) return false;
    const MenuItem& item = menu_->Item(index);
    if (item.kind == MenuItemKind::Separator || !item.enabled) return false;
    return item.accelerator == 0 || GetSurface() == nullptr || GetSurface()->AcceleratorEnabled(item.accelerator);
}

void MenuView::Rebuild() {
    Surface* surface = GetSurface();
    if (surface == nullptr) return;
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), 0, scale);
    const f32 pad_y = style.padding_y / scale;
    const f32 gap = surface->Themes().Metric(MetricId::Spacing, 1.0f);
    const usize count = menu_->ItemCount();
    rows_.Clear();
    rows_.Resize(count);
    f32 y = 0;
    f32 max_label = 0;
    f32 max_accel = 0;
    for (usize i = 0; i < count; ++i) {
        const MenuItem& item = menu_->Item(i);
        Row& row = rows_[i];
        row.top = y;
        if (item.kind == MenuItemKind::Separator) {
            row.height = gap;
        } else {
            Vector<char> label;
            row.mnemonic = SplitMnemonic(item.text.View(), &label, &row.mnemonic_offset);
            row.label.SetText(StringView(label.Data(), label.Size()));
            f32 line = 0;
            if (row.label.EnsureForStyle(surface, style, 0)) {
                max_label = FMax(max_label, row.label.Width());
                line = row.label.Height();
            }
            if (item.accelerator != 0) {
                const String<HeapAllocator> chord = ChordText(surface->AcceleratorChord(item.accelerator));
                row.accel.SetText(chord.View());
                if (row.accel.EnsureForStyle(surface, style, 0)) {
                    max_accel = FMax(max_accel, row.accel.Width());
                    line = FMax(line, row.accel.Height());
                }
            }
            row.height = line + pad_y * 2;
        }
        y += row.height;
    }
    const f32 pad_x = style.padding_x / scale;
    const f32 icon = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    label_x_ = pad_x + icon;
    accel_right_ = label_x_ + max_label + (max_accel > 0 ? gap * 2 + max_accel : 0.0f);
    content_width_ = accel_right_ + icon + pad_x;
    content_height_ = y;
}

Size MenuView::Measure(const Constraints&) {
    Rebuild();
    return Size{content_width_, content_height_};
}

Rect MenuView::RowRect(usize index) const {
    if (index >= rows_.Size()) return Rect();
    return Rect{Bounds().x, Bounds().y + rows_[index].top, Bounds().w, rows_[index].height};
}

usize MenuView::RowAt(f32 local_y) const {
    for (usize i = 0; i < rows_.Size(); ++i) {
        if (local_y >= rows_[i].top && local_y < rows_[i].top + rows_[i].height) return i;
    }
    return kNone;
}

void MenuView::SetHighlight(usize index) {
    if (index != kNone && !CanChoose(index)) return;
    if (index != kNone && index != open_index_) CloseSub();
    if (index == highlight_) return;
    highlight_ = index;
    MarkDirty(kDrawDirty);
}

void MenuView::Move(int direction) {
    const usize n = menu_->ItemCount();
    if (n == 0) return;
    const usize start = highlight_ == kNone ? (direction > 0 ? n - 1 : 0) : highlight_;
    for (usize k = 1; k <= n; ++k) {
        const usize idx = (start + (direction > 0 ? k : n - k)) % n;
        if (CanChoose(idx)) {
            SetHighlight(idx);
            pending_ = kNone;
            return;
        }
    }
}

void MenuView::HighlightFirst() {
    highlight_ = kNone;
    Move(1);
}

void MenuView::CloseSub() {
    pending_ = kNone;
    Surface* surface = GetSurface();
    if (sub_ != nullptr && surface != nullptr && surface->IsPopupOpen(sub_)) surface->ClosePopup(sub_);
    sub_ = nullptr;
    open_index_ = kNone;
}

void MenuView::OpenSub(usize index, bool take_focus) {
    Surface* surface = GetSurface();
    Menu* sub = menu_->Submenu(index);
    if (surface == nullptr || sub == nullptr || !CanChoose(index)) return;
    if (open_index_ == index && SubmenuOpen()) {
        if (take_focus) surface->SetFocus(sub_);
        return;
    }
    CloseSub();
    UniquePtr<MenuView> view = MakeUnique<MenuView>(sub);
    view->is_submenu_ = true;
    Rect anchor = Bounds();
    anchor.y += rows_[index].top;
    anchor.h = rows_[index].height;
    PopupOptions options;
    options.placement = Placement::Right;
    options.take_focus = take_focus;
    options.owner = this;
    highlight_ = index;
    open_index_ = index;
    MenuView* raw = view.Get();
    sub_ = surface->OpenPopup(UniquePtr<Widget>(view.Release()), anchor, options);
    if (take_focus) raw->Move(1);
    MarkDirty(kDrawDirty);
}

void MenuView::Choose(usize index) {
    if (!CanChoose(index)) return;
    if (menu_->Item(index).kind == MenuItemKind::Submenu) {
        OpenSub(index, true);
        return;
    }
    Surface* surface = GetSurface();
    menu_->Choose(index);
    surface->CloseAllPopups();
}

void MenuView::Update(f32 dt) {
    if (pending_ == kNone) return;
    pending_time_ += static_cast<f64>(dt);
    if (pending_time_ >= kSubmenuDelay) {
        const usize index = pending_;
        pending_ = kNone;
        OpenSub(index, false);
    }
}

void MenuView::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, surface->Themes().Resolve(widget_kind::kPanel, VariantId(0), 0, scale), scale);
    if (rows_.Size() != menu_->ItemCount()) Rebuild();
    const ResolvedStyle base = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), 0, scale);
    const f32 pad_x = base.padding_x / scale;
    const f32 pad_y = base.padding_y / scale;
    const f32 icon = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    for (usize i = 0; i < rows_.Size(); ++i) {
        const MenuItem& item = menu_->Item(i);
        const Row& row = rows_[i];
        const Rect r{b.x, b.y + row.top, b.w, row.height};
        if (item.kind == MenuItemKind::Separator) {
            canvas.DrawRect(Rect{r.x + pad_x, r.y + r.h / 2, r.w - pad_x * 2, kStroke}, surface->Themes().Color(ColorId::Border));
            continue;
        }
        u32 state = 0;
        if (!CanChoose(i)) state |= kStateDisabled;
        if (i == highlight_ && (state & kStateDisabled) == 0) state |= kStateHover;
        const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), state, scale);
        if (IsVisibleColor(style.background)) canvas.DrawRect(r, style.background);
        const f32 text_y = r.y + pad_y;
        if (!row.label.Full().glyphs.Empty()) {
            canvas.DrawText(row.label.Full(), row.label.Style(), Point{r.x + label_x_, text_y}, style.text_color);
            if (row.mnemonic != 0) {
                const text::CursorPosition from = text::PositionOfOffset(row.label.Full(), row.mnemonic_offset);
                const text::CursorPosition to = text::PositionOfOffset(row.label.Full(), text::NextBoundary(row.label.Full(), row.mnemonic_offset));
                const f32 y = text_y + from.height / scale;
                canvas.DrawLine(Point{r.x + label_x_ + from.x / scale, y}, Point{r.x + label_x_ + to.x / scale, y}, kStroke, style.text_color);
            }
        }
        if (item.accelerator != 0 && !row.accel.Full().glyphs.Empty()) {
            canvas.DrawText(row.accel.Full(), row.accel.Style(), Point{r.x + accel_right_ - row.accel.Width(), text_y}, style.text_color);
        }
        const f32 mark = row.height * kMarkFraction;
        const f32 cx = r.x + pad_x + icon / 2;
        const f32 cy = r.y + r.h / 2;
        if (item.kind == MenuItemKind::Check && item.checked) {
            canvas.DrawLine(Point{cx - mark, cy}, Point{cx - mark * kCheckElbowX, cy + mark * kCheckElbowY}, kStroke, style.text_color);
            canvas.DrawLine(Point{cx - mark * kCheckElbowX, cy + mark * kCheckElbowY}, Point{cx + mark, cy - mark * kCheckElbowY}, kStroke, style.text_color);
        } else if (item.kind == MenuItemKind::Radio && item.checked) {
            canvas.DrawRoundedRect(Rect{cx - mark / 2, cy - mark / 2, mark, mark}, CornerRadii(mark / 2), style.text_color);
        } else if (item.kind == MenuItemKind::Submenu) {
            const f32 ax = r.Right() - pad_x - icon / 2;
            canvas.DrawTriangle(Point{ax - mark / 2, cy - mark}, Point{ax - mark / 2, cy + mark}, Point{ax + mark / 2, cy}, style.text_color);
        }
    }
}

void MenuView::OnEvent(Event& event) {
    switch (event.type) {
        case EventType::PointerMove: {
            const usize row = RowAt(event.position.y);
            if (row != kNone && row != highlight_ && CanChoose(row)) {
                SetHighlight(row);
                if (menu_->Item(row).kind == MenuItemKind::Submenu && open_index_ != row) {
                    pending_ = row;
                    pending_time_ = 0;
                }
            }
            break;
        }
        case EventType::PointerDown:
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            pressed_ = RowAt(event.position.y);
            break;
        case EventType::PointerUp:
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            if (pressed_ != kNone && pressed_ == RowAt(event.position.y)) Choose(pressed_);
            pressed_ = kNone;
            break;
        case EventType::Wheel:
            event.handled = true; // a menu does not scroll the page behind it
            break;
        case EventType::KeyDown: {
            event.handled = true;
            const bool plain = (event.modifiers & (os::kModCtrl | os::kModAlt)) == 0;
            switch (event.key) {
                case Key::Down:
                    Move(1);
                    break;
                case Key::Up:
                    Move(-1);
                    break;
                case Key::Home:
                    highlight_ = kNone;
                    Move(1);
                    break;
                case Key::End:
                    highlight_ = kNone;
                    Move(-1);
                    break;
                case Key::Right:
                    if (highlight_ != kNone && menu_->Item(highlight_).kind == MenuItemKind::Submenu) {
                        OpenSub(highlight_, true);
                    } else {
                        on_edge_.Invoke(this, 1);
                    }
                    break;
                case Key::Left:
                    if (is_submenu_) {
                        GetSurface()->ClosePopup(this);
                    } else {
                        on_edge_.Invoke(this, -1);
                    }
                    break;
                case Key::Enter:
                case Key::Space:
                    if (highlight_ != kNone) Choose(highlight_);
                    break;
                default: {
                    const char letter = plain ? KeyLetter(event.key) : 0;
                    event.handled = false;
                    if (letter == 0) break;
                    for (usize i = 0; i < rows_.Size(); ++i) {
                        if (rows_[i].mnemonic == letter && CanChoose(i)) {
                            event.handled = true;
                            SetHighlight(i);
                            Choose(i);
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
        default:
            break;
    }
}

} // namespace engine_placeholder::ui
