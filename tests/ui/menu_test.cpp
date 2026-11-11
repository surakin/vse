#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/menu.h"
#include "ui/widgets/menu_bar.h"
#include "ui/widgets/text_field.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

Rect MRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// Items: 0 New, 1 separator, 2 Open (disabled), 3 Save, 4 check Grid, 5 radio One, 6 radio Two, 7 submenu Recent{0
// Alpha, 1 Beta}.
struct MenuRig
{
    MenuRig() : h(500, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        menu.AddItem(StringView("&New"), &MenuRig::Run, this);
        menu.AddSeparator();
        menu.AddItem(StringView("&Open"), &MenuRig::Run, this);
        menu.SetItemEnabled(2, false);
        menu.AddItem(StringView("S&ave"), &MenuRig::Run, this);
        menu.AddCheck(StringView("Show &grid"), false, &MenuRig::Run, this);
        menu.AddRadio(StringView("One"), true, &MenuRig::Run, this);
        menu.AddRadio(StringView("Two"), false, &MenuRig::Run, this);
        UniquePtr<Menu> sub = MakeUnique<Menu>();
        sub->AddItem(StringView("&Alpha"), &MenuRig::RunSub, this);
        sub->AddItem(StringView("Beta"), &MenuRig::RunSub, this);
        menu.AddSubmenu(StringView("&Recent"), static_cast<UniquePtr<Menu>&&>(sub));
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
        field = f.Get();
        field->SetBounds(MRect(300, 250, 100, 30));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        h.Advance(0);
    }
    static void Run(void* user, usize index)
    {
        MenuRig* r = static_cast<MenuRig*>(user);
        r->last = static_cast<int>(index);
        ++r->runs;
    }
    static void RunSub(void* user, usize index)
    {
        MenuRig* r = static_cast<MenuRig*>(user);
        r->last_sub = static_cast<int>(index);
        ++r->runs;
    }
    MenuView* Open()
    {
        UniquePtr<MenuView> v = MakeUnique<MenuView>(&menu);
        MenuView* raw = v.Get();
        h.GetSurface().OpenPopup(UniquePtr<Widget>(v.Release()), MRect(20, 20, 60, 20), PopupOptions());
        h.Advance(0);
        return raw;
    }
    MenuView* Top() { return static_cast<MenuView*>(h.GetSurface().TopPopup()); }
    Point Center(MenuView* v, usize row)
    {
        const Rect r = v->RowRect(row);
        return Point{r.x + r.w / 2, r.y + r.h / 2};
    }
    Harness h;
    Menu menu;
    TextField* field = nullptr;
    int last = -1;
    int last_sub = -1;
    int runs = 0;
};

}  // namespace

TEST_CASE("SplitMnemonic finds the marked letter and unescapes ampersands", "[ui][menu]")
{
    Vector<char> label;
    u32 offset = 99;
    CHECK(SplitMnemonic(StringView("&File"), &label, &offset) == 'f');
    CHECK(StringView(label.Data(), label.Size()) == StringView("File"));
    CHECK(offset == 0);
    CHECK(SplitMnemonic(StringView("Save &As"), &label, &offset) == 'a');
    CHECK(StringView(label.Data(), label.Size()) == StringView("Save As"));
    CHECK(offset == 5);
    offset = 99;
    CHECK(SplitMnemonic(StringView("Tom && Jerry"), &label, &offset) == 0);
    CHECK(StringView(label.Data(), label.Size()) == StringView("Tom & Jerry"));
    CHECK(offset == 99);
    CHECK(SplitMnemonic(StringView("A && &B"), &label, &offset) == 'b');
    CHECK(StringView(label.Data(), label.Size()) == StringView("A & B"));
    CHECK(SplitMnemonic(StringView("trailing &"), &label, &offset) == 0);
    CHECK(StringView(label.Data(), label.Size()) == StringView("trailing &"));
}

TEST_CASE("the model keeps radio items exclusive within their run and toggles checks", "[ui][menu]")
{
    MenuRig rig;
    CHECK(rig.menu.Item(5).checked);
    CHECK(rig.menu.Choose(6));
    CHECK(rig.menu.Item(6).checked);
    CHECK_FALSE(rig.menu.Item(5).checked);
    CHECK(rig.menu.Choose(4));
    CHECK(rig.menu.Item(4).checked);
    CHECK(rig.menu.Choose(4));
    CHECK_FALSE(rig.menu.Item(4).checked);
    CHECK_FALSE(rig.menu.Choose(2));  // disabled
    CHECK_FALSE(rig.menu.Choose(1));  // separator
    CHECK_FALSE(rig.menu.Choose(7));  // submenu
    CHECK_FALSE(rig.menu.Choose(99));
}

TEST_CASE("arrow keys skip separators and disabled items and wrap", "[ui][menu]")
{
    MenuRig rig;
    MenuView* v = rig.Open();
    CHECK(rig.h.GetSurface().Focus() == v);
    rig.h.Key(Key::Down);
    CHECK(v->Highlight() == 0);
    rig.h.Key(Key::Down);
    CHECK(v->Highlight() == 3);  // past the separator and the disabled Open
    rig.h.Key(Key::Up);
    CHECK(v->Highlight() == 0);
    rig.h.Key(Key::Up);
    CHECK(v->Highlight() == 7);  // wrapped to the last
    rig.h.Key(Key::Down);
    CHECK(v->Highlight() == 0);  // and back
    rig.h.Key(Key::End);
    CHECK(v->Highlight() == 7);
    rig.h.Key(Key::Home);
    CHECK(v->Highlight() == 0);
}

TEST_CASE("Enter runs the action and closes every popup", "[ui][menu]")
{
    MenuRig rig;
    rig.Open();
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Enter);
    CHECK(rig.runs == 1);
    CHECK(rig.last == 3);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("a mnemonic letter chooses its item, a disabled one does nothing", "[ui][menu]")
{
    MenuRig rig;
    rig.Open();
    rig.h.Key(Key::O);  // Open is disabled
    CHECK(rig.runs == 0);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Key(Key::A);  // Save
    CHECK(rig.last == 3);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("choosing a check item toggles it and a radio item selects it", "[ui][menu]")
{
    MenuRig rig;
    rig.Open();
    rig.h.Key(Key::G);
    CHECK(rig.menu.Item(4).checked);
    rig.Open();
    rig.h.Key(Key::End);
    rig.h.Key(Key::Up);  // Two
    rig.h.Key(Key::Enter);
    CHECK(rig.menu.Item(6).checked);
    CHECK_FALSE(rig.menu.Item(5).checked);
}

TEST_CASE("the pointer highlights, and a click chooses", "[ui][menu]")
{
    MenuRig rig;
    MenuView* v = rig.Open();
    const Point save = rig.Center(v, 3);
    rig.h.PointerMove(save.x, save.y);
    CHECK(v->Highlight() == 3);
    const Point sep = rig.Center(v, 1);
    rig.h.PointerMove(sep.x, sep.y);
    CHECK(v->Highlight() == 3);  // a separator is not highlighted
    const Point disabled = rig.Center(v, 2);
    rig.h.Click(disabled.x, disabled.y);
    CHECK(rig.runs == 0);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Click(save.x, save.y);
    CHECK(rig.last == 3);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("Right opens a submenu and Left closes it", "[ui][menu]")
{
    MenuRig rig;
    MenuView* v = rig.Open();
    rig.h.Key(Key::End);
    REQUIRE(v->Highlight() == 7);
    rig.h.Key(Key::Right);
    REQUIRE(rig.h.GetSurface().PopupCount() == 2);
    MenuView* sub = rig.Top();
    CHECK(rig.h.GetSurface().Focus() == sub);
    CHECK(sub->Highlight() == 0);
    CHECK(sub->Bounds().x >= v->Bounds().Right() - 1);  // beside the parent
    rig.h.Key(Key::Left);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    CHECK(rig.h.GetSurface().Focus() == v);
    rig.h.Key(Key::Right);
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Enter);
    CHECK(rig.last_sub == 1);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("hovering a submenu item opens it after the delay", "[ui][menu]")
{
    MenuRig rig;
    MenuView* v = rig.Open();
    const Point p = rig.Center(v, 7);
    rig.h.PointerMove(p.x, p.y);
    rig.h.Advance(static_cast<f32>(MenuView::kSubmenuDelay) * 0.5f);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Advance(static_cast<f32>(MenuView::kSubmenuDelay) * 0.7f);
    CHECK(rig.h.GetSurface().PopupCount() == 2);
    CHECK(v->SubmenuOpen());
    CHECK(rig.h.GetSurface().Focus() == v);  // opened by the pointer: the focus stays
    // Moving to another item closes it.
    const Point q = rig.Center(v, 0);
    rig.h.PointerMove(q.x, q.y);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    CHECK_FALSE(v->SubmenuOpen());
}

TEST_CASE("leaving a submenu item before the delay cancels it", "[ui][menu]")
{
    MenuRig rig;
    MenuView* v = rig.Open();
    const Point p = rig.Center(v, 7);
    rig.h.PointerMove(p.x, p.y);
    rig.h.Advance(static_cast<f32>(MenuView::kSubmenuDelay) * 0.5f);
    const Point q = rig.Center(v, 0);
    rig.h.PointerMove(q.x, q.y);
    rig.h.Advance(static_cast<f32>(MenuView::kSubmenuDelay) * 2);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
}

TEST_CASE("an item whose accelerator is off cannot be chosen", "[ui][menu]")
{
    MenuRig rig;
    Surface& s = rig.h.GetSurface();
    KeyChord chord;
    chord.key = Key::S;
    chord.modifiers = os::kModCtrl;
    const u32 id = s.RegisterAccelerator(chord, [](void*) {}, nullptr);
    rig.menu.SetItemAccelerator(3, id);
    s.SetAcceleratorEnabled(id, false);
    MenuView* v = rig.Open();
    rig.h.Key(Key::Down);
    rig.h.Key(Key::Down);
    CHECK(v->Highlight() == 4);  // Save is skipped
    rig.h.Key(Key::Home);
    CHECK(v->Highlight() == 0);
    s.SetAcceleratorEnabled(id, true);
    rig.h.Key(Key::Down);
    CHECK(v->Highlight() == 3);
}

// ---- MenuBar ----

namespace
{
struct BarRig
{
    BarRig() : h(500, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        file.AddItem(StringView("&New"), &BarRig::Run, this);
        file.AddItem(StringView("&Quit"), &BarRig::Run, this);
        edit.AddItem(StringView("&Copy"), &BarRig::Run, this);
        UniquePtr<MenuBar> b = MakeUnique<MenuBar>(StringView("bar"));
        bar = b.Get();
        bar->AddMenu(StringView("&File"), &file);
        bar->AddMenu(StringView("&Edit"), &edit);
        bar->SetBounds(MRect(0, 0, 500, 24));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(b.Release()));
        h.GetSurface().SetMenuKeyTarget(bar);
        h.Advance(0);
    }
    static void Run(void* user, usize index)
    {
        BarRig* r = static_cast<BarRig*>(user);
        r->last = static_cast<int>(index);
        ++r->runs;
    }
    // The x in the middle of title `i`.
    f32 TitleX(usize i)
    {
        bar->Measure(Constraints());
        return bar->Bounds().x + (i == 0 ? 10.0f : 80.0f);
    }
    Harness h;
    Menu file;
    Menu edit;
    MenuBar* bar = nullptr;
    int last = -1;
    int runs = 0;
};
}  // namespace

TEST_CASE("a click on a title opens its menu below it", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Click(10, 10);
    REQUIRE(rig.h.GetSurface().PopupCount() == 1);
    CHECK(rig.bar->Active() == 0);
    CHECK(rig.h.GetSurface().TopPopup()->Bounds().y >= rig.bar->Bounds().Bottom() - 1);
    // A second click on the same title closes it.
    rig.h.Click(10, 10);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("with a menu open the pointer switches menus", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Click(10, 10);
    REQUIRE(rig.bar->MenuOpen());
    const f32 x = rig.bar->Bounds().x + 100;  // the Edit title
    // Find the Edit title by moving until the active title changes.
    f32 probe = 20;
    while (probe < 200 && rig.bar->Active() == 0)
    {
        rig.h.PointerMove(probe, 10);
        probe += 5;
    }
    (void)x;
    CHECK(rig.bar->Active() == 1);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
}

TEST_CASE("Right and Left in an open menu switch to the neighbouring menu", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Click(10, 10);
    REQUIRE(rig.bar->Active() == 0);
    rig.h.Key(Key::Right);
    CHECK(rig.bar->Active() == 1);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Key(Key::Left);
    CHECK(rig.bar->Active() == 0);
    rig.h.Key(Key::Left);  // wraps
    CHECK(rig.bar->Active() == 1);
}

TEST_CASE("F10 enters the bar, arrows move, Down opens, Escape leaves", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Key(Key::F10);
    CHECK(rig.bar->Active() == 0);
    CHECK(rig.h.GetSurface().Focus() == rig.bar);
    rig.h.Key(Key::Right);
    CHECK(rig.bar->Active() == 1);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    rig.h.Key(Key::Down);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Key(Key::Enter);  // Copy
    CHECK(rig.last == 0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    rig.h.Advance(0);
    CHECK(rig.bar->Active() == MenuBar::kNone);
}

TEST_CASE("Escape leaves the bar and gives the focus back", "[ui][menu][menubar]")
{
    BarRig rig;
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
    TextField* field = f.Get();
    field->SetBounds(MRect(10, 100, 100, 30));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    rig.h.Advance(0);
    rig.h.GetSurface().SetFocus(field);
    rig.h.Key(Key::F10);
    CHECK(rig.h.GetSurface().Focus() == rig.bar);
    rig.h.Key(Key::Escape);
    CHECK(rig.bar->Active() == MenuBar::kNone);
    CHECK(rig.h.GetSurface().Focus() == field);
}

TEST_CASE("choosing from a menu opened by Alt+letter gives the focus back", "[ui][menu][menubar]")
{
    BarRig rig;
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
    TextField* field = f.Get();
    field->SetBounds(MRect(10, 100, 100, 30));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    rig.h.Advance(0);
    rig.h.GetSurface().SetFocus(field);
    rig.h.Key(Key::E, true, os::kModAlt);
    rig.h.Key(Key::C);
    rig.h.Advance(0);
    CHECK(rig.runs == 1);
    CHECK(rig.h.GetSurface().Focus() == field);
}

TEST_CASE("Alt with a mnemonic letter opens that menu from anywhere", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Key(Key::E, true, os::kModAlt);
    CHECK(rig.bar->Active() == 1);
    CHECK(rig.h.GetSurface().PopupCount() == 1);
    rig.h.Key(Key::C);  // mnemonic of Copy
    CHECK(rig.last == 0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
}

TEST_CASE("a lone Alt toggles the bar", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Key(Key::LeftAlt, true, os::kModAlt);
    CHECK(rig.bar->Active() == 0);
    rig.h.Key(Key::LeftAlt, true, os::kModAlt);
    CHECK(rig.bar->Active() == MenuBar::kNone);
}

TEST_CASE("Alt used with another key is not a lone Alt", "[ui][menu][menubar]")
{
    BarRig rig;
    Event e;
    e.type = EventType::KeyDown;
    e.key = Key::LeftAlt;
    e.modifiers = os::kModAlt;
    rig.h.GetSurface().Dispatch(e);
    e.key = Key::X;  // some other key while Alt is down
    rig.h.GetSurface().Dispatch(e);
    e.type = EventType::KeyUp;
    e.key = Key::LeftAlt;
    rig.h.GetSurface().Dispatch(e);
    CHECK(rig.bar->Active() == MenuBar::kNone);
}

TEST_CASE("a menu closed by an outside click leaves the bar inactive", "[ui][menu][menubar]")
{
    BarRig rig;
    rig.h.Click(10, 10);
    rig.h.Click(400, 200);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().PopupCount() == 0);
    CHECK(rig.bar->Active() == MenuBar::kNone);
}
