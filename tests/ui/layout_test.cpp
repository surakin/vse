#include <cmath>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/layout.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/text_block.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

// A widget with a size of its own (no content) that draws its bounds in a color.
class Block : public Widget {
public:
    Block(StringView name, f32 w, f32 h, u32 color = 0xFF0000FFu) : Widget(name), color_(color) {
        LayoutProps p;
        p.width = w;
        p.height = h;
        SetProps(p);
    }
    void Draw(Canvas& c) override { c.DrawRect(Bounds(), color_); }

private:
    u32 color_;
};

// A widget that invalidates layout from Arrange `times` times (a widget that does not settle).
class Restless : public Widget {
public:
    explicit Restless(i32 times) : Widget("restless"), times_(times) {}
    void Arrange(const Rect& rect) override {
        Widget::Arrange(rect);
        if (times_ != 0) {
            if (times_ > 0) --times_;
            MarkDirty(kLayoutDirty);
        }
    }

private:
    i32 times_;
};

template <typename T>
T* Add(Widget* parent, UniquePtr<T> w) {
    T* raw = w.Get();
    parent->AddChild(UniquePtr<Widget>(w.Release()));
    return raw;
}

Block* AddBlock(Widget* parent, StringView name, f32 w, f32 h, LayoutProps extra = LayoutProps()) {
    Block* b = Add(parent, MakeUnique<Block>(name, w, h));
    extra.width = w;
    extra.height = h;
    b->SetProps(extra);
    return b;
}

// A container filling the surface.
template <typename T, typename... Args>
T* Fill(Surface* s, Args&&... args) {
    T* c = Add(s->Root(), MakeUnique<T>(static_cast<Args&&>(args)...));
    LayoutProps p = c->Props();
    p.fill_parent = true;
    c->SetProps(p);
    return c;
}

bool Near(f32 a, f32 b) { return std::fabs(a - b) < 1e-3f; }

bool Is(const Widget* w, f32 x, f32 y, f32 width, f32 height) {
    const Rect& r = w->Bounds();
    const bool ok = Near(r.x, x) && Near(r.y, y) && Near(r.w, width) && Near(r.h, height);
    if (!ok) std::printf("  %s is (%g, %g, %g, %g), expected (%g, %g, %g, %g)\n", w->Name().Length() > 0 ? String<HeapAllocator>(HeapAllocator::Default(), w->Name()).CStr() : "?", r.x, r.y, r.w, r.h, x, y, width, height);
    return ok;
}

} // namespace

TEST_CASE("Layout: a horizontal box places children with spacing and padding and stretches them across", "[ui][layout]") {
    Surface s(200, 100, 1);
    BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Horizontal, StringView("box"));
    box->SetSpacing(5);
    box->SetPadding(Insets(4));
    Widget* a = AddBlock(box, "a", 20, 10);
    Widget* b = AddBlock(box, "b", 30, 10);
    Widget* c = AddBlock(box, "c", 40, 10);
    s.Frame(0);
    CHECK(Is(box, 0, 0, 200, 100));
    CHECK(Is(a, 4, 4, 20, 92)); // height stretches to the content box
    CHECK(Is(b, 29, 4, 30, 92));
    CHECK(Is(c, 64, 4, 40, 92));
}

TEST_CASE("Layout: weights share the free space and a maximum freezes a child", "[ui][layout]") {
    {
        Surface s(200, 50, 1);
        BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Horizontal, StringView("box"));
        LayoutProps w1; w1.weight = 1;
        LayoutProps w3; w3.weight = 3;
        Widget* a = AddBlock(box, "a", 20, 10, w1);
        Widget* b = AddBlock(box, "b", 20, 10, w3);
        s.Frame(0);
        CHECK(Is(a, 0, 0, 60, 50));   // 20 + 160 * 1/4
        CHECK(Is(b, 60, 0, 140, 50)); // 20 + 160 * 3/4
    }
    {
        Surface s(200, 50, 1);
        BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Horizontal, StringView("box"));
        LayoutProps capped; capped.weight = 1; capped.max.w = 50;
        LayoutProps open; open.weight = 1;
        Widget* a = AddBlock(box, "a", 20, 10, capped);
        Widget* b = AddBlock(box, "b", 20, 10, open);
        s.Frame(0);
        CHECK(Is(a, 0, 0, 50, 50)); // its share would be 100: it stops at its maximum and the other takes the rest
        CHECK(Is(b, 50, 0, 150, 50));
    }
    {
        Surface s(200, 50, 1);
        BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Horizontal, StringView("box"));
        LayoutProps capped; capped.weight = 1; capped.max.w = 50;
        LayoutProps open; open.weight = 1;
        // Content-sized children (no explicit width): they start at 0, the capped one stops at 50 and the other takes the rest.
        Widget* a = Add(box, MakeUnique<Spacer>(1.0f));
        a->SetProps(capped);
        Widget* b = Add(box, MakeUnique<Spacer>(1.0f));
        b->SetProps(open);
        s.Frame(0);
        CHECK(Is(a, 0, 0, 50, 50));
        CHECK(Is(b, 50, 0, 150, 50));
    }
}

TEST_CASE("Layout: too little room shrinks children towards their minimum", "[ui][layout]") {
    Surface s(100, 50, 1);
    BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Horizontal, StringView("box"));
    LayoutProps with_min; with_min.min.w = 40;
    Widget* a = AddBlock(box, "a", 80, 10, with_min);
    Widget* b = AddBlock(box, "b", 80, 10);
    s.Frame(0);
    // 160 wanted in 100: a gives up (80-40)*0.5, b (80-0)*0.5.
    CHECK(Is(a, 0, 0, 60, 50));
    CHECK(Is(b, 60, 0, 40, 50));
    // Minimums that cannot be met overflow instead of going below.
    LayoutProps big_min; big_min.min.w = 90;
    a->SetProps([&] { LayoutProps p = big_min; p.width = 80; p.height = 10; return p; }());
    b->SetProps([&] { LayoutProps p = big_min; p.width = 80; p.height = 10; return p; }());
    s.Frame(0);
    CHECK(Is(a, 0, 0, 90, 50));
    CHECK(Is(b, 90, 0, 90, 50)); // runs past the box's right edge, as documented
}

TEST_CASE("Layout: alignment across the axis", "[ui][layout]") {
    Surface s(100, 100, 1);
    BoxLayout* box = Fill<BoxLayout>(&s, Orientation::Vertical, StringView("box"));
    LayoutProps start; start.align_x = Align::Start;
    LayoutProps center; center.align_x = Align::Center;
    LayoutProps end; end.align_x = Align::End;
    LayoutProps stretch;
    Widget* a = AddBlock(box, "a", 20, 10, start);
    Widget* b = AddBlock(box, "b", 20, 10, center);
    Widget* c = AddBlock(box, "c", 20, 10, end);
    Widget* d = AddBlock(box, "d", 20, 10, stretch);
    s.Frame(0);
    CHECK(Is(a, 0, 0, 20, 10));
    CHECK(Is(b, 40, 10, 20, 10));
    CHECK(Is(c, 80, 20, 20, 10));
    CHECK(Is(d, 0, 30, 100, 10)); // stretch fills the width whatever the preferred width
}

TEST_CASE("Layout: empty, single child, collapsed and hidden children", "[ui][layout]") {
    Surface s(100, 100, 1);
    BoxLayout* empty = Fill<BoxLayout>(&s, Orientation::Vertical, StringView("empty"));
    s.Frame(0);
    CHECK(Is(empty, 0, 0, 100, 100));
    Widget* only = AddBlock(empty, "only", 30, 20);
    s.Frame(0);
    CHECK(Is(only, 0, 0, 100, 20));
    Widget* second = AddBlock(empty, "second", 30, 20);
    Widget* third = AddBlock(empty, "third", 30, 20);
    s.Frame(0);
    CHECK(Is(third, 0, 40, 100, 20));
    second->SetCollapsed(true); // takes no space
    s.Frame(0);
    CHECK(Is(third, 0, 20, 100, 20));
    CHECK_FALSE(second->Shown());
    CHECK(s.Root()->HitTest({10, 5}) == only);
    second->SetCollapsed(false);
    second->SetVisible(false); // keeps its space but is not drawn or hit
    s.Frame(0);
    CHECK(Is(third, 0, 40, 100, 20));
    CHECK_FALSE(second->Shown());
}

TEST_CASE("Layout: a stack overlays its children and aligns each", "[ui][layout]") {
    Surface s(100, 50, 1);
    StackLayout* stack = Fill<StackLayout>(&s, StringView("stack"));
    stack->SetPadding(Insets(10));
    LayoutProps center; center.align_x = Align::Center; center.align_y = Align::Center;
    LayoutProps corner; corner.align_x = Align::End; corner.align_y = Align::End;
    Widget* a = AddBlock(stack, "a", 20, 10, center);
    Widget* b = AddBlock(stack, "b", 8, 8, corner);
    Widget* c = AddBlock(stack, "c", 5, 5); // stretch fills the content box
    s.Frame(0);
    CHECK(Is(a, 40, 20, 20, 10));
    CHECK(Is(b, 82, 32, 8, 8));
    CHECK(Is(c, 10, 10, 80, 30));
}

TEST_CASE("Layout: anchors follow the size of the container", "[ui][layout]") {
    const f32 sizes[][2] = {{200, 100}, {100, 100}, {400, 200}};
    for (const auto& sz : sizes) {
        Surface s(sz[0], sz[1], 1);
        AnchorLayout* anchors = Fill<AnchorLayout>(&s, StringView("anchors"));
        Widget* fill = Add(anchors, MakeUnique<Widget>("fill"));
        LayoutProps f;
        f.anchor = AnchorSpec{0, 0, 1, 1, 10, 10, -10, -10};
        fill->SetProps(f);
        Widget* centered = AddBlock(anchors, "centered", 20, 10);
        LayoutProps c = centered->Props();
        c.anchor = AnchorSpec{0.5f, 0.5f, 0.5f, 0.5f, -10, -5, 0, 0};
        centered->SetProps(c);
        Widget* right = Add(anchors, MakeUnique<Widget>("right"));
        LayoutProps r;
        r.anchor = AnchorSpec{0.75f, 0, 1, 0.5f, 0, 0, 0, 0};
        right->SetProps(r);
        s.Frame(0);
        INFO("surface " << sz[0] << "x" << sz[1]);
        CHECK(Is(fill, 10, 10, sz[0] - 20, sz[1] - 20));
        CHECK(Is(centered, sz[0] * 0.5f - 10, sz[1] * 0.5f - 5, 20, 10));
        CHECK(Is(right, sz[0] * 0.75f, 0, sz[0] * 0.25f, sz[1] * 0.5f));
    }
}

TEST_CASE("Layout: a grid sizes fixed, auto and weighted tracks and honours spans", "[ui][layout]") {
    Surface s(300, 100, 1);
    GridLayout* grid = Fill<GridLayout>(&s, StringView("grid"));
    grid->AddColumn(TrackSize::Fixed(50));
    grid->AddColumn(TrackSize::Weight(1));
    grid->AddColumn(TrackSize::Auto());
    grid->AddRow(TrackSize::Auto());
    grid->AddRow(TrackSize::Weight(1));
    grid->SetSpacing(10, 5);
    auto at = [](u32 col, u32 row, u32 cspan = 1, u32 rspan = 1) {
        LayoutProps p;
        p.column = col;
        p.row = row;
        p.column_span = cspan;
        p.row_span = rspan;
        return p;
    };
    Widget* a = AddBlock(grid, "a", 30, 10, at(0, 0));
    Widget* b = AddBlock(grid, "b", 20, 20, at(1, 0));
    Widget* c = AddBlock(grid, "c", 25, 15, at(2, 0));
    Widget* d = AddBlock(grid, "d", 10, 10, at(0, 1, 2, 1)); // spans the fixed and the weighted column
    s.Frame(0);
    // Columns: fixed 50, auto 25, weight takes the rest: 300 - 50 - 25 - 20 gaps = 205 (its preferred 20 plus all the free space).
    // Row 0 is auto: the tallest single-cell child is 20; row 1 takes the rest: 100 - 20 - 5.
    CHECK(Is(a, 0, 0, 50, 20));
    CHECK(Is(b, 60, 0, 205, 20));
    CHECK(Is(c, 275, 0, 25, 20));
    CHECK(Is(d, 0, 25, 265, 75)); // 50 + 10 + 205
    // Without declared tracks every track is auto.
    Surface s2(100, 100, 1);
    GridLayout* auto_grid = Fill<GridLayout>(&s2, StringView("auto"));
    Widget* e = AddBlock(auto_grid, "e", 10, 10, at(0, 0));
    Widget* f = AddBlock(auto_grid, "f", 30, 20, at(1, 1));
    s2.Frame(0);
    CHECK(Is(e, 0, 0, 10, 10));
    CHECK(Is(f, 10, 10, 30, 20));
}

TEST_CASE("Layout: a label's height follows the width it is wrapped at, and its layout is cached", "[ui][layout][text]") {
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    BoxLayout* box = Add(h.GetSurface().Root(), MakeUnique<BoxLayout>(Orientation::Vertical, StringView("box")));
    LayoutProps fill; fill.fill_parent = true;
    box->SetProps(fill);
    TextBlock* wrapped = Add(box, MakeUnique<TextBlock>(StringView("wrapped"), StringView("The quick brown fox jumps over the lazy dog and keeps running")));
    wrapped->SetWrap(true);
    TextBlock* plain = Add(box, MakeUnique<TextBlock>(StringView("plain"), StringView("The quick brown fox jumps over the lazy dog and keeps running")));
    h.Advance(0);
    const Rect wide = wrapped->Bounds();
    CHECK(wide.w == 300);
    const f32 plain_h = plain->Bounds().h;
    CHECK(plain_h > 5);
    CHECK(wide.h >= plain_h); // at 300 wide it needs one line or more
    h.GetSurface().Resize(100, 200, 1);
    h.Advance(0);
    CHECK(wrapped->Bounds().w == 100);
    CHECK(wrapped->Bounds().h > wide.h);          // wrapped to more lines than at 300
    CHECK(std::fmod(wrapped->Bounds().h, plain_h) == 0); // a whole number of lines
    CHECK(plain->Bounds().h == plain_h);         // not wrapped: one line however narrow
    // The layout is cached: another frame with nothing changed lays out nothing.
    const u64 count = wrapped->LayoutCount();
    h.GetSurface().Root()->MarkDirty(kLayoutDirty);
    h.Advance(0);
    CHECK(wrapped->LayoutCount() == count);
    wrapped->SetText("short");
    h.Advance(0);
    CHECK(wrapped->LayoutCount() > count);
    CHECK(wrapped->Bounds().h == plain_h);
}

TEST_CASE("Layout: neighbours share an edge at 1.25x with no seam and no overlap", "[ui][layout][raster]") {
    Harness h(100, 20, 1.25f, StringView(UI_TEST_FONT_FILE));
    BoxLayout* box = Add(h.GetSurface().Root(), MakeUnique<BoxLayout>(Orientation::Horizontal, StringView("box")));
    LayoutProps fill; fill.fill_parent = true;
    box->SetProps(fill);
    const u32 colors[3] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u};
    for (int i = 0; i < 3; ++i) {
        Block* b = Add(box, MakeUnique<Block>(StringView("b"), 0.0f, 20.0f, colors[i]));
        LayoutProps p;
        p.weight = 1;
        p.height = 20;
        b->SetProps(p);
    }
    h.Advance(0);
    RasterImage img;
    h.Render(&img, 0xFF000000u);
    CHECK(img.width == 125);
    u32 counted[3] = {0, 0, 0};
    for (u32 x = 0; x < img.width; ++x) {
        const u8* p = img.Pixel(x, 10);
        CHECK((p[0] != 0 || p[1] != 0 || p[2] != 0)); // no seam
        if (p[0] == 255) ++counted[0];
        if (p[1] == 255) ++counted[1];
        if (p[2] == 255) ++counted[2];
    }
    CHECK(counted[0] + counted[1] + counted[2] == 125); // every column belongs to exactly one block
    CHECK(counted[0] >= 41);
    CHECK(counted[0] <= 42);
}

TEST_CASE("Layout: ten resizes between frames cost one layout, the same size none", "[ui][layout]") {
    Surface s(100, 100, 1);
    Fill<StackLayout>(&s, StringView("stack"));
    s.Frame(0);
    const u64 passes = s.LayoutPasses();
    for (int i = 0; i < 10; ++i) s.Resize(100.0f + static_cast<f32>(i), 100, 1);
    CHECK(s.LayoutPasses() == passes);
    s.Frame(0);
    CHECK(s.LayoutPasses() == passes + 1);
    s.Resize(109, 100, 1); // the size it already has
    s.Frame(0);
    CHECK(s.LayoutPasses() == passes + 1);
    CHECK(s.Root()->ChildAt(0)->Bounds().w == 109);
}

TEST_CASE("Layout: a widget that invalidates layout is given two passes, then the surface gives up", "[ui][layout]") {
    Surface s(100, 100, 1);
    Restless* once = Add(s.Root(), MakeUnique<Restless>(1));
    s.Frame(0);
    CHECK(s.LayoutOverruns() == 0); // the second pass settled it
    once->RemoveFromParent();
    Restless* forever = Add(s.Root(), MakeUnique<Restless>(-1));
    s.Frame(0); // must return
    CHECK(s.LayoutOverruns() == 1);
    s.Frame(0);
    CHECK(s.LayoutOverruns() == 1); // it gave up for that frame and stays quiet until something invalidates again
    forever->MarkDirty(kLayoutDirty);
    s.Frame(0);
    CHECK(s.LayoutOverruns() == 2);
}

namespace {

struct Lcg {
    u64 state;
    u32 Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * static_cast<f32>(Next() % 10000) / 10000.0f; }
    u32 Below(u32 n) { return Next() % n; }
};

// Builds a random subtree of containers and blocks under `parent`.
void Grow(Widget* parent, Lcg* rng, int depth) {
    const u32 kids = 1 + rng->Below(4);
    for (u32 i = 0; i < kids; ++i) {
        const u32 kind = depth > 0 ? rng->Below(5) : 4;
        LayoutProps p;
        p.weight = rng->Below(3) == 0 ? rng->Range(0.5f, 3.0f) : 0.0f;
        p.align_x = static_cast<Align>(rng->Below(4));
        p.align_y = static_cast<Align>(rng->Below(4));
        p.min = Size{rng->Range(0, 8), rng->Range(0, 8)};
        // A maximum on a container could make it smaller than its children need (overflow, which the test does not count), so only leaves get one.
        if (kind == 4 && rng->Below(3) == 0) p.max = Size{rng->Range(20, 80), rng->Range(20, 80)};
        p.column = rng->Below(3);
        p.row = rng->Below(3);
        // Spanning children do not size auto tracks and fixed tracks can be narrower than a child: both may overflow, so the property test uses neither.
        Widget* child = nullptr;
        if (kind == 0) {
            BoxLayout* b = Add(parent, MakeUnique<BoxLayout>(rng->Below(2) == 0 ? Orientation::Horizontal : Orientation::Vertical, StringView("box")));
            b->SetSpacing(rng->Range(0, 6));
            b->SetPadding(Insets(rng->Range(0, 6)));
            child = b;
        } else if (kind == 1) {
            child = Add(parent, MakeUnique<StackLayout>(StringView("stack")));
        } else if (kind == 2) {
            GridLayout* g = Add(parent, MakeUnique<GridLayout>(StringView("grid")));
            const u32 cols = 1 + rng->Below(3);
            for (u32 c = 0; c < cols; ++c) g->AddColumn(rng->Below(2) == 0 ? TrackSize::Auto() : TrackSize::Weight(rng->Range(0.5f, 2)));
            g->SetSpacing(rng->Range(0, 5), rng->Range(0, 5));
            child = g;
        } else if (kind == 3) {
            child = Add(parent, MakeUnique<Spacer>(rng->Range(0, 2)));
        } else {
            child = Add(parent, MakeUnique<Block>(StringView("block"), rng->Range(5, 60), rng->Range(5, 60)));
            p.width = child->Props().width;
            p.height = child->Props().height;
        }
        if (kind != 3) child->SetProps(p); else { p.weight = child->Props().weight; child->SetProps(p); }
        if (kind < 3) Grow(child, rng, depth - 1);
    }
}

bool Finite(f32 v) { return std::isfinite(v); }

// Every widget has a finite, non-negative size; for the containers whose children must stay inside (not the anchors) each child is inside its parent.
bool CheckTree(Widget* w, bool* inside_ok) {
    const Rect& r = w->Bounds();
    if (!Finite(r.x) || !Finite(r.y) || !Finite(r.w) || !Finite(r.h) || r.w < 0 || r.h < 0) return false;
    for (usize i = 0; i < w->ChildCount(); ++i) {
        Widget* c = w->ChildAt(i);
        const Rect& cr = c->Bounds();
        const f32 eps = 0.01f;
        if (cr.x < r.x - eps || cr.y < r.y - eps || cr.x + cr.w > r.x + r.w + eps || cr.y + cr.h > r.y + r.h + eps) *inside_ok = false;
        if (!CheckTree(c, inside_ok)) return false;
    }
    return true;
}

} // namespace

TEST_CASE("Layout: random trees never produce a negative, non-finite or escaping rectangle", "[ui][layout][property]") {
    for (u64 seed = 1; seed <= 200; ++seed) {
        Lcg rng{seed * 7919};
        Surface probe(10, 10, 1);
        BoxLayout* root_box = Fill<BoxLayout>(&probe, rng.Below(2) == 0 ? Orientation::Horizontal : Orientation::Vertical, StringView("root"));
        Grow(root_box, &rng, 3);
        // The surface is as large as the tree wants, so nothing needs to overflow.
        const Size want = root_box->MeasureTotal(Constraints());
        const f32 scale = seed % 3 == 0 ? 1.25f : 1.0f;
        probe.Resize(want.w + 20, want.h + 20, scale);
        probe.Frame(0);
        bool inside = true;
        INFO("seed " << seed);
        CHECK(CheckTree(probe.Root(), &inside));
        CHECK(inside);
        CHECK(probe.LayoutOverruns() == 0);
        // A surface far too small: still finite and non-negative (overflow is allowed).
        probe.Resize(3, 3, 1);
        probe.Frame(0);
        bool ignored = true;
        CHECK(CheckTree(probe.Root(), &ignored));
        // Collapsing everything gives an empty layout.
        for (usize i = 0; i < root_box->ChildCount(); ++i) root_box->ChildAt(i)->SetCollapsed(true);
        probe.Frame(0);
        CHECK(CheckTree(probe.Root(), &ignored));
    }
}

TEST_CASE("Layout: a wild grid cell index is clamped to the last track and deep nesting stays fast", "[ui][layout]") {
    Surface s(100, 100, 1);
    GridLayout* grid = Fill<GridLayout>(&s, StringView("grid"));
    grid->AddColumn(TrackSize::Auto());
    LayoutProps p;
    p.column = 4000000000u;
    p.row = 4000000000u;
    p.column_span = 4000000000u;
    Widget* wild = AddBlock(grid, "wild", 10, 10, p);
    s.Frame(0); // must not allocate gigabytes or crash
    CHECK(wild->Bounds().w >= 0);
    CHECK(wild->Bounds().x >= 0);

    // 14 nested boxes, each with two children: measuring must not be exponential in the depth.
    Surface deep(400, 400, 1);
    Widget* parent = deep.Root();
    for (int i = 0; i < 14; ++i) {
        BoxLayout* b = Add(parent, MakeUnique<BoxLayout>(i % 2 == 0 ? Orientation::Horizontal : Orientation::Vertical, StringView("b")));
        LayoutProps fp;
        fp.fill_parent = (i == 0);
        b->SetProps(fp);
        AddBlock(b, "leaf", 5, 5);
        parent = b;
    }
    AddBlock(parent, "last", 5, 5);
    deep.Frame(0);
    CHECK(deep.LayoutOverruns() == 0);
    CHECK(parent->Bounds().w >= 0);
}
