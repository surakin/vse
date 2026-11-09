#include "ui/widgets/buttons.h"

#include <cmath>

#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

// ---- Button ----

Button::Button(StringView name, StringView text) : Button(widget_kind::kButton, name, text) {}

Button::Button(WidgetKind kind, StringView name, StringView text) : Widget(name), kind_(kind) {
    run_.SetText(text);
    SetFocusable(true);
}

void Button::SetText(StringView text) {
    if (run_.SetText(text)) MarkDirty(kLayoutDirty);
}

void Button::SetImage(ImageId image) {
    if (image_ == image) return;
    image_ = image;
    MarkDirty(kLayoutDirty);
}

void Button::SetVariant(StringView variant) {
    variant_ = RegisterVariant(variant);
    MarkDirty(kLayoutDirty);
}

ResolvedStyle Button::Look(f32 scale) { return GetSurface()->Themes().Resolve(kind_, variant_, State(), scale); }

Size Button::LeadingSize(f32) {
    if (image_ == 0) return Size();
    const f32 icon = GetSurface()->Themes().Metric(MetricId::IconSize, 1.0f);
    return Size{icon, icon};
}

void Button::DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle&, f32) {
    if (image_ == 0) return;
    Rect uv;
    uv.w = 1;
    uv.h = 1;
    canvas.DrawImage(rect, image_, uv, ~0u);
}

Size Button::Measure(const Constraints&) {
    Surface* surface = GetSurface();
    if (surface == nullptr) return Size();
    const f32 scale = surface->Scale();
    const ResolvedStyle style = Look(scale);
    const bool has_text = run_.Text().Length() > 0;
    const bool laid_out = has_text && run_.EnsureForStyle(surface, style, 0);
    const Size lead = LeadingSize(scale);
    const f32 text_w = laid_out ? run_.Width() : 0.0f;
    const f32 text_h = laid_out ? run_.Height() : 0.0f;
    const f32 gap = (lead.w > 0 && text_w > 0) ? surface->Themes().Metric(MetricId::Spacing, 1.0f) : 0.0f;
    Size size;
    size.w = style.padding_x / scale * 2 + lead.w + gap + text_w;
    size.h = style.padding_y / scale * 2 + std::fmax(lead.h, text_h);
    return size;
}

void Button::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const ResolvedStyle style = Look(scale);
    const Rect& b = Bounds();
    if (PaintsBox()) DrawStyleBox(canvas, b, style, scale);
    const f32 pad_x = style.padding_x / scale;
    const bool has_text = run_.Text().Length() > 0;
    const bool laid_out = has_text && run_.EnsureForStyle(surface, style, 0);
    const Size lead = LeadingSize(scale);
    const f32 gap = (lead.w > 0 && laid_out) ? surface->Themes().Metric(MetricId::Spacing, 1.0f) : 0.0f;
    f32 room = b.w - pad_x * 2 - lead.w - gap; // what the text may use
    if (room < 0) room = 0;
    const text::TextLayout* layout = nullptr;
    f32 text_w = 0;
    if (laid_out) {
        layout = &run_.Fit(room * scale);
        text_w = layout->width / scale;
    }
    const f32 content_w = lead.w + gap + text_w;
    f32 x = CentersContent() ? b.x + (b.w - content_w) / 2 : b.x + pad_x;
    if (x < b.x + pad_x) x = b.x + pad_x;
    if (lead.w > 0) {
        Rect box;
        box.x = x;
        box.y = b.y + (b.h - lead.h) / 2;
        box.w = lead.w;
        box.h = lead.h;
        DrawLeading(canvas, box, style, scale);
    }
    if (layout != nullptr) {
        const f32 text_h = layout->height / scale;
        canvas.DrawText(*layout, run_.Style(), Point{x + lead.w + gap, b.y + (b.h - text_h) / 2}, style.text_color);
    }
}

void Button::Click() {
    if (!EffectivelyEnabled()) return;
    Activated();
    on_click_.Invoke(this);
}

void Button::OnEvent(Event& event) {
    switch (event.type) {
        case EventType::PointerDown:
            if (event.button == os::kMouseLeft) event.handled = true;
            break;
        case EventType::PointerUp: {
            if (event.button != os::kMouseLeft) break;
            event.handled = true;
            const bool inside = event.position.x >= 0 && event.position.y >= 0 && event.position.x < Bounds().w && event.position.y < Bounds().h;
            if (inside && (State() & kStatePressed) != 0) Click();
            break;
        }
        case EventType::KeyDown:
            if (!event.repeat && (event.key == Key::Space || event.key == Key::Enter)) {
                event.handled = true;
                Click();
            }
            break;
        default:
            break;
    }
}

// ---- ToggleButton ----

void ToggleButton::Activated() {
    SetChecked(!Checked());
    on_toggled_.Invoke(this);
}

// ---- CheckBox ----

void CheckBox::SetTriState(bool tri_state) {
    tri_state_ = tri_state;
    if (!tri_state && state_ == CheckState::Indeterminate) SetCheckState(CheckState::Unchecked);
}

void CheckBox::SetCheckState(CheckState state) {
    if (state == CheckState::Indeterminate && !tri_state_) state = CheckState::Unchecked;
    state_ = state;
    SetChecked(state == CheckState::Checked);
    MarkDirty(kDrawDirty);
}

void CheckBox::Activated() {
    switch (state_) {
        case CheckState::Unchecked:
            SetCheckState(CheckState::Checked);
            break;
        case CheckState::Checked:
            SetCheckState(tri_state_ ? CheckState::Indeterminate : CheckState::Unchecked);
            break;
        case CheckState::Indeterminate:
            SetCheckState(CheckState::Unchecked);
            break;
    }
    on_changed_.Invoke(this);
}

Size CheckBox::LeadingSize(f32) {
    const f32 icon = GetSurface()->Themes().Metric(MetricId::IconSize, 1.0f);
    return Size{icon, icon};
}

void CheckBox::DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale) {
    DrawStyleBox(canvas, rect, style, scale);
    const u32 mark = GetSurface()->Themes().Color(ColorId::WindowBackground);
    const f32 width = std::fmax(style.border_width / scale * 2, 1.0f);
    if (state_ == CheckState::Checked) {
        const Point a{rect.x + rect.w * 0.25f, rect.y + rect.h * 0.5f};
        const Point m{rect.x + rect.w * 0.45f, rect.y + rect.h * 0.7f};
        const Point z{rect.x + rect.w * 0.75f, rect.y + rect.h * 0.3f};
        canvas.DrawLine(a, m, width, mark);
        canvas.DrawLine(m, z, width, mark);
    } else if (state_ == CheckState::Indeterminate) {
        canvas.DrawLine(Point{rect.x + rect.w * 0.25f, rect.y + rect.h * 0.5f}, Point{rect.x + rect.w * 0.75f, rect.y + rect.h * 0.5f}, width, style.text_color);
    }
}

// ---- RadioButton ----

RadioButton::RadioButton(StringView name, StringView text) : Button(widget_kind::kRadio, name, text) { SetRole(RadioGroup::kButtonRole); }

RadioGroup* RadioButton::Group() const {
    for (Widget* w = Parent(); w != nullptr; w = w->Parent()) {
        if (w->Role() == RadioGroup::kRole) return static_cast<RadioGroup*>(w);
    }
    return nullptr;
}

void RadioButton::Activated() {
    if (RadioGroup* group = Group()) {
        group->Select(this);
    } else {
        SetChecked(true);
    }
}

Size RadioButton::LeadingSize(f32) {
    const f32 icon = GetSurface()->Themes().Metric(MetricId::IconSize, 1.0f);
    return Size{icon, icon};
}

void RadioButton::DrawLeading(Canvas& canvas, const Rect& rect, const ResolvedStyle& style, f32 scale) {
    const CornerRadii round(rect.w / 2);
    if (IsVisibleColor(style.background)) canvas.DrawRoundedRect(rect, round, style.background);
    if (style.border_width > 0) canvas.DrawBorder(rect, round, style.border_width / scale, style.border_color);
    if (Checked()) {
        Rect dot = rect;
        const f32 inset = rect.w * 0.25f;
        dot.x += inset;
        dot.y += inset;
        dot.w -= inset * 2;
        dot.h -= inset * 2;
        canvas.DrawRoundedRect(dot, CornerRadii(dot.w / 2), GetSurface()->Themes().Color(ColorId::WindowBackground));
    }
}

void RadioButton::OnEvent(Event& event) {
    if (event.type == EventType::KeyDown && !event.handled) {
        int delta = 0;
        if (event.key == Key::Left || event.key == Key::Up) delta = -1;
        if (event.key == Key::Right || event.key == Key::Down) delta = 1;
        if (delta != 0) {
            event.handled = true;
            if (RadioGroup* group = Group()) group->Step(this, delta);
            return;
        }
    }
    Button::OnEvent(event);
}

// ---- RadioGroup ----

RadioGroup::RadioGroup(StringView name) : BoxLayout(Orientation::Vertical, name) { SetRole(kRole); }

void RadioGroup::Collect(Widget* widget, Vector<RadioButton*>& out) const {
    for (usize i = 0; i < widget->ChildCount(); ++i) {
        Widget* child = widget->ChildAt(i);
        if (child->Role() == kButtonRole) out.Push(static_cast<RadioButton*>(child));
        Collect(child, out);
    }
}

RadioButton* RadioGroup::Selected() const {
    Vector<RadioButton*> all;
    Collect(const_cast<RadioGroup*>(this), all);
    for (usize i = 0; i < all.Size(); ++i) {
        if (all[i]->Checked()) return all[i];
    }
    return nullptr;
}

void RadioGroup::Select(RadioButton* button) {
    Vector<RadioButton*> all;
    Collect(this, all);
    if (button != nullptr) {
        bool member = false;
        for (usize i = 0; i < all.Size(); ++i) member = member || all[i] == button;
        if (!member) return; // a button of another group (or none) cannot be selected here
    }
    RadioButton* before = Selected();
    for (usize i = 0; i < all.Size(); ++i) {
        all[i]->SetChecked(all[i] == button);
        // With a selection only the selected button is a Tab stop; the arrow keys move inside the group.
        all[i]->SetTabIndex(button == nullptr || all[i] == button ? 0 : -1);
    }
    if (before != button) on_changed_.Invoke(this);
}

void RadioGroup::Step(RadioButton* from, int delta) {
    Vector<RadioButton*> usable;
    Vector<RadioButton*> all;
    Collect(this, all);
    usize at = 0;
    for (usize i = 0; i < all.Size(); ++i) {
        if (all[i] == from) at = usable.Size();
        if (all[i]->Shown() && all[i]->EffectivelyEnabled()) usable.Push(all[i]);
    }
    if (usable.Empty()) return;
    const usize n = usable.Size();
    const usize next = delta < 0 ? (at + n - 1) % n : (at + 1) % n;
    Select(usable[next]);
    usable[next]->RequestFocus();
}

} // namespace engine_placeholder::ui
