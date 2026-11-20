#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/popup.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/text_field.h"
#include "ui/widgets/tooltip.h"

using namespace vse;
using namespace vse::ui;

namespace
{

Rect PRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// A focusable popup content of a fixed size that records the keys it saw.
class PopupPane : public Widget
{
public:
    PopupPane(f32 w, f32 h) : Widget(StringView("pane")), size_(Size{w, h}) { SetFocusable(true); }
    Size Measure(const Constraints&) override { return size_; }
    void OnEvent(Event& e) override
    {
        if (e.type == EventType::KeyDown && e.key == Key::A)
        {
            ++a_keys;
            e.handled = true;
        }
    }
    int a_keys = 0;

private:
    Size size_;
};

struct PopupRig
{
    PopupRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<Button> b = MakeUnique<Button>(StringView("behind"), StringView("behind"));
        button = b.Get();
        button->SetBounds(PRect(150, 100, 100, 30));
        button->OnClick().Connect(&PopupRig::Clicked, this);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(b.Release()));
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
        field = f.Get();
        field->SetBounds(PRect(10, 250, 100, 30));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        h.Advance(0);
    }
    static void Clicked(void* user, Button*) { ++static_cast<PopupRig*>(user)->clicks; }
    PopupPane* Open(const Rect& anchor, const PopupOptions& options = PopupOptions(), f32 w = 80, f32 height = 60)
    {
        UniquePtr<PopupPane> p = MakeUnique<PopupPane>(w, height);
        PopupPane* raw = p.Get();
        h.GetSurface().OpenPopup(UniquePtr<Widget>(p.Release()), anchor, options);
        h.Advance(0);
        return raw;
    }
    Harness h;
    Button* button = nullptr;
    TextField* field = nullptr;
    int clicks = 0;
};

}  // namespace

TEST_CASE("PlacePopup prefers its side and flips at each edge", "[ui][popup]")
{
    const Rect bounds = PRect(0, 0, 400, 300);
    const Size size{100, 80};
    // Below, with room.
    Rect r = PlacePopup(size, PRect(10, 10, 50, 20), Placement::Below, bounds);
    CHECK(r.x == 10);
    CHECK(r.y == 30);
    // Below, no room: flips above.
    r = PlacePopup(size, PRect(10, 250, 50, 20), Placement::Below, bounds);
    CHECK(r.y == 170);
    // Above, no room: flips below.
    r = PlacePopup(size, PRect(10, 20, 50, 20), Placement::Above, bounds);
    CHECK(r.y == 40);
    // Right, no room: flips left.
    r = PlacePopup(size, PRect(350, 50, 40, 20), Placement::Right, bounds);
    CHECK(r.x == 250);
    CHECK(r.y == 50);
    // Left, no room: flips right.
    r = PlacePopup(size, PRect(20, 50, 40, 20), Placement::Left, bounds);
    CHECK(r.x == 60);
}

TEST_CASE("PlacePopup shifts along the edge and starts inside when too large", "[ui][popup]")
{
    const Rect bounds = PRect(0, 0, 400, 300);
    Rect r = PlacePopup(Size{100, 80}, PRect(350, 10, 40, 20), Placement::Below, bounds);
    CHECK(r.x == 300);  // shifted left to fit
    r = PlacePopup(Size{100, 80}, PRect(10, 290, 40, 20), Placement::Right, bounds);
    CHECK(r.y == 220);  // shifted up to fit
    r = PlacePopup(Size{100, 500}, PRect(10, 10, 40, 20), Placement::Below, bounds);
    CHECK(r.y >= 0);
    CHECK(r.x >= 0);
}

TEST_CASE("an outside press closes the popup and is consumed", "[ui][popup]")
{
    PopupRig rig;
    rig.Open(PRect(10, 10, 50, 20));
    REQUIRE(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Click(200, 115);  // on the button behind
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    CHECK(rig.clicks == 0);
}

TEST_CASE("an outside press can be passed through", "[ui][popup]")
{
    PopupRig rig;
    PopupOptions options;
    options.consume_outside_click = false;
    rig.Open(PRect(10, 10, 50, 20), options);
    rig.h.Click(200, 115);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    CHECK(rig.clicks == 1);
}

TEST_CASE("a press inside the popup keeps it open", "[ui][popup]")
{
    PopupRig rig;
    rig.Open(PRect(10, 10, 50, 20));
    rig.h.Click(30, 50);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
}

TEST_CASE("closing a popup closes the ones opened after it", "[ui][popup]")
{
    PopupRig rig;
    PopupPane* a = rig.Open(PRect(10, 10, 50, 20));
    PopupPane* b = rig.Open(PRect(100, 10, 50, 20));
    PopupPane* c = rig.Open(PRect(200, 10, 50, 20));
    REQUIRE(rig.h.GetSurface().PopupCount() == 3);
    rig.h.GetSurface().ClosePopup(b);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    CHECK(rig.h.GetSurface().IsPopupOpen(a));
    CHECK_FALSE(rig.h.GetSurface().IsPopupOpen(c));
    // An outside press closes the rest.
    rig.h.Click(300, 200);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("Escape closes only the top popup", "[ui][popup]")
{
    PopupRig rig;
    PopupPane* a = rig.Open(PRect(10, 10, 50, 20));
    rig.Open(PRect(100, 10, 50, 20));
    rig.h.Key(Key::Escape);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    CHECK(rig.h.GetSurface().IsPopupOpen(a));
    rig.h.Key(Key::Escape);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("the popup takes the focus and the previous owner gets it back", "[ui][popup]")
{
    PopupRig rig;
    rig.h.GetSurface().SetFocus(rig.field);
    PopupPane* p = rig.Open(PRect(10, 10, 50, 20));
    CHECK(rig.h.GetSurface().Focus() == p);
    rig.h.Key(Key::Escape);
    CHECK(rig.h.GetSurface().Focus() == rig.field);
}

TEST_CASE("nested popups hand the focus down one level at a time", "[ui][popup]")
{
    PopupRig rig;
    rig.h.GetSurface().SetFocus(rig.field);
    PopupPane* a = rig.Open(PRect(10, 10, 50, 20));
    PopupPane* b = rig.Open(PRect(100, 10, 50, 20));
    CHECK(rig.h.GetSurface().Focus() == b);
    rig.h.Key(Key::Escape);
    CHECK(rig.h.GetSurface().Focus() == a);
    rig.h.Key(Key::Escape);
    CHECK(rig.h.GetSurface().Focus() == rig.field);
}

TEST_CASE("a popup that does not take the focus leaves it alone", "[ui][popup]")
{
    PopupRig rig;
    rig.h.GetSurface().SetFocus(rig.field);
    PopupOptions options;
    options.take_focus = false;
    rig.Open(PRect(10, 10, 50, 20), options);
    CHECK(rig.h.GetSurface().Focus() == rig.field);
}

TEST_CASE("destroying the anchor closes its popup", "[ui][popup]")
{
    PopupRig rig;
    PopupOptions options;
    options.owner = rig.button;
    rig.Open(PRect(150, 100, 100, 30), options);
    REQUIRE(rig.h.GetSurface().PopupCount() == 1);
    rig.h.GetSurface().Root()->RemoveChild(rig.button);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("a press on the owner while its popup is open closes it and is consumed", "[ui][popup]")
{
    PopupRig rig;
    PopupOptions options;
    options.owner = rig.button;
    rig.Open(PRect(150, 100, 100, 30), options);
    rig.h.Click(200, 115);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    CHECK(rig.clicks == 0);
}

TEST_CASE("losing the window focus closes every popup", "[ui][popup]")
{
    PopupRig rig;
    rig.Open(PRect(10, 10, 50, 20));
    rig.Open(PRect(100, 10, 50, 20));
    Event e;
    e.type = EventType::WindowFocusLost;
    rig.h.GetSurface().Post(e);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("a context menu is clamped inside the surface", "[ui][popup]")
{
    PopupRig rig;
    UniquePtr<PopupPane> p = MakeUnique<PopupPane>(100.0f, 80.0f);
    Widget* w = rig.h.GetSurface().ShowContextMenu(UniquePtr<Widget>(p.Release()), Point{395, 295});
    rig.h.Advance(0);
    REQUIRE(w != nullptr);
    const Rect b = w->Bounds();
    CHECK(b.x >= 0);
    CHECK(b.y >= 0);
    CHECK(b.Right() <= 400);
    CHECK(b.Bottom() <= 300);
}

namespace
{
struct PopupContextRig
{
    PopupRig rig;
    int calls = 0;
    Point where;
    static void Handler(void* user, Widget*, Point position)
    {
        PopupContextRig* c = static_cast<PopupContextRig*>(user);
        ++c->calls;
        c->where = position;
    }
};
}  // namespace

TEST_CASE("a right press runs the context handler of the widget under it", "[ui][popup]")
{
    PopupContextRig c;
    c.rig.button->SetContextHandler(&PopupContextRig::Handler, &c);
    c.rig.h.PointerDown(200, 115, os::kMouseRight);
    c.rig.h.PointerUp(200, 115, os::kMouseRight);
    CHECK(c.calls == 1);
    CHECK(c.where.x == 200);
    CHECK(c.where.y == 115);
    // A left press does not.
    c.rig.h.Click(200, 115);
    CHECK(c.calls == 1);
}

namespace
{
struct AccelCounter
{
    int fired = 0;
    static void Fire(void* user) { ++static_cast<AccelCounter*>(user)->fired; }
};
}  // namespace

TEST_CASE("an accelerator runs for its chord and can be turned off", "[ui][accelerator]")
{
    PopupRig rig;
    AccelCounter counter;
    Surface& s = rig.h.GetSurface();
    KeyChord chord;
    chord.key = Key::S;
    chord.modifiers = os::kModCtrl;
    const u32 id = s.RegisterAccelerator(chord, &AccelCounter::Fire, &counter);
    REQUIRE(id != 0);
    CHECK(s.RegisterAccelerator(chord, &AccelCounter::Fire, &counter) == 0);  // a chord has one accelerator
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(counter.fired == 1);
    rig.h.Key(Key::S);  // without the modifier
    CHECK(counter.fired == 1);
    s.SetAcceleratorEnabled(id, false);
    CHECK_FALSE(s.AcceleratorEnabled(id));
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(counter.fired == 1);
    s.SetAcceleratorEnabled(id, true);
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(counter.fired == 2);
    s.UnregisterAccelerator(id);
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(counter.fired == 2);
    CHECK(s.RegisterAccelerator(chord, &AccelCounter::Fire, &counter) != 0);  // free again
}

TEST_CASE("the focus owner sees a key before the accelerators", "[ui][accelerator]")
{
    PopupRig rig;
    AccelCounter counter;
    Surface& s = rig.h.GetSurface();
    KeyChord chord;
    chord.key = Key::A;
    s.RegisterAccelerator(chord, &AccelCounter::Fire, &counter);
    PopupPane* p = rig.Open(PRect(10, 10, 50, 20));  // takes the focus and handles A
    rig.h.Key(Key::A);
    CHECK(p->a_keys == 1);
    CHECK(counter.fired == 0);
    rig.h.Key(Key::Escape);
    rig.h.Key(Key::A);  // nothing handles it now
    CHECK(counter.fired == 1);
}

TEST_CASE("the chord text names the modifiers in order", "[ui][accelerator]")
{
    KeyChord chord;
    chord.key = Key::S;
    chord.modifiers = os::kModCtrl | os::kModShift;
    CHECK(ChordText(chord).View() == StringView("Ctrl+Shift+S"));
    chord.key = Key::F10;
    chord.modifiers = 0;
    CHECK(ChordText(chord).View() == StringView("F10"));
}

// ---- Tooltips ----

TEST_CASE("a tooltip shows after the hover delay and hides when the pointer leaves", "[ui][tooltip]")
{
    PopupRig rig;
    Surface& s = rig.h.GetSurface();
    rig.button->SetTooltip(StringView("hello"));
    rig.h.PointerMove(200, 115);
    rig.h.Advance(static_cast<f32>(Surface::kTooltipDelay) * 0.5f);
    CHECK_FALSE(s.TooltipShown());
    rig.h.Advance(static_cast<f32>(Surface::kTooltipDelay) * 0.6f);
    CHECK(s.TooltipShown());
    rig.h.PointerMove(20, 20);
    CHECK_FALSE(s.TooltipShown());
}

TEST_CASE("a widget without a tooltip text shows none", "[ui][tooltip]")
{
    PopupRig rig;
    rig.h.PointerMove(200, 115);
    rig.h.Advance(static_cast<f32>(Surface::kTooltipDelay) * 2);
    CHECK_FALSE(rig.h.GetSurface().TooltipShown());
}

TEST_CASE("a press, a wheel turn and a popup hide the tooltip", "[ui][tooltip]")
{
    PopupRig rig;
    Surface& s = rig.h.GetSurface();
    rig.button->SetTooltip(StringView("hello"));
    const f32 wait = static_cast<f32>(Surface::kTooltipDelay) * 1.2f;
    rig.h.PointerMove(200, 115);
    rig.h.Advance(wait);
    REQUIRE(s.TooltipShown());
    rig.h.PointerDown(200, 115);
    CHECK_FALSE(s.TooltipShown());
    rig.h.PointerUp(200, 115);
    rig.h.Advance(wait);  // the pointer is still: it does not come back until it moves
    CHECK_FALSE(s.TooltipShown());
    rig.h.PointerMove(205, 115);
    rig.h.Advance(wait);
    REQUIRE(s.TooltipShown());
    rig.h.Wheel(205, 115, 0, 1);
    CHECK_FALSE(s.TooltipShown());
    rig.h.PointerMove(200, 115);
    rig.h.Advance(wait);
    REQUIRE(s.TooltipShown());
    rig.Open(PRect(10, 10, 50, 20));
    CHECK_FALSE(s.TooltipShown());
}

TEST_CASE("the tooltip stays inside the surface", "[ui][tooltip]")
{
    PopupRig rig;
    Surface& s = rig.h.GetSurface();
    UniquePtr<Button> b = MakeUnique<Button>(StringView("corner"), StringView("x"));
    Button* corner = b.Get();
    corner->SetBounds(PRect(360, 270, 40, 30));
    corner->SetTooltip(StringView("a rather long tooltip text that cannot fit at the corner"));
    s.Root()->AddChild(UniquePtr<Widget>(b.Release()));
    rig.h.Advance(0);
    rig.h.PointerMove(398, 298);
    rig.h.Advance(static_cast<f32>(Surface::kTooltipDelay) * 1.2f);
    REQUIRE(s.TooltipShown());
    // The tooltip layer is the last child of the popup layer's sibling; find the bubble by its bounds: it must lie
    // inside.
    bool found = false;
    for (usize i = 0; i < s.TooltipLayer()->ChildCount(); ++i)
    {
        const Rect r = s.TooltipLayer()->ChildAt(i)->Bounds();
        found = true;
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
        CHECK(r.Right() <= 400.5f);
        CHECK(r.Bottom() <= 300.5f);
    }
    CHECK(found);
}

TEST_CASE("PlacePopup keeps a popup that fits neither side inside the bounds", "[ui][popup]")
{
    const Rect bounds = PRect(0, 0, 400, 600);
    // 400 high, anchor at y 280..300: 300 below and 280 above are both too little.
    Rect r = PlacePopup(Size{100, 400}, PRect(10, 280, 50, 20), Placement::Below, bounds);
    CHECK(r.y >= 0);
    CHECK(r.y + r.h <= 600);  // the far edge was at 700 before: its last items could not be reached
    // The same across.
    r = PlacePopup(Size{300, 100}, PRect(150, 10, 100, 20), Placement::Right, bounds);
    CHECK(r.x >= 0);
    CHECK(r.x + r.w <= 400);
}
