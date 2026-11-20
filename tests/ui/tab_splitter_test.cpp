#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/splitter.h"
#include "ui/widgets/tab_view.h"
#include "ui/widgets/text_field.h"

using namespace vse;
using namespace vse::ui;

namespace
{

Rect TsRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

Point TsCenter(const Rect& r)
{
    return Point{r.x + r.w / 2, r.y + r.h / 2};
}

// ---- tabs ----

struct TabRig
{
    explicit TabRig(usize count = 3, f32 width = 400) : h(500, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<TabView> t = MakeUnique<TabView>(StringView("tabs"));
        tabs = t.Get();
        tabs->OnSelectionChanged().Connect(&TabRig::Changed, this);
        tabs->OnCloseRequested().Connect(&TabRig::CloseAsked, this);
        tabs->SetClosable(closable);
        for (usize i = 0; i < count; ++i)
        {
            AddOne(i);
        }
        tabs->SetBounds(TsRect(10, 10, width, 200));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(t.Release()));
        h.Advance(0);
    }
    void AddOne(usize i)
    {
        StringBuilder<HeapAllocator> title(HeapAllocator::Default());
        title.Append(StringView("Tab number "));
        title.AppendUInt(i);
        UniquePtr<TextField> page = MakeUnique<TextField>(StringView("page"));
        pages[i < 8 ? i : 7] = page.Get();
        tabs->AddTab(title.View(), UniquePtr<Widget>(page.Release()));
    }
    static void Changed(void* user, TabView*) { ++static_cast<TabRig*>(user)->changes; }
    static void CloseAsked(void* user, TabView*, usize index)
    {
        TabRig* r = static_cast<TabRig*>(user);
        r->close_asked = static_cast<int>(index);
    }
    Harness h;
    TabView* tabs = nullptr;
    TextField* pages[8] = {};
    bool closable = false;
    int changes = 0;
    int close_asked = -1;
};

UniquePtr<Widget> MakeLazyPage(void* user)
{
    ++*static_cast<int*>(user);
    return UniquePtr<Widget>(MakeUnique<TextField>(StringView("lazy")).Release());
}

}  // namespace

TEST_CASE("a click on a header selects its tab and shows its page", "[ui][tabs]")
{
    TabRig rig;
    CHECK(rig.tabs->Selected() == 0);
    CHECK_FALSE(rig.pages[0]->Collapsed());
    CHECK(rig.pages[1]->Collapsed());
    const Point p = TsCenter(rig.tabs->TabRect(1));
    rig.h.Click(p.x - 10, p.y);
    CHECK(rig.tabs->Selected() == 1);
    CHECK(rig.changes == 1);
    CHECK(rig.pages[0]->Collapsed());
    CHECK_FALSE(rig.pages[1]->Collapsed());
    const Rect content = rig.tabs->ContentRect();
    CHECK(rig.pages[1]->Bounds().y >= content.y - 0.5f);
    rig.h.Click(p.x - 10, p.y);  // the same tab again: no change
    CHECK(rig.changes == 1);
}

TEST_CASE("Select runs no listener and ignores a bad index", "[ui][tabs]")
{
    TabRig rig;
    rig.tabs->Select(2);
    CHECK(rig.tabs->Selected() == 2);
    CHECK(rig.changes == 0);
    rig.tabs->Select(9);
    CHECK(rig.tabs->Selected() == 2);
}

TEST_CASE("arrow keys on the focused view move the selection", "[ui][tabs]")
{
    TabRig rig;
    rig.h.GetSurface().SetFocus(rig.tabs);
    rig.h.Key(Key::Right);
    CHECK(rig.tabs->Selected() == 1);
    rig.h.Key(Key::Right);
    rig.h.Key(Key::Right);  // at the end: stays
    CHECK(rig.tabs->Selected() == 2);
    rig.h.Key(Key::Left);
    CHECK(rig.tabs->Selected() == 1);
    rig.h.Key(Key::Home);
    CHECK(rig.tabs->Selected() == 0);
    rig.h.Key(Key::End);
    CHECK(rig.tabs->Selected() == 2);
}

TEST_CASE("Ctrl+Tab cycles the tabs, also from inside a page", "[ui][tabs]")
{
    TabRig rig;
    rig.h.GetSurface().SetFocus(rig.pages[0]);
    rig.h.Key(Key::Tab, true, os::kModCtrl);
    CHECK(rig.tabs->Selected() == 1);
    rig.h.Key(Key::Tab, true, os::kModCtrl);
    rig.h.Key(Key::Tab, true, os::kModCtrl);
    CHECK(rig.tabs->Selected() == 0);  // wrapped
    rig.h.Key(Key::Tab, true, os::kModCtrl | os::kModShift);
    CHECK(rig.tabs->Selected() == 2);
    rig.h.Key(Key::PageUp, true, os::kModCtrl);
    CHECK(rig.tabs->Selected() == 1);
}

TEST_CASE("arrow keys typed in a page do not switch tabs", "[ui][tabs]")
{
    TabRig rig;
    rig.h.GetSurface().SetFocus(rig.pages[0]);
    rig.h.Key(Key::Right);
    CHECK(rig.tabs->Selected() == 0);
}

TEST_CASE("a lazy page is built once, on first selection", "[ui][tabs]")
{
    TabRig rig(1);
    int built = 0;
    rig.tabs->AddLazyTab(StringView("Lazy"), &MakeLazyPage, &built);
    CHECK(built == 0);
    CHECK(rig.tabs->Page(1) == nullptr);
    rig.tabs->Select(1);
    CHECK(built == 1);
    REQUIRE(rig.tabs->Page(1) != nullptr);
    Widget* page = rig.tabs->Page(1);
    rig.tabs->Select(0);
    rig.tabs->Select(1);
    CHECK(built == 1);
    CHECK(rig.tabs->Page(1) == page);
}

TEST_CASE("a hidden page keeps its state", "[ui][tabs]")
{
    TabRig rig;
    rig.pages[0]->SetText(StringView("kept"));
    rig.tabs->Select(2);
    rig.tabs->Select(0);
    CHECK(rig.tabs->Page(0) == rig.pages[0]);
    CHECK(rig.pages[0]->Text() == StringView("kept"));
}

TEST_CASE("a close button asks, and CloseTab removes the page and moves the selection", "[ui][tabs]")
{
    TabRig rig(0);
    rig.closable = true;
    rig.tabs->SetClosable(true);
    for (usize i = 0; i < 3; ++i)
    {
        rig.AddOne(i);
    }
    rig.h.Advance(0);
    rig.tabs->Select(1);
    const Rect r = rig.tabs->TabRect(1);
    rig.h.Click(r.Right() - 14, r.y + r.h / 2);
    CHECK(rig.close_asked == 1);
    CHECK(rig.tabs->TabCount() == 3);  // the program decides
    rig.tabs->CloseTab(1);
    rig.h.Advance(0);
    CHECK(rig.tabs->TabCount() == 2);
    CHECK(rig.tabs->Selected() == 1);  // the next tab moved up
    CHECK_FALSE(rig.tabs->Page(1)->Collapsed());
    rig.tabs->CloseTab(0);
    CHECK(rig.tabs->Selected() == 0);
    rig.tabs->CloseTab(0);
    CHECK(rig.tabs->Selected() == TabView::kNone);
    CHECK(rig.tabs->TabCount() == 0);
}

TEST_CASE("headers that do not fit scroll, and a selected tab scrolls into view", "[ui][tabs]")
{
    TabRig rig(10, 300);
    TabView* t = rig.tabs;
    REQUIRE(t->HeaderOverflows());
    CHECK(t->HeaderScroll() == 0);
    // The right arrow is at the right end of the header.
    const Rect b = t->Bounds();
    rig.h.Click(b.Right() - 5, b.y + 5);
    CHECK(t->HeaderScroll() > 0);
    const f32 after_arrow = t->HeaderScroll();
    rig.h.Wheel(b.x + 20, b.y + 5, 0, 1);
    CHECK(t->HeaderScroll() > after_arrow);
    t->Select(9);
    const Rect last = t->TabRect(9);
    CHECK(last.Right() <= b.Right() + 0.5f);
    t->Select(0);
    CHECK(t->HeaderScroll() == 0);
    CHECK(t->TabRect(0).x >= b.x - 0.5f);
}

TEST_CASE("headers that fit show no arrows", "[ui][tabs]")
{
    TabRig rig(2, 400);
    CHECK_FALSE(rig.tabs->HeaderOverflows());
}

// ---- splitter ----

namespace
{

struct SplitRig
{
    explicit SplitRig(Orientation o = Orientation::Horizontal, usize panes = 2)
        : h(500, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<Splitter> s = MakeUnique<Splitter>(o, StringView("split"));
        split = s.Get();
        split->OnChanged().Connect(&SplitRig::Changed, this);
        for (usize i = 0; i < panes; ++i)
        {
            split->AddPane(MakeUnique<Widget>(StringView("pane")), 1.0f);
        }
        LayoutProps props;
        props.fill_parent = true;
        split->SetProps(props);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(s.Release()));
        h.Advance(0);
    }
    static void Changed(void* user, Splitter*) { ++static_cast<SplitRig*>(user)->changes; }
    Point Handle(usize i = 0) const { return TsCenter(split->HandleRect(i)); }
    void Drag(f32 dx, f32 dy, usize handle = 0)
    {
        const Point p = Handle(handle);
        h.PointerDown(p.x, p.y);
        h.PointerMove(p.x + dx / 2, p.y + dy / 2);
        h.PointerMove(p.x + dx, p.y + dy);
        h.PointerUp(p.x + dx, p.y + dy);
        h.Advance(0);
    }
    Harness h;
    Splitter* split = nullptr;
    int changes = 0;
};

}  // namespace

TEST_CASE("panes share the room less the handles", "[ui][splitter]")
{
    SplitRig rig;
    CHECK(rig.split->PaneSize(0) + rig.split->PaneSize(1) + Splitter::kHandle == 500);
    CHECK(rig.split->PaneSize(0) == rig.split->PaneSize(1));
    CHECK(rig.split->Pane(1)->Bounds().x == rig.split->PaneSize(0) + Splitter::kHandle);
}

TEST_CASE("dragging a handle moves the boundary", "[ui][splitter]")
{
    SplitRig rig;
    const f32 before = rig.split->PaneSize(0);
    rig.Drag(60, 0);
    CHECK(rig.split->PaneSize(0) == before + 60);
    CHECK(rig.split->PaneSize(0) + rig.split->PaneSize(1) + Splitter::kHandle == 500);
    CHECK(rig.changes > 0);
    CHECK(rig.split->Pane(0)->Bounds().w == rig.split->PaneSize(0));
}

TEST_CASE("a drag stops at the minimum size of a pane", "[ui][splitter]")
{
    SplitRig rig;
    rig.split->SetMinSize(0, 120);
    rig.split->SetMinSize(1, 100);
    rig.h.Advance(0);
    rig.Drag(-400, 0);
    CHECK(rig.split->PaneSize(0) == 120);
    rig.h.Advance(1.0f);
    rig.Drag(900, 0);
    CHECK(rig.split->PaneSize(1) == 100);
}

TEST_CASE("the panes keep their proportions when the splitter resizes", "[ui][splitter]")
{
    SplitRig rig;
    rig.Drag(100, 0);
    const f32 ratio = rig.split->PaneSize(0) / rig.split->PaneSize(1);
    rig.h.GetSurface().Resize(1000, 300, 1);
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(0) + rig.split->PaneSize(1) + Splitter::kHandle == 1000);
    CHECK(rig.split->PaneSize(0) / rig.split->PaneSize(1) == Catch::Approx(ratio).epsilon(0.001));
}

TEST_CASE("a double click on a handle restores the starting sizes", "[ui][splitter]")
{
    SplitRig rig;
    const f32 start = rig.split->PaneSize(0);
    rig.Drag(80, 0);
    REQUIRE(rig.split->PaneSize(0) != start);
    rig.h.Advance(1.0f);
    const Point p = rig.Handle();
    rig.h.Click(p.x, p.y);
    rig.h.Click(p.x, p.y);
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(0) == start);
}

TEST_CASE("a collapsible pane collapses past half its minimum and opens again", "[ui][splitter]")
{
    SplitRig rig;
    rig.split->SetMinSize(0, 100);
    rig.split->SetCollapsible(0, true);
    rig.h.Advance(0);
    rig.Drag(-240, 0);
    CHECK(rig.split->PaneCollapsed(0));
    CHECK(rig.split->PaneSize(0) == 0);
    CHECK(rig.split->PaneSize(1) == 500 - Splitter::kHandle);
    CHECK(rig.split->Pane(0)->Collapsed());
    rig.h.Advance(1.0f);
    rig.Drag(200, 0);
    CHECK_FALSE(rig.split->PaneCollapsed(0));
    CHECK(rig.split->PaneSize(0) >= 100);
}

TEST_CASE("panes collapse and open from code", "[ui][splitter]")
{
    SplitRig rig(Orientation::Horizontal, 3);
    rig.split->SetPaneCollapsed(1, true);
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(1) == 0);
    CHECK(rig.split->PaneSize(0) == rig.split->PaneSize(2));
    rig.split->SetPaneCollapsed(1, false);
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(1) > 0);
    CHECK(rig.changes == 0);  // code changes run no listener
}

TEST_CASE("the fractions are plain numbers that round-trip", "[ui][splitter]")
{
    SplitRig rig(Orientation::Horizontal, 3);
    Vector<f32> f;
    rig.split->Fractions(&f);
    REQUIRE(f.Size() == 3);
    CHECK(f[0] + f[1] + f[2] == Catch::Approx(1.0));
    const f32 set[3] = {1.0f, 3.0f, 1.0f};
    rig.split->SetFractions(Span<const f32>(set, 3));
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(1) == Catch::Approx(rig.split->PaneSize(0) * 3).epsilon(0.001));
    const f32 bad[2] = {1.0f, 1.0f};
    rig.split->SetFractions(Span<const f32>(bad, 2));  // wrong count: ignored
    rig.h.Advance(0);
    CHECK(rig.split->PaneSize(1) == Catch::Approx(rig.split->PaneSize(0) * 3).epsilon(0.001));
}

TEST_CASE("a vertical splitter drags along y and shows the matching cursor", "[ui][splitter]")
{
    SplitRig rig(Orientation::Vertical);
    const Point p = rig.Handle();
    rig.h.PointerMove(p.x, p.y);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeVertical);
    rig.h.PointerMove(p.x, 20);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::Arrow);
    const f32 before = rig.split->PaneSize(0);
    rig.Drag(0, 40);
    CHECK(rig.split->PaneSize(0) == before + 40);
}

TEST_CASE("a horizontal splitter shows the horizontal resize cursor over its handle", "[ui][splitter]")
{
    SplitRig rig;
    const Point p = rig.Handle();
    rig.h.PointerMove(p.x, p.y);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeHorizontal);
}
