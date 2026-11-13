#include "ui/widgets/menu_bar.h"

#include "lang/scalar.h"

#include "text/cursor.h"
#include "ui/surface.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui
{

MenuBar::MenuBar(StringView name) : Widget(name)
{
    SetFocusable(true);
}

usize MenuBar::AddMenu(StringView text, Menu* menu)
{
    Title title;
    Vector<char> label;
    title.mnemonic = SplitMnemonic(text, &label, &title.mnemonic_offset);
    title.run.SetText(StringView(label.Data(), label.Size()));
    title.menu = menu;
    titles_.Push(static_cast<Title&&>(title));
    MarkDirty(kLayoutDirty);
    return titles_.Size() - 1;
}

bool MenuBar::MenuOpen() const
{
    return open_ != kNone && popup_ != nullptr && GetSurface() != nullptr && GetSurface()->IsPopupOpen(popup_);
}

Size MenuBar::Measure(const Constraints&)
{
    return Layout();
}

Size MenuBar::Layout()
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return Size();
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), 0, scale);
    const f32 pad_x = style.padding_x / scale;
    const f32 pad_y = style.padding_y / scale;
    f32 x = 0;
    f32 h = 0;
    for (usize i = 0; i < titles_.Size(); ++i)
    {
        Title& t = titles_[i];
        f32 w = 0;
        if (t.run.EnsureForStyle(surface, style, 0))
        {
            w = t.run.Width();
            h = FMax(h, t.run.Height());
        }
        t.x = x;
        t.width = w + pad_x * 2;
        x += t.width;
    }
    height_ = h + pad_y * 2;
    return Size{x, height_};
}

usize MenuBar::TitleAt(f32 local_x) const
{
    for (usize i = 0; i < titles_.Size(); ++i)
    {
        if (local_x >= titles_[i].x && local_x < titles_[i].x + titles_[i].width)
        {
            return i;
        }
    }
    return kNone;
}

usize MenuBar::TitleWithMnemonic(char letter) const
{
    for (usize i = 0; i < titles_.Size(); ++i)
    {
        if (titles_[i].mnemonic == letter)
        {
            return i;
        }
    }
    return kNone;
}

void MenuBar::Draw(Canvas& canvas)
{
    Layout();
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const Rect& b = Bounds();
    DrawStyleBox(canvas, b, surface->Themes().Resolve(widget_kind::kPanel, VariantId(0), 0, scale), scale);
    const ResolvedStyle base = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), 0, scale);
    const f32 pad_x = base.padding_x / scale;
    const f32 pad_y = base.padding_y / scale;
    const bool underline = active_ != kNone;
    for (usize i = 0; i < titles_.Size(); ++i)
    {
        const Title& t = titles_[i];
        const u32 state = i == active_ ? kStateHover : 0;
        const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kMenu, VariantId(0), state, scale);
        const Rect r{b.x + t.x, b.y, t.width, b.h};
        if (IsVisibleColor(style.background))
        {
            canvas.DrawRect(r, style.background);
        }
        if (t.run.Full().glyphs.Empty())
        {
            continue;
        }
        const f32 ty = b.y + pad_y;
        canvas.DrawText(t.run.Full(), t.run.Style(), Point{r.x + pad_x, ty}, style.text_color);
        if (underline && t.mnemonic != 0)
        {
            const text::CursorPosition from = text::PositionOfOffset(t.run.Full(), t.mnemonic_offset);
            const text::CursorPosition to = text::PositionOfOffset(t.run.Full(),
                                                                   text::NextBoundary(t.run.Full(), t.mnemonic_offset));
            const f32 y = ty + from.height / scale;
            canvas.DrawLine(Point{r.x + pad_x + from.x / scale, y}, Point{r.x + pad_x + to.x / scale, y},
                            MenuView::kStroke, style.text_color);
        }
    }
}

void MenuBar::MenuEdge(void* user, MenuView*, int direction)
{
    static_cast<MenuBar*>(user)->Step(direction);
}

void MenuBar::OpenMenu(usize index)
{
    Layout();
    Surface* surface = GetSurface();
    if (surface == nullptr || index >= titles_.Size() || titles_[index].menu == nullptr)
    {
        return;
    }
    CloseMenu();
    UniquePtr<MenuView> view = MakeUnique<MenuView>(titles_[index].menu);
    view->OnEdge().Connect(&MenuBar::MenuEdge, this);
    MenuView* raw = view.Get();
    const Title& t = titles_[index];
    const Rect anchor{Bounds().x + t.x, Bounds().y, t.width, Bounds().h};
    PopupOptions options;
    options.placement = Placement::Below;
    options.owner = this;
    active_ = index;
    open_ = index;
    popup_ = surface->OpenPopup(UniquePtr<Widget>(view.Release()), anchor, options);
    raw->HighlightFirst();
    MarkDirty(kDrawDirty);
}

void MenuBar::CloseMenu()
{
    Surface* surface = GetSurface();
    if (popup_ != nullptr && surface != nullptr && surface->IsPopupOpen(popup_))
    {
        surface->ClosePopup(popup_);
    }
    popup_ = nullptr;
    open_ = kNone;
}

void MenuBar::Enter()
{
    Surface* surface = GetSurface();
    if (titles_.Empty() || surface == nullptr)
    {
        return;
    }
    if (surface->Focus() != this)
    {
        surface->SaveFocus();
    }
    surface->SetFocus(this);
    active_ = 0;
    MarkDirty(kDrawDirty);
}

void MenuBar::Leave()
{
    const bool had_menu = open_ != kNone;
    CloseMenu();
    active_ = kNone;
    Surface* surface = GetSurface();
    if (surface != nullptr && (!had_menu || surface->Focus() == this))
    {
        surface->RestoreSavedFocus();
    }
    MarkDirty(kDrawDirty);
}

void MenuBar::Step(int direction)
{
    const usize n = titles_.Size();
    if (n == 0 || active_ == kNone)
    {
        return;
    }
    const usize next = direction > 0 ? (active_ + 1) % n : (active_ + n - 1) % n;
    if (open_ != kNone)
    {
        OpenMenu(next);
    }
    else
    {
        active_ = next;
        MarkDirty(kDrawDirty);
    }
}

void MenuBar::Update(f32)
{
    if (open_ != kNone && !MenuOpen())
    {
        // The menu closed (an item was chosen, Escape, an outside click): the bar is no longer active.
        open_ = kNone;
        popup_ = nullptr;
        active_ = kNone;
        if (GetSurface()->Focus() == this)
        {
            GetSurface()->RestoreSavedFocus();  // the bar took the focus from someone: give it back
        }
        MarkDirty(kDrawDirty);
    }
}

void MenuBar::OnEvent(Event& event)
{
    Layout();
    switch (event.type)
    {
    case EventType::PointerMove:
    {
        const usize i = TitleAt(event.position.x);
        if (i != kNone && i != active_ && open_ != kNone)
        {
            OpenMenu(i);
        }
        break;
    }
    case EventType::PointerDown:
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        event.handled = true;
        if (const usize i = TitleAt(event.position.x); i != kNone)
        {
            OpenMenu(i);
        }
        break;
    case EventType::FocusLost:
        if (open_ == kNone && active_ != kNone)
        {
            active_ = kNone;
            MarkDirty(kDrawDirty);
        }
        break;
    case EventType::KeyUp:
        if (event.key == Key::LeftAlt || event.key == Key::RightAlt)
        {
            event.handled = true;
            if (active_ == kNone)
            {
                Enter();
            }
            else
            {
                Leave();
            }
        }
        break;
    case EventType::KeyDown:
    {
        const bool alt = (event.modifiers & os::kModAlt) != 0;
        const bool plain = (event.modifiers & (os::kModCtrl | os::kModAlt | os::kModShift | os::kModSuper)) == 0;
        const char letter = KeyLetter(event.key);
        if (event.key == Key::F10 && plain)
        {
            event.handled = true;
            if (active_ == kNone)
            {
                Enter();
            }
            else
            {
                Leave();
            }
        }
        else if (alt && letter != 0)
        {
            const usize i = TitleWithMnemonic(letter);
            if (i != kNone)
            {
                event.handled = true;
                if (GetSurface()->Focus() != this)
                {
                    GetSurface()->SaveFocus();
                }
                GetSurface()->SetFocus(this);
                OpenMenu(i);
            }
        }
        else if (active_ != kNone && plain)
        {
            event.handled = true;
            switch (event.key)
            {
            case Key::Left: Step(-1); break;
            case Key::Right: Step(1); break;
            case Key::Down:
            case Key::Enter:
            case Key::Space: OpenMenu(active_); break;
            case Key::Escape: Leave(); break;
            default:
            {
                const usize i = letter != 0 ? TitleWithMnemonic(letter) : kNone;
                if (i != kNone)
                {
                    OpenMenu(i);
                }
                else
                {
                    event.handled = false;
                }
                break;
            }
            }
        }
        break;
    }
    default: break;
    }
}

}  // namespace engine_placeholder::ui
