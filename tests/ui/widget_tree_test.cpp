#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widget.h"
#include "ui/widgets/display.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

// A widget with bounds that records what it is asked to do.
class Probe : public Widget {
public:
    explicit Probe(StringView name, Rect bounds = Rect()) : Widget(name) { SetBounds(bounds); }
    bool remove_self_on_down = false;
    usize children_seen_in_handler = 0;
    u32 downs = 0;
    void OnEvent(Event& e) override {
        if (e.type == EventType::PointerDown) {
            ++downs;
            if (remove_self_on_down && Parent() != nullptr) {
                Widget* parent = Parent();
                RemoveFromParent();
                children_seen_in_handler = parent->ChildCount();
            }
            e.handled = true;
        }
    }
};

Probe* Add(Widget* parent, StringView name, Rect bounds) {
    UniquePtr<Probe> p = MakeUnique<Probe>(name, bounds);
    Probe* raw = p.Get();
    parent->AddChild(UniquePtr<Widget>(p.Release()));
    return raw;
}

void BuildTree(Surface* s, u64* ids) {
    Probe* a = Add(s->Root(), "a", {0, 0, 50, 50});
    Probe* b = Add(a, "b", {0, 0, 10, 10});
    Probe* unnamed1 = Add(a, "", {0, 0, 5, 5});
    Probe* unnamed2 = Add(a, "", {0, 0, 5, 5});
    ids[0] = a->Id();
    ids[1] = b->Id();
    ids[2] = unnamed1->Id();
    ids[3] = unnamed2->Id();
}

} // namespace

TEST_CASE("Widget tree: ids are stable across rebuilds and distinct across siblings", "[ui][tree]") {
    Surface s1(100, 100, 1), s2(100, 100, 1);
    u64 ids1[4], ids2[4];
    BuildTree(&s1, ids1);
    BuildTree(&s2, ids2);
    for (int i = 0; i < 4; ++i) CHECK(ids1[i] == ids2[i]);
    CHECK(ids1[2] != ids1[3]); // empty names: told apart by index
    CHECK(ids1[0] != ids1[1]);
    CHECK(ids1[0] != 0);
    // The same name under another parent is another id.
    Probe* other = Add(s1.Root(), "other", {0, 0, 10, 10});
    Probe* b2 = Add(other, "b", {0, 0, 1, 1});
    CHECK(b2->Id() != ids1[1]);
    CHECK(s1.Root()->FindById(ids1[1]) == s1.Root()->FindByName("b"));
    CHECK(s1.Root()->FindById(0xDEADBEEFull) == nullptr);
    CHECK(s1.Root()->FindByName("nope") == nullptr);
    CHECK(s1.Root()->FindByName("b")->Name() == StringView("b"));
}

TEST_CASE("Widget tree: dirty flags reach the root and the surface, and a frame clears them", "[ui][tree]") {
    Surface s(100, 100, 1);
    DrawList list; // a frame with a list clears the draw flags too
    Probe* a = Add(s.Root(), "a", {0, 0, 50, 50});
    Probe* b = Add(a, "b", {0, 0, 10, 10});
    s.Frame(0, &list);
    CHECK(s.Root()->Dirty() == 0);
    CHECK(a->Dirty() == 0);
    const u64 passes = s.LayoutPasses();
    b->MarkDirty(kDrawDirty);
    CHECK((a->Dirty() & kDrawDirty) != 0);
    CHECK((s.Root()->Dirty() & kDrawDirty) != 0);
    CHECK((s.Root()->Dirty() & kLayoutDirty) == 0);
    CHECK(s.NeedsRedraw());
    s.Frame(0, &list);
    CHECK(s.LayoutPasses() == passes); // a draw change needs no layout
    b->MarkDirty(kLayoutDirty);
    CHECK((s.Root()->Dirty() & kLayoutDirty) != 0);
    CHECK((s.Root()->Dirty() & kDrawDirty) != 0); // layout implies draw
    s.Frame(0, &list);
    CHECK(s.LayoutPasses() == passes + 1);
    CHECK(s.Root()->Dirty() == 0);
    // Changes that do not change anything dirty nothing.
    b->SetBounds({0, 0, 10, 10});
    b->SetVisible(true);
    b->SetEnabled(true);
    CHECK(s.Root()->Dirty() == 0);
    b->SetVisible(false);
    CHECK((s.Root()->Dirty() & kLayoutDirty) != 0);
    s.Frame(0, &list);
    a->SetEnabled(false);
    CHECK((s.Root()->Dirty() & kDrawDirty) != 0);
    CHECK((s.Root()->Dirty() & kLayoutDirty) == 0);
    // Adding and removing children change layout.
    s.Frame(0, &list);
    Add(a, "c", {0, 0, 1, 1});
    CHECK((a->Dirty() & kLayoutDirty) != 0);
    s.Frame(0, &list);
    a->RemoveChild(a->FindByName("c"));
    CHECK((a->Dirty() & kLayoutDirty) != 0);
}

TEST_CASE("Widget tree: hit testing follows drawing order, clips, visibility and pass-through", "[ui][tree]") {
    Surface s(200, 200, 1);
    Probe* under = Add(s.Root(), "under", {10, 10, 50, 50});
    Probe* over = Add(s.Root(), "over", {30, 30, 50, 50});
    CHECK(s.Root()->HitTest({40, 40}) == over);   // the later sibling is on top
    CHECK(s.Root()->HitTest({15, 15}) == under);
    CHECK(s.Root()->HitTest({100, 100}) == s.Root());
    CHECK(s.Root()->HitTest({10, 10}) == under);  // the top-left edge is inside
    CHECK(s.Root()->HitTest({60, 60}) == over);   // 60 is inside over (30..80) and outside under (10..60)
    over->SetVisible(false);
    CHECK(s.Root()->HitTest({40, 40}) == under);
    over->SetVisible(true);

    // Pass-through: the widget is skipped, its children are not.
    over->SetPassThrough(true);
    CHECK(s.Root()->HitTest({40, 40}) == under);
    Probe* inner = Add(over, "inner", {35, 35, 10, 10});
    CHECK(s.Root()->HitTest({40, 40}) == inner);
    CHECK(s.Root()->HitTest({70, 70}) == s.Root()); // over itself does not take it
    over->SetPassThrough(false);

    // Clipping: a child outside a clipping parent is not hit, outside a non-clipping parent it is.
    Probe* far = Add(over, "far", {150, 150, 20, 20});
    CHECK(s.Root()->HitTest({160, 160}) == s.Root());
    over->SetClipsChildren(false);
    CHECK(s.Root()->HitTest({160, 160}) == far);
    CHECK(inner != nullptr);
}

TEST_CASE("Widget tree: removal inside a handler is deferred and the widget dies after the dispatch", "[ui][tree]") {
    Surface s(100, 100, 1);
    Probe* a = Add(s.Root(), "a", {0, 0, 50, 50});
    Probe* b = Add(s.Root(), "b", {60, 0, 30, 30});
    a->remove_self_on_down = true;
    Event down;
    down.type = EventType::PointerDown;
    down.position = {5, 5};
    down.button = 1;
    s.Dispatch(down);
    // `a` is destroyed by now: the next lines must not touch it.
    CHECK(s.Root()->FindByName("a") == nullptr);
    CHECK(s.Root()->ChildCount() == 1);
    CHECK(s.Root()->ChildAt(0) == b);
    CHECK(s.Capture() == nullptr); // the capture died with the widget
    CHECK(s.Hover() == nullptr);
}

TEST_CASE("Widget tree: a removal handler sees the tree unchanged until the dispatch ends", "[ui][tree]") {
    Surface s(100, 100, 1);
    Probe* a = Add(s.Root(), "a", {0, 0, 50, 50});
    Add(s.Root(), "b", {60, 0, 30, 30});
    a->remove_self_on_down = true;
    // Observe the child count from inside the handler through a second probe that shares the parent.
    class Watcher : public Widget {
    public:
        Watcher() : Widget("watcher") {}
        usize seen = 99;
        void OnEvent(Event& e) override {
            if (e.type == EventType::PointerDown) {
                Widget* parent = Parent();
                parent->RemoveChild(parent->FindByName("a"));
                seen = parent->ChildCount();
                e.handled = true;
            }
        }
    };
    UniquePtr<Watcher> w = MakeUnique<Watcher>();
    Watcher* watcher = w.Get();
    s.Root()->AddChild(UniquePtr<Widget>(w.Release()));
    watcher->SetBounds({0, 60, 30, 30});
    Event down;
    down.type = EventType::PointerDown;
    down.position = {5, 70};
    down.button = 1;
    s.Dispatch(down);
    CHECK(watcher->seen == 3); // a, b and the watcher were all still there inside the handler
    CHECK(s.Root()->ChildCount() == 2);
    CHECK(s.Root()->FindByName("a") == nullptr);
}

TEST_CASE("Widget tree: removing a subtree clears focus, capture and hover held inside it", "[ui][tree]") {
    Surface s(100, 100, 1);
    Probe* panel = Add(s.Root(), "panel", {0, 0, 80, 80});
    Probe* leaf = Add(panel, "leaf", {10, 10, 20, 20});
    s.SetFocus(leaf);
    CHECK(s.Focus() == leaf);
    Event down;
    down.type = EventType::PointerDown;
    down.position = {15, 15};
    down.button = 1;
    s.Dispatch(down);
    CHECK(s.Capture() == leaf);
    CHECK(s.Hover() == leaf);
    s.Root()->RemoveChild(panel); // not during dispatch: immediate
    CHECK(s.Focus() == nullptr);
    CHECK(s.Capture() == nullptr);
    CHECK(s.Hover() == nullptr);
    // Each reference on its own.
    Probe* a = Add(s.Root(), "a", {0, 0, 40, 40});
    s.SetFocus(a);
    a->RemoveFromParent();
    CHECK(s.Focus() == nullptr);
    Probe* b = Add(s.Root(), "b", {0, 0, 40, 40});
    s.SetCapture(b);
    b->RemoveFromParent();
    CHECK(s.Capture() == nullptr);
    Probe* c = Add(s.Root(), "c", {0, 0, 40, 40});
    Event move;
    move.type = EventType::PointerMove;
    move.position = {5, 5};
    s.Dispatch(move);
    CHECK(s.Hover() == c);
    c->RemoveFromParent();
    CHECK(s.Hover() == nullptr);
    // Events still work afterwards.
    s.Dispatch(move);
    CHECK(s.Hover() == s.Root());
}

TEST_CASE("Widget tree: the effective enabled state and the state bits", "[ui][tree]") {
    Surface s(100, 100, 1);
    Probe* a = Add(s.Root(), "a", {0, 0, 50, 50});
    Probe* b = Add(a, "b", {0, 0, 10, 10});
    CHECK(b->EffectivelyEnabled());
    CHECK((b->State() & kStateDisabled) == 0);
    a->SetEnabled(false);
    CHECK(b->Enabled());
    CHECK_FALSE(b->EffectivelyEnabled());
    CHECK((b->State() & kStateDisabled) != 0);
    b->SetChecked(true);
    CHECK((b->State() & kStateChecked) != 0);
    b->SetChecked(false);
    CHECK((b->State() & kStateChecked) == 0);
}


TEST_CASE("A subtree theme override styles that subtree only, and removing it restyles", "[ui][theme]") {
    Harness h(200, 100, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    h.GetSurface().SetTheme(&DefaultLightTheme());
    UniquePtr<Panel> a = MakeUnique<Panel>(StringView("light_panel"));
    UniquePtr<Panel> b = MakeUnique<Panel>(StringView("dark_panel"));
    Panel* light = a.Get();
    Panel* dark = b.Get();
    light->SetBounds(Rect{0, 0, 100, 100});
    dark->SetBounds(Rect{100, 0, 100, 100});
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(a.Release()));
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(b.Release()));
    dark->SetThemeOverride(&DefaultDarkTheme());
    CHECK(dark->ThemeOverride() == &DefaultDarkTheme());
    h.Advance(0);
    RasterImage image;
    h.Render(&image);
    const u8* left = image.Pixel(50, 50);
    const u8* right = image.Pixel(150, 50);
    CHECK(left[0] + left[1] + left[2] > right[0] + right[1] + right[2] + 100); // the light panel is much brighter
    CHECK(h.GetSurface().Themes().ActiveOverrides() == 0);                    // every push was popped
    dark->SetThemeOverride(nullptr);
    h.Advance(0);
    h.Render(&image);
    const u8* again = image.Pixel(150, 50);
    CHECK(again[0] == left[0]);
}
