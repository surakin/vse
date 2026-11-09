#include "sync/atomic.h"
#include <cmath>
#include <cstdlib>
#include "lang/new.h"

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#if ENGINE_PLATFORM_WINDOWS
#include <malloc.h> // _aligned_malloc, _aligned_free
#endif
#include "font_support.h"
#include "ui/layout.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/testing/recording_canvas.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/scroll.h"
#include "types.h"
#include "cstr/mem_ops.h"
#include "os/console.h"

// --- an allocation counter (this test executable only), to prove that scrolling a long list allocates nothing once warm ---------------
#if !ENGINE_SANITIZED // the sanitizer runtimes define their own operator new
namespace {
Atomic<long> g_ui_allocs{0};
Atomic<bool> g_ui_counting{false};
// Every form is replaced so allocation and release always pair up. `align` is 0 for the plain forms. Windows' C library has no
// aligned_alloc: memory from _aligned_malloc goes back through _aligned_free, so the aligned forms take that path there (as in
// tests/renderer/render_graph_test.cpp).
void* UiCountedAlloc(usize n, usize align) {
    if (g_ui_counting.Load(MemoryOrder::Relaxed)) g_ui_allocs.FetchAdd(1, MemoryOrder::Relaxed);
    if (n == 0) n = 1;
#if ENGINE_PLATFORM_WINDOWS
    if (align != 0) return _aligned_malloc(n, align);
#else
    if (align != 0) return std::aligned_alloc(align, (n + align - 1) / align * align);
#endif
    return MemAllocate(n);
}
void UiAlignedFree(void* p) noexcept {
#if ENGINE_PLATFORM_WINDOWS
    _aligned_free(p);
#else
    MemFree(p);
#endif
}
} // namespace
void* operator new(usize n) {
    if (void* p = UiCountedAlloc(n, 0)) return p;
    os::Abort();
}
void* operator new(usize n, AlignVal a) {
    if (void* p = UiCountedAlloc(n, static_cast<usize>(a))) return p;
    os::Abort();
}
void* operator new[](usize n) { return operator new(n); }
void* operator new[](usize n, AlignVal a) { return operator new(n, a); }
void* operator new(usize n, const NoThrowTag&) noexcept { return UiCountedAlloc(n, 0); }
void* operator new(usize n, AlignVal a, const NoThrowTag&) noexcept { return UiCountedAlloc(n, static_cast<usize>(a)); }
void operator delete(void* p) noexcept { MemFree(p); }
void operator delete[](void* p) noexcept { MemFree(p); }
void operator delete(void* p, usize) noexcept { MemFree(p); }
void operator delete[](void* p, usize) noexcept { MemFree(p); }
void operator delete(void* p, AlignVal) noexcept { UiAlignedFree(p); }
void operator delete(void* p, usize, AlignVal) noexcept { UiAlignedFree(p); }
void operator delete[](void* p, AlignVal) noexcept { UiAlignedFree(p); }
void operator delete[](void* p, usize, AlignVal) noexcept { UiAlignedFree(p); }
void operator delete(void* p, AlignVal, const NoThrowTag&) noexcept { UiAlignedFree(p); }
void operator delete(void* p, const NoThrowTag&) noexcept { MemFree(p); }
#endif

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

Rect ScRect(f32 x, f32 y, f32 w, f32 h) {
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

template <typename T>
T* ScAdd(Widget* parent, UniquePtr<T> w, Rect bounds) {
    T* raw = w.Get();
    raw->SetBounds(bounds);
    parent->AddChild(UniquePtr<Widget>(w.Release()));
    return raw;
}

// A widget with a size of its own that counts the wheel events it sees and does not handle them.
class ScContent : public Widget {
public:
    ScContent(StringView name, f32 w, f32 h) : Widget(name) {
        LayoutProps p;
        p.width = w;
        p.height = h;
        SetProps(p);
    }
    int wheels = 0;
    void OnEvent(Event& e) override {
        if (e.type == EventType::Wheel) ++wheels;
    }
};

class NumberModel : public ListModel {
public:
    explicit NumberModel(usize n) : count(n) {}
    usize count;
    usize ItemCount() const override { return count; }
    StringView ItemText(usize i) override {
        text_.Clear();
        char digits[24];
        usize n = 0;
        usize v = i;
        do {
            digits[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v > 0);
        const char* prefix = "Item ";
        for (usize k = 0; k < 5; ++k) text_.Push(prefix[k]);
        while (n > 0) text_.Push(digits[--n]);
        return StringView(text_.Data(), text_.Size());
    }

private:
    Vector<char> text_;
};

class FruitModel : public ListModel {
public:
    usize ItemCount() const override { return 5; }
    StringView ItemText(usize i) override {
        static const char* const kNames[] = {"Apple", "Banana", "Blueberry", "Cherry", "Date"};
        return StringView(kNames[i]);
    }
};

usize CountTextCalls(Widget* w) {
    RecordingCanvas c;
    w->Draw(c);
    usize n = 0;
    for (usize i = 0; i < c.Calls().Size(); ++i) n += c.Calls()[i].kind == CanvasCallKind::Text;
    return n;
}

} // namespace

TEST_CASE("ScrollBar: thumb geometry follows the view/content ratio", "[ui][scroll]") {
    Harness h(100, 300, 1, StringView(UI_TEST_FONT_FILE));
    ScrollBar* bar = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollBar>(StringView("bar"), true), ScRect(0, 0, 10, 200));
    const f32 min_thumb = h.GetSurface().Themes().Metric(MetricId::MinHitSize, 1.0f);

    bar->SetRange(1000, 200); // a fifth
    CHECK(bar->ThumbRect().h == 40);
    CHECK(bar->ThumbRect().y == 0);
    bar->SetValue(800);
    CHECK(bar->ThumbRect().y + bar->ThumbRect().h == 200); // flush at the end
    bar->SetValue(400);
    CHECK(bar->ThumbRect().y == 80); // halfway through the room

    bar->SetRange(100, 200); // content smaller than the view: the thumb fills the track, the value is 0
    CHECK(bar->ThumbRect().h == 200);
    CHECK(bar->Value() == 0);
    CHECK(bar->MaxValue() == 0);

    bar->SetRange(200, 200);
    CHECK(bar->ThumbRect().h == 200);

    bar->SetRange(100000, 200); // a tiny ratio: the thumb keeps its minimum size
    CHECK(bar->ThumbRect().h == min_thumb);

    bar->SetRange(1000, 200);
    bar->SetValue(5000); // clamped
    CHECK(bar->Value() == 800);
    bar->SetValue(-5);
    CHECK(bar->Value() == 0);
    bar->SetRange(1000, 200);
    bar->SetValue(800);
    bar->SetRange(500, 200); // shrinking the content clamps the value
    CHECK(bar->Value() == 300);
}

TEST_CASE("ScrollBar: drag with capture, track pages, arrows", "[ui][scroll]") {
    Harness h(100, 300, 1, StringView(UI_TEST_FONT_FILE));
    ScrollBar* bar = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollBar>(StringView("bar"), true), ScRect(0, 0, 10, 200));
    bar->SetRange(1000, 200);
    h.Advance(0);
    int moved = 0;
    bar->OnChanged().Connect([](void* u, ScrollBar*) { ++*static_cast<int*>(u); }, &moved);

    // Press on the thumb at y = 10 (grab offset 10), drag 80 down the screen: the room is 160 for 800 units, so 5 units per unit.
    h.PointerMove(5, 10);
    h.PointerDown(5, 10);
    h.PointerMove(5, 90);
    CHECK(bar->Value() == 400);
    h.PointerMove(300, 5000); // the pointer left the bar: the drag goes on (captured)
    CHECK(bar->Value() == 800);
    h.PointerMove(5, -400);
    CHECK(bar->Value() == 0);
    h.PointerUp(5, -400);
    h.PointerMove(5, 90); // released: no more drag
    CHECK(bar->Value() == 0);

    // The track pages by a view toward the pointer.
    h.Click(5, 150);
    CHECK(bar->Value() == 200);
    h.Click(5, 190);
    CHECK(bar->Value() == 400);
    h.Click(5, 2);
    CHECK(bar->Value() == 200);
    const int before = moved;
    bar->SetValue(300); // programmatic changes run no listener
    CHECK(moved == before);

    SECTION("arrows move by the line step") {
        bar->SetArrows(true);
        bar->SetLineStep(25);
        h.Advance(0);
        bar->SetValue(100);
        h.Click(5, 3); // up arrow
        CHECK(bar->Value() == 75);
        h.Click(5, 197); // down arrow
        h.Click(5, 197);
        CHECK(bar->Value() == 125);
    }
}

TEST_CASE("ScrollBar: the auto-hide variant is drawn only when needed", "[ui][scroll]") {
    Harness h(100, 300, 1, StringView(UI_TEST_FONT_FILE));
    ScrollBar* bar = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollBar>(StringView("bar"), true), ScRect(0, 0, 10, 200));
    bar->SetAutoHide(true);
    auto calls = [&]() {
        RecordingCanvas c;
        bar->Draw(c);
        return c.Calls().Size();
    };
    bar->SetRange(100, 200);
    CHECK(calls() == 0); // nothing to scroll
    bar->SetRange(1000, 200);
    h.GetSurface().SetTime(100);
    CHECK(calls() == 0); // idle
    CHECK(!bar->WantsUpdate());
    h.PointerMove(5, 50);
    h.Advance(0);
    CHECK(calls() > 0); // hovered
    h.PointerMove(50, 250);
    h.Advance(0);
    CHECK(calls() == 0);
    h.PointerMove(5, 10);
    h.PointerDown(5, 10);
    h.PointerMove(5, 20); // a drag changes the value
    h.PointerUp(5, 20);
    h.PointerMove(50, 250);
    h.Advance(0);
    CHECK(calls() > 0); // just changed
    CHECK(bar->WantsUpdate());
    h.GetSurface().SetTime(h.GetSurface().Time() + 2);
    CHECK(calls() == 0); // faded
}

TEST_CASE("ScrollView: bars by policy, clamping, wheel, bubbling and ScrollTo", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    ScrollView* view = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollView>(StringView("view")), ScRect(0, 0, 100, 100));
    ScContent* content = static_cast<ScContent*>(view->SetContent(MakeUnique<ScContent>(StringView("content"), 80.0f, 500.0f)));
    h.Advance(0);
    const f32 bar_w = h.GetSurface().Themes().Metric(MetricId::ScrollbarThickness, 1.0f);

    CHECK(!view->VerticalBar()->Collapsed());
    CHECK(view->HorizontalBar()->Collapsed()); // 80 wide fits in the 90 left of the bar
    CHECK(view->Viewport().w == 100 - bar_w);
    CHECK(view->ContentSize().h == 500);
    CHECK(content->Bounds().y == 0);

    // The wheel scrolls by the line step per notch and is clamped.
    const f32 step = h.GetSurface().Themes().Metric(MetricId::MinHitSize, 1.0f);
    h.Wheel(20, 20, 0, 1);
    h.Advance(0);
    CHECK(view->Offset().y == step);
    CHECK(content->Bounds().y == -step);
    CHECK(content->wheels == 1); // the inner widget saw it first and let it bubble
    h.Wheel(20, 20, 0, 100);
    h.Advance(0);
    CHECK(view->Offset().y == 400);
    CHECK(content->Bounds().y == -400);
    h.Wheel(20, 20, 0, -1000);
    h.Advance(0);
    CHECK(view->Offset().y == 0);
    CHECK(view->VerticalBar()->Value() == 0);

    // Dragging the bar scrolls the content.
    const Rect thumb = view->VerticalBar()->ThumbRect();
    h.PointerMove(thumb.x + 2, thumb.y + 2);
    h.PointerDown(thumb.x + 2, thumb.y + 2);
    h.PointerMove(thumb.x + 2, thumb.y + 22);
    h.Advance(0);
    CHECK(view->Offset().y > 50);
    h.PointerUp(thumb.x + 2, thumb.y + 22);

    SECTION("ScrollTo brings a rectangle into view with the least movement") {
        view->SetOffset(Point());
        h.Advance(0);
        CHECK(view->ScrollTo(ScRect(0, 300, 20, 20)));
        h.Advance(0);
        CHECK(view->Offset().y == 220); // the rectangle's bottom is at the view's bottom
        // The rectangle is in the view's own coordinates: the 300 of the content is now at 80.
        CHECK(!view->ScrollTo(ScRect(0, 80, 20, 20)));
        CHECK(view->ScrollTo(ScRect(0, -220, 20, 20)));
        h.Advance(0);
        CHECK(view->Offset().y == 0);
        // Larger than the view: its start is shown.
        CHECK(view->ScrollTo(ScRect(0, 200, 20, 300)));
        h.Advance(0);
        CHECK(view->Offset().y == 200);
    }
    SECTION("the policies") {
        view->SetPolicy(ScrollPolicy::Never, ScrollPolicy::Never);
        h.Advance(0);
        CHECK(view->VerticalBar()->Collapsed());
        CHECK(view->ContentSize().w == 100);
        CHECK(view->ContentSize().h == 100); // the content is given the view's size
        view->SetPolicy(ScrollPolicy::Always, ScrollPolicy::Always);
        h.Advance(0);
        CHECK(!view->HorizontalBar()->Collapsed());
        CHECK(!view->VerticalBar()->Collapsed());
        CHECK(view->Viewport().h == 100 - bar_w);
        view->SetPolicy(ScrollPolicy::Auto, ScrollPolicy::Auto);
        view->SetContent(MakeUnique<ScContent>(StringView("wide"), 400.0f, 50.0f));
        h.Advance(0);
        CHECK(view->VerticalBar()->Collapsed());
        CHECK(!view->HorizontalBar()->Collapsed());
        CHECK(view->ContentSize().w == 400);
        CHECK(view->ContentSize().h == 100 - bar_w); // a small content fills the view
    }
    SECTION("a small content fills the view and does not scroll") {
        view->SetContent(MakeUnique<ScContent>(StringView("small"), 10.0f, 10.0f));
        h.Advance(0);
        CHECK(view->VerticalBar()->Collapsed());
        CHECK(view->ContentSize().w == 100);
        h.Wheel(20, 20, 0, 1);
        CHECK(view->Offset().y == 0);
    }
}

TEST_CASE("ScrollView: an inner view that is at its end lets the wheel go to the outer one; focus scrolls into view", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    ScrollView* outer = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollView>(StringView("outer")), ScRect(0, 0, 150, 150));
    Widget* column = outer->SetContent(MakeUnique<ScContent>(StringView("column"), 100.0f, 600.0f));
    ScrollView* inner = static_cast<ScrollView*>(column->AddChild(MakeUnique<ScrollView>(StringView("inner"))));
    inner->SetBounds(ScRect(0, 0, 100, 100));
    LayoutProps fixed;
    fixed.width = 100;
    fixed.height = 100;
    inner->SetProps(fixed);
    inner->SetContent(MakeUnique<ScContent>(StringView("innerContent"), 80.0f, 300.0f));
    h.Advance(0);

    h.Wheel(20, 20, 0, 1);
    h.Advance(0);
    CHECK(inner->Offset().y > 0);
    CHECK(outer->Offset().y == 0); // the inner one took it
    h.Wheel(20, 20, 0, 100);
    h.Advance(0);
    const f32 inner_end = inner->Offset().y;
    CHECK(inner_end > 0);
    h.Wheel(20, 20, 0, 1); // at the end of the inner content: the outer view scrolls
    h.Advance(0);
    CHECK(inner->Offset().y == inner_end);
    CHECK(outer->Offset().y > 0);

    // Focusing a widget far down the content scrolls it into view.
    outer->SetOffset(Point());
    Widget* field = column->AddChild(MakeUnique<ScContent>(StringView("field"), 50.0f, 20.0f));
    LayoutProps at;
    at.width = 50;
    at.height = 20;
    field->SetProps(at);
    field->SetFocusable(true);
    h.Advance(0);
    field->SetBounds(ScRect(0, 520, 50, 20));
    outer->Arrange(outer->Bounds());
    field->SetBounds(ScRect(0, 520 - outer->Offset().y, 50, 20));
    h.GetSurface().SetFocus(field);
    h.Advance(0);
    CHECK(outer->Offset().y > 0);
}

TEST_CASE("ScrollView: focus scrolls nested views, the inner one first, and the outer one sees where it put the widget", "[ui][scroll]") {
    Harness h(300, 400, 1, StringView(UI_TEST_FONT_FILE));
    ScrollView* outer = ScAdd(h.GetSurface().Root(), MakeUnique<ScrollView>(StringView("outer")), ScRect(0, 0, 150, 150));
    BoxLayout* column = static_cast<BoxLayout*>(outer->SetContent(MakeUnique<BoxLayout>(Orientation::Vertical, StringView("column"))));
    column->AddChild(MakeUnique<ScContent>(StringView("above"), 100.0f, 300.0f));
    ScrollView* inner = static_cast<ScrollView*>(column->AddChild(MakeUnique<ScrollView>(StringView("inner"))));
    LayoutProps fixed;
    fixed.width = 100;
    fixed.height = 100;
    inner->SetProps(fixed);
    BoxLayout* list = static_cast<BoxLayout*>(inner->SetContent(MakeUnique<BoxLayout>(Orientation::Vertical, StringView("list"))));
    list->AddChild(MakeUnique<ScContent>(StringView("top"), 50.0f, 250.0f));
    Widget* target = list->AddChild(MakeUnique<ScContent>(StringView("target"), 50.0f, 20.0f));
    target->SetFocusable(true);
    column->AddChild(MakeUnique<ScContent>(StringView("below"), 100.0f, 300.0f));
    h.Advance(0);
    h.GetSurface().SetFocus(target);
    h.Advance(0);
    const Rect t = target->Bounds();
    CHECK(inner->Offset().y > 0);
    CHECK(outer->Offset().y > 0);
    CHECK(t.y >= inner->Viewport().y);
    CHECK(t.Bottom() <= inner->Viewport().Bottom());
    CHECK(t.y >= outer->Viewport().y);
    CHECK(t.Bottom() <= outer->Viewport().Bottom());
}

TEST_CASE("ListBox: selection rules, keyboard and type-ahead", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    FruitModel fruit;
    ListBox* list = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("list")), ScRect(0, 0, 150, 200));
    list->SetModel(&fruit);
    h.Advance(0);
    const f32 row = list->RowHeight();
    REQUIRE(row > 0);
    int changes = 0;
    list->OnSelectionChanged().Connect([](void* u, ListBox*) { ++*static_cast<int*>(u); }, &changes);
    auto click_row = [&](usize i, u32 modifiers = 0) {
        h.PointerMove(20, row * static_cast<f32>(i) + row / 2);
        h.PointerDown(20, row * static_cast<f32>(i) + row / 2);
        // The harness sends no modifiers with a click: post the press with them.
        (void)modifiers;
        h.PointerUp(20, row * static_cast<f32>(i) + row / 2);
    };

    click_row(1);
    CHECK(list->IsSelected(1));
    CHECK(list->SelectedCount() == 1);
    CHECK(list->Current() == 1);
    CHECK(changes == 1);
    click_row(3);
    CHECK(!list->IsSelected(1));
    CHECK(list->IsSelected(3));
    CHECK(changes == 2);
    click_row(3); // the same item again: no change
    CHECK(changes == 2);

    // Keyboard: arrows move and select; Home/End/Page.
    h.Key(Key::Up);
    CHECK(list->Current() == 2);
    CHECK(list->IsSelected(2));
    CHECK(list->SelectedCount() == 1);
    h.Key(Key::Home);
    CHECK(list->Current() == 0);
    h.Key(Key::Up);
    CHECK(list->Current() == 0);
    h.Key(Key::End);
    CHECK(list->Current() == 4);
    h.Key(Key::Down);
    CHECK(list->Current() == 4);

    SECTION("type-ahead") {
        h.GetSurface().SetTime(10);
        h.TypeText("b");
        CHECK(list->Current() == 1); // Banana, the first starting with b after Date wrapping around
        h.TypeText("l"); // "bl" within a second: Blueberry
        CHECK(list->Current() == 2);
        h.GetSurface().SetTime(20); // a pause: a new prefix
        h.TypeText("b");
        CHECK(list->Current() == 1);
        h.TypeText("b"); // the same letter again cycles
        CHECK(list->Current() == 2);
        h.TypeText("b");
        CHECK(list->Current() == 1);
        h.GetSurface().SetTime(30);
        h.TypeText("z"); // nothing starts with z
        CHECK(list->Current() == 1);
        h.GetSurface().SetTime(40);
        h.TypeText("D");
        CHECK(list->Current() == 4); // case-insensitive
    }
    SECTION("multiple selection") {
        list->SetMode(SelectionMode::Multiple);
        list->Select(1);
        // Shift+arrows select a range from the anchor.
        h.Key(Key::Down, true, os::kModShift);
        h.Key(Key::Down, true, os::kModShift);
        CHECK(list->SelectedCount() == 3);
        CHECK(list->IsSelected(1));
        CHECK(list->IsSelected(3));
        h.Key(Key::Up, true, os::kModShift);
        CHECK(list->SelectedCount() == 2);
        h.Key(Key::A, true, os::kModCtrl);
        CHECK(list->SelectedCount() == 5);
        h.Key(Key::Down); // a plain arrow collapses to one
        CHECK(list->SelectedCount() == 1);
        list->SetMode(SelectionMode::Single);
        CHECK(list->SelectedCount() == 1);
    }
    SECTION("Enter activates the current item") {
        int activated = 0;
        list->OnActivate().Connect([](void* u, ListBox*) { ++*static_cast<int*>(u); }, &activated);
        h.Key(Key::Enter);
        CHECK(activated == 1);
        list->Select(ListBox::kNone);
        CHECK(list->SelectedCount() == 0);
        h.Key(Key::Enter);
        CHECK(activated == 1);
    }
}

TEST_CASE("ListBox: Ctrl and Shift clicks in multiple mode", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    NumberModel model(20);
    ListBox* list = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("list")), ScRect(0, 0, 150, 300));
    list->SetModel(&model);
    list->SetMode(SelectionMode::Multiple);
    h.Advance(0);
    const f32 row = list->RowHeight();
    auto click = [&](usize i, u32 modifiers) {
        Event e;
        e.position = Point(20, row * static_cast<f32>(i) + row / 2);
        e.modifiers = modifiers;
        e.type = EventType::PointerMove;
        h.GetSurface().Dispatch(e);
        e.type = EventType::PointerDown;
        e.button = os::kMouseLeft;
        h.GetSurface().Dispatch(e);
        e.type = EventType::PointerUp;
        h.GetSurface().Dispatch(e);
    };
    click(2, 0);
    click(5, os::kModCtrl);
    click(7, os::kModCtrl);
    CHECK(list->SelectedCount() == 3);
    click(5, os::kModCtrl); // toggles off
    CHECK(list->SelectedCount() == 2);
    click(7, 0); // a plain click selects only it
    CHECK(list->SelectedCount() == 1);
    click(10, os::kModShift); // a range from the anchor (7)
    CHECK(list->SelectedCount() == 4);
    CHECK(list->IsSelected(7));
    CHECK(list->IsSelected(10));
    CHECK(!list->IsSelected(6));
    click(4, os::kModShift);
    CHECK(list->SelectedCount() == 4); // 4..7
    CHECK(list->IsSelected(4));
    CHECK(!list->IsSelected(10));
}

TEST_CASE("ListBox: only the rows in view are laid out and drawn, however many items there are", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    NumberModel small(100);
    NumberModel huge(100000);
    ListBox* a = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("a")), ScRect(0, 0, 150, 200));
    ListBox* b = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("b")), ScRect(150, 0, 150, 200));
    a->SetModel(&small);
    b->SetModel(&huge);
    h.Advance(0);
    const usize drawn_small = CountTextCalls(a);
    const usize drawn_huge = CountTextCalls(b);
    CHECK(drawn_small == drawn_huge);
    CHECK(drawn_huge > 3);
    CHECK(drawn_huge < 30);
    CHECK(b->RowLayouts() < 30);

    // Scrolling by one row lays out one new row.
    const u64 before = b->RowLayouts();
    h.Wheel(200, 50, 0, 1 / ListBox::kWheelRows);
    h.Advance(0);
    (void)CountTextCalls(b);
    CHECK(b->RowLayouts() - before <= 2);

    // The end of a huge list: the key goes there at once and the rows in view are the last ones.
    h.GetSurface().SetFocus(b);
    h.Key(Key::End);
    h.Advance(0);
    usize first = 0;
    usize end = 0;
    b->VisibleRange(&first, &end);
    CHECK(end == 100000);
    CHECK(end - first < 30);
    CHECK(b->Current() == 99999);
    CHECK(b->IsSelected(99999));
    // ScrollToIndex puts an item in view; Select scrolls to it.
    b->Select(50000);
    b->VisibleRange(&first, &end);
    CHECK(first <= 50000);
    CHECK(end > 50000);
    h.Key(Key::PageDown);
    CHECK(b->Current() > 50000 + 3);
    b->VisibleRange(&first, &end);
    CHECK(b->Current() >= first);
    CHECK(b->Current() < end);
}

TEST_CASE("ListBox: the model changing clears the selection and clamps the scroll", "[ui][scroll]") {
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    NumberModel model(1000);
    ListBox* list = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("list")), ScRect(0, 0, 150, 200));
    list->SetModel(&model);
    h.Advance(0);
    list->Select(900);
    CHECK(list->Offset() > 0);
    model.count = 3;
    list->ModelChanged();
    h.Advance(0);
    CHECK(list->SelectedCount() == 0);
    list->Select(1);
    list->ModelChanged(); // as many items, but not the same ones: the selection goes
    CHECK(list->SelectedCount() == 0);
    h.Advance(0);
    CHECK(list->SelectedCount() == 0);
    CHECK(list->Offset() == 0);
    CHECK(list->Current() == ListBox::kNone);
}

TEST_CASE("ListBox: frames that scroll within the laid-out rows allocate nothing once warm", "[ui][scroll]") {
#if ENGINE_SANITIZED
    SKIP("allocation counting needs the plain flavor (the sanitizer runtimes own operator new)");
#else
    Harness h(300, 300, 1, StringView(UI_TEST_FONT_FILE));
    NumberModel model(100000);
    ListBox* list = ScAdd(h.GetSurface().Root(), MakeUnique<ListBox>(StringView("list")), ScRect(0, 0, 200, 150));
    list->SetModel(&model);
    h.Advance(0);
    const f32 row = list->RowHeight();
    list->SetOffset(row * 1000);
    // Warm up: the same sweep once, so that the caches, the glyph atlas and every vector have their size. The sweep stays inside
    // one row, so no new row comes into view (laying out a new text allocates inside the text module; the cache is what makes it rare).
    for (int i = 0; i < 100; ++i) {
        list->SetOffset(row * 1000 + row * static_cast<f32>(i % 10) / 20.0f);
        h.Advance(0.016f);
    }
    const u64 layouts = list->RowLayouts();
    g_ui_allocs.Store(0);
    g_ui_counting.Store(true);
    for (int i = 0; i < 100; ++i) {
        list->SetOffset(row * 1000 + row * static_cast<f32>(i % 10) / 20.0f);
        h.Advance(0.016f);
    }
    g_ui_counting.Store(false);
    CHECK(g_ui_allocs.Load() == 0);
    CHECK(list->RowLayouts() == layouts);
    CHECK(list->Offset() >= row * 1000);
#endif
}
