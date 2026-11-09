#include "ui/widgets/tab_view.h"

#include <cmath>

#include "log/assert.h"
#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui {

TabView::TabView(StringView name) : Widget(name) { SetFocusable(true); }

usize TabView::AddTab(StringView title, UniquePtr<Widget> page) {
    Tab tab;
    tab.title.SetText(title);
    tab.closable = closable_;
    if (page.Get() != nullptr) tab.page = AddChild(static_cast<UniquePtr<Widget>&&>(page));
    tabs_.Push(static_cast<Tab&&>(tab));
    const usize index = tabs_.Size() - 1;
    if (tabs_[index].page != nullptr) tabs_[index].page->SetCollapsed(true);
    if (selected_ == kNone) ShowPage(index);
    MarkDirty(kLayoutDirty);
    return index;
}

usize TabView::AddLazyTab(StringView title, PageFactory factory, void* user) {
    Tab tab;
    tab.title.SetText(title);
    tab.closable = closable_;
    tab.factory = factory;
    tab.user = user;
    tabs_.Push(static_cast<Tab&&>(tab));
    const usize index = tabs_.Size() - 1;
    if (selected_ == kNone) ShowPage(index);
    MarkDirty(kLayoutDirty);
    return index;
}

void TabView::SetTabImage(usize index, ImageId image) {
    ENGINE_ASSERT(index < tabs_.Size(), "TabView::SetTabImage: index out of range");
    tabs_[index].image = image;
    MarkDirty(kLayoutDirty);
}

void TabView::SetTabClosable(usize index, bool closable) {
    ENGINE_ASSERT(index < tabs_.Size(), "TabView::SetTabClosable: index out of range");
    tabs_[index].closable = closable;
    MarkDirty(kLayoutDirty);
}

void TabView::SetTabTitle(usize index, StringView title) {
    ENGINE_ASSERT(index < tabs_.Size(), "TabView::SetTabTitle: index out of range");
    if (tabs_[index].title.SetText(title)) MarkDirty(kLayoutDirty);
}

Widget* TabView::Page(usize index) const { return index < tabs_.Size() ? tabs_[index].page : nullptr; }

void TabView::ShowPage(usize index) {
    // Builds a lazy page the first time, collapses the old page and shows the new one.
    if (index >= tabs_.Size()) return;
    Tab& tab = tabs_[index];
    if (tab.page == nullptr && tab.factory != nullptr) {
        UniquePtr<Widget> page = tab.factory(tab.user);
        tab.factory = nullptr; // once
        if (page.Get() != nullptr) tab.page = AddChild(static_cast<UniquePtr<Widget>&&>(page));
    }
    if (selected_ != kNone && selected_ < tabs_.Size() && tabs_[selected_].page != nullptr) tabs_[selected_].page->SetCollapsed(true);
    selected_ = index;
    if (tab.page != nullptr) tab.page->SetCollapsed(false);
    ScrollIntoView(index);
    MarkDirty(kLayoutDirty);
}

void TabView::Select(usize index) {
    if (index >= tabs_.Size() || index == selected_) return;
    ShowPage(index);
}

void TabView::UserSelect(usize index) {
    if (index >= tabs_.Size() || index == selected_) return;
    ShowPage(index);
    on_selection_.Invoke(this);
}

void TabView::CloseTab(usize index) {
    if (index >= tabs_.Size()) return;
    Widget* page = tabs_[index].page;
    const bool was_selected = index == selected_;
    tabs_.Remove(index);
    if (page != nullptr) RemoveChild(page);
    hover_ = kNone;
    if (tabs_.Empty()) {
        selected_ = kNone;
    } else if (was_selected) {
        selected_ = kNone;
        ShowPage(index < tabs_.Size() ? index : tabs_.Size() - 1);
    } else if (index < selected_) {
        --selected_;
    }
    MarkDirty(kLayoutDirty);
}

f32 TabView::CloseSize() const {
    Surface* surface = GetSurface();
    return surface == nullptr ? 0.0f : surface->Themes().Metric(MetricId::IconSize, 1.0f) * kCloseGlyph;
}

void TabView::Layout() {
    Surface* surface = GetSurface();
    if (surface == nullptr) return;
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTab, VariantId(0), 0, scale);
    const f32 pad_x = style.padding_x / scale;
    const f32 pad_y = style.padding_y / scale;
    const f32 gap = surface->Themes().Metric(MetricId::Spacing, 1.0f);
    const f32 icon = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    f32 x = 0;
    f32 h = icon;
    for (usize i = 0; i < tabs_.Size(); ++i) {
        Tab& t = tabs_[i];
        f32 w = 0;
        if (t.title.Text().Length() > 0 && t.title.EnsureForStyle(surface, style, 0)) {
            w = t.title.Width();
            h = std::fmax(h, t.title.Height());
        }
        if (t.image != 0) w += icon + (w > 0 ? gap : 0.0f);
        if (t.closable) w += gap + CloseSize();
        t.x = x;
        t.width = w + pad_x * 2;
        x += t.width;
    }
    total_w_ = x;
    header_h_ = h + pad_y * 2;
}

f32 TabView::Span() const {
    // The room for the headers: all of the width, less the two arrows when they do not fit.
    const f32 w = Bounds().w;
    return total_w_ > w ? std::fmax(w - header_h_ * 2, 0.0f) : w;
}

f32 TabView::MaxScroll() const { return std::fmax(total_w_ - Span(), 0.0f); }

bool TabView::HeaderOverflows() {
    Layout();
    return total_w_ > Bounds().w;
}

void TabView::SetScroll(f32 scroll) {
    const f32 clamped = std::fmin(std::fmax(scroll, 0.0f), MaxScroll());
    if (clamped == scroll_) return;
    scroll_ = clamped;
    MarkDirty(kDrawDirty);
}

void TabView::ScrollIntoView(usize index) {
    if (index >= tabs_.Size()) return;
    Layout();
    const Tab& t = tabs_[index];
    const f32 span = Span();
    if (t.x < scroll_) {
        SetScroll(t.x);
    } else if (t.x + t.width > scroll_ + span) {
        SetScroll(t.x + t.width - span);
    }
}

Rect TabView::TabRect(usize index) {
    if (index >= tabs_.Size()) return Rect();
    Layout();
    return Rect{Bounds().x + tabs_[index].x - scroll_, Bounds().y, tabs_[index].width, header_h_};
}

Rect TabView::ContentRect() {
    Layout();
    const Rect& b = Bounds();
    return Rect{b.x, b.y + header_h_, b.w, std::fmax(b.h - header_h_, 0.0f)};
}

Size TabView::Measure(const Constraints& constraints) {
    Layout();
    Size size{0, 0};
    for (usize i = 0; i < tabs_.Size(); ++i) {
        if (tabs_[i].page == nullptr) continue;
        const Size s = tabs_[i].page->MeasureTotal(constraints);
        size.w = std::fmax(size.w, s.w);
        size.h = std::fmax(size.h, s.h);
    }
    size.h += header_h_;
    return size;
}

void TabView::Arrange(const Rect& rect) {
    SetBounds(rect);
    Layout();
    SetScroll(scroll_);
    const Rect content = ContentRect();
    for (usize i = 0; i < tabs_.Size(); ++i) {
        Widget* page = tabs_[i].page;
        if (page == nullptr) continue;
        page->SetCollapsed(i != selected_);
        if (i == selected_) page->Arrange(content);
    }
}

TabView::HitInfo TabView::HitAt(Point local) {
    HitInfo info;
    if (local.y < 0 || local.y >= header_h_ || local.x < 0 || local.x >= Bounds().w) return info;
    const bool overflow = total_w_ > Bounds().w;
    if (overflow && local.x >= Span()) {
        info.kind = local.x < Span() + header_h_ ? Hit::ArrowLeft : Hit::ArrowRight;
        return info;
    }
    const f32 x = local.x + scroll_;
    for (usize i = 0; i < tabs_.Size(); ++i) {
        const Tab& t = tabs_[i];
        if (x < t.x || x >= t.x + t.width) continue;
        info.kind = Hit::Tab;
        info.tab = i;
        if (t.closable) {
            const f32 gap = GetSurface()->Themes().Metric(MetricId::Spacing, 1.0f);
            const f32 pad_x = GetSurface()->Themes().Resolve(widget_kind::kTab, VariantId(0), 0, GetSurface()->Scale()).padding_x / GetSurface()->Scale();
            if (x >= t.x + t.width - pad_x - CloseSize() - gap / 2) info.kind = Hit::Close;
        }
        return info;
    }
    return info;
}

void TabView::Draw(Canvas& canvas) {
    Surface* surface = GetSurface();
    Layout();
    const f32 scale = surface->Scale();
    const Rect& b = Bounds();
    const f32 pad_x = surface->Themes().Resolve(widget_kind::kTab, VariantId(0), 0, scale).padding_x / scale;
    const f32 gap = surface->Themes().Metric(MetricId::Spacing, 1.0f);
    const f32 icon = surface->Themes().Metric(MetricId::IconSize, 1.0f);
    const u32 border = surface->Themes().Color(ColorId::Border);
    const u32 accent = surface->Themes().Color(ColorId::Accent);
    canvas.DrawRect(Rect{b.x, b.y + header_h_ - 1, b.w, 1}, border);
    const f32 span = Span();
    canvas.PushClip(Rect{b.x, b.y, span, header_h_});
    for (usize i = 0; i < tabs_.Size(); ++i) {
        const Tab& t = tabs_[i];
        const Rect r{b.x + t.x - scroll_, b.y, t.width, header_h_};
        if (r.Right() < b.x || r.x > b.x + span) continue;
        u32 state = 0;
        if (i == selected_) state |= kStateChecked;
        if (i == hover_) state |= kStateHover;
        const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTab, VariantId(0), state, scale);
        DrawStyleBox(canvas, r, style, scale);
        f32 x = r.x + pad_x;
        if (t.image != 0) {
            Rect uv;
            uv.w = 1;
            uv.h = 1;
            canvas.DrawImage(Rect{x, r.y + (r.h - icon) / 2, icon, icon}, t.image, uv, ~0u);
            x += icon + gap;
        }
        if (t.title.Text().Length() > 0) canvas.DrawText(t.title.Full(), t.title.Style(), Point{x, r.y + (r.h - t.title.Height()) / 2}, style.text_color);
        if (t.closable) {
            const f32 s = CloseSize();
            const f32 cx = r.Right() - pad_x - s / 2;
            const f32 cy = r.y + r.h / 2;
            const u32 ink = (i == hover_ && hover_close_) ? accent : style.text_color;
            canvas.DrawLine(Point{cx - s / 2, cy - s / 2}, Point{cx + s / 2, cy + s / 2}, 1.0f, ink);
            canvas.DrawLine(Point{cx - s / 2, cy + s / 2}, Point{cx + s / 2, cy - s / 2}, 1.0f, ink);
        }
        if (i == selected_) {
            canvas.DrawRect(Rect{r.x, r.Bottom() - kUnderline, r.w, kUnderline}, accent);
            if ((State() & kStateFocused) != 0) canvas.DrawBorder(r, CornerRadii(), 1.0f, accent);
        }
    }
    canvas.PopClip();
    if (total_w_ > b.w) {
        const u32 ink = surface->Themes().Color(ColorId::Text);
        const u32 dim = surface->Themes().Color(ColorId::TextDisabled);
        const f32 reach = header_h_ * kArrowReach;
        const f32 ay = b.y + header_h_ / 2;
        const f32 lx = b.x + span + header_h_ / 2;
        const f32 rx = lx + header_h_;
        canvas.DrawTriangle(Point{lx + reach, ay - reach}, Point{lx + reach, ay + reach}, Point{lx - reach, ay}, scroll_ > 0 ? ink : dim);
        canvas.DrawTriangle(Point{rx - reach, ay - reach}, Point{rx - reach, ay + reach}, Point{rx + reach, ay}, scroll_ < MaxScroll() ? ink : dim);
    }
}

void TabView::OnEvent(Event& event) {
    Layout();
    switch (event.type) {
        case EventType::PointerMove: {
            const HitInfo hit = HitAt(event.position);
            const usize tab = (hit.kind == Hit::Tab || hit.kind == Hit::Close) ? hit.tab : kNone;
            const bool close = hit.kind == Hit::Close;
            if (tab != hover_ || close != hover_close_) {
                hover_ = tab;
                hover_close_ = close;
                MarkDirty(kDrawDirty);
            }
            break;
        }
        case EventType::PointerLeave:
            if (hover_ != kNone) {
                hover_ = kNone;
                hover_close_ = false;
                MarkDirty(kDrawDirty);
            }
            break;
        case EventType::PointerDown: {
            if (event.button != os::kMouseLeft) break;
            const HitInfo hit = HitAt(event.position);
            if (hit.kind == Hit::None) break;
            event.handled = true;
            const f32 step = header_h_ * kWheelHeights;
            switch (hit.kind) {
                case Hit::ArrowLeft:
                    SetScroll(scroll_ - step);
                    break;
                case Hit::ArrowRight:
                    SetScroll(scroll_ + step);
                    break;
                case Hit::Close:
                    on_close_.Invoke(this, hit.tab);
                    break;
                default:
                    UserSelect(hit.tab);
                    break;
            }
            break;
        }
        case EventType::Wheel:
            if (event.position.y >= 0 && event.position.y < header_h_ && total_w_ > Bounds().w) {
                event.handled = true;
                SetScroll(scroll_ + event.wheel_y * header_h_ * kWheelHeights);
            }
            break;
        case EventType::KeyDown: {
            const usize n = tabs_.Size();
            if (n == 0) break;
            const bool ctrl = (event.modifiers & os::kModCtrl) != 0;
            const bool shift = (event.modifiers & os::kModShift) != 0;
            const usize cur = selected_ == kNone ? 0 : selected_;
            if (ctrl && (event.key == Key::Tab || event.key == Key::PageDown || event.key == Key::PageUp)) {
                const bool back = event.key == Key::PageUp || (event.key == Key::Tab && shift);
                event.handled = true;
                UserSelect(back ? (cur + n - 1) % n : (cur + 1) % n);
            } else if (!ctrl && GetSurface()->Focus() == this) {
                switch (event.key) {
                    case Key::Left:
                        event.handled = true;
                        if (cur > 0) UserSelect(cur - 1);
                        break;
                    case Key::Right:
                        event.handled = true;
                        if (cur + 1 < n) UserSelect(cur + 1);
                        break;
                    case Key::Home:
                        event.handled = true;
                        UserSelect(0);
                        break;
                    case Key::End:
                        event.handled = true;
                        UserSelect(n - 1);
                        break;
                    default:
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
