#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/drag_drop.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/scroll.h"
#include "ui/widgets/tab_view.h"
#include "ui/widgets/text_field.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

Rect DdRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// Starts a drag with the payload it is configured with, once the pointer is past the slop after a press.
class DdSource : public Widget
{
public:
    explicit DdSource(StringView name) : Widget(name) {}
    void OnEvent(Event& e) override
    {
        if (e.type == EventType::PointerDown)
        {
            press = e.position;
            e.handled = true;
        }
        else if (e.type == EventType::PointerMove && GetSurface()->Capture() == this && !started &&
                 GetSurface()->PastDragSlop(press, e.position))
        {
            DragPayload p;
            if (text.Length() > 0)
            {
                p.AddText(text);
            }
            if (files.Length() > 0)
            {
                p.AddFiles(files);
            }
            if (with_pointer)
            {
                p.AddPointer(StringView("dd.source"), this);
            }
            p.SetAllowed(allowed);
            UniquePtr<Widget> visual;
            if (with_visual)
            {
                visual = MakeUnique<Widget>(StringView("ghost"));
                LayoutProps props;
                props.width = 30;
                props.height = 12;
                visual->SetProps(props);
            }
            started = GetSurface()->BeginDrag(static_cast<DragPayload&&>(p), static_cast<UniquePtr<Widget>&&>(visual),
                                              Point(4, 6));
            e.handled = true;
        }
    }
    void OnDragEnded(DragAction a) override
    {
        ++ended;
        ended_with = a;
        started = false;
    }
    StringView text = StringView("payload");
    StringView files;
    bool with_pointer = false;
    bool with_visual = false;
    u8 allowed = os::kAllDragActions;
    Point press;
    bool started = false;
    int ended = 0;
    DragAction ended_with = DragAction::Nothing;
};

class DdTarget : public Widget
{
public:
    explicit DdTarget(StringView name) : Widget(name) {}
    DragAction CanAccept(const DragPayload& p, Point) override
    {
        if (!accepts || !p.Has(DragEntryKind::Text))
        {
            return DragAction::Nothing;
        }
        return fixed != DragAction::Nothing ? fixed : p.Requested();
    }
    void OnDragEnter(const DragPayload&, Point) override { ++enters; }
    void OnDragOver(const DragPayload&, Point p, DragAction a) override
    {
        ++overs;
        last = p;
        last_action = a;
    }
    void OnDragLeave() override { ++leaves; }
    void OnDrop(const DragPayload& p, Point at, DragAction a) override
    {
        ++drops;
        drop_at = at;
        drop_action = a;
        dropped_text = String<HeapAllocator>(HeapAllocator::Default(), p.Text());
    }
    bool OnDragHoverDelay(Point) override
    {
        ++hovers;
        return true;
    }
    bool accepts = true;
    DragAction fixed = DragAction::Nothing;
    int enters = 0;
    int overs = 0;
    int leaves = 0;
    int drops = 0;
    int hovers = 0;
    Point last;
    Point drop_at;
    DragAction last_action = DragAction::Nothing;
    DragAction drop_action = DragAction::Nothing;
    String<HeapAllocator> dropped_text{HeapAllocator::Default(), StringView()};
};

bool DdSamePixel(const RasterImage& a, const RasterImage& b, u32 x, u32 y)
{
    const u8* p = a.Pixel(x, y);
    const u8* q = b.Pixel(x, y);
    return p[0] == q[0] && p[1] == q[1] && p[2] == q[2] && p[3] == q[3];
}

struct DdRig
{
    DdRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<DdSource> s = MakeUnique<DdSource>(StringView("src"));
        src = s.Get();
        src->SetBounds(DdRect(10, 10, 80, 40));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(s.Release()));
        UniquePtr<DdTarget> t = MakeUnique<DdTarget>(StringView("tgt"));
        tgt = t.Get();
        tgt->SetBounds(DdRect(200, 100, 100, 80));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(t.Release()));
        h.Advance(0);
    }
    Surface& S() { return h.GetSurface(); }
    // Presses on the source and moves past the slop, so the drag has begun.
    void Begin()
    {
        h.PointerDown(20, 20);
        h.PointerMove(40, 30);
    }
    void MoveWith(f32 x, f32 y, u32 mods)
    {
        Event e;
        e.type = EventType::PointerMove;
        e.position = Point(x, y);
        e.modifiers = mods;
        S().Post(e);
        h.Advance(0);
    }
    Harness h;
    DdSource* src;
    DdTarget* tgt;
};

}  // namespace

TEST_CASE("a payload holds typed entries and refuses what is too big or malformed", "[ui][drag]")
{
    DragPayload p;
    CHECK(p.AddText(StringView("hi")));
    CHECK(p.AddFiles(StringView("/a/b\n/c")));
    const byte raw[3] = {byte(1), byte(2), byte(3)};
    CHECK(p.AddCustom(StringView("application/x-test"), raw, 3));
    CHECK_FALSE(p.AddCustom(StringView(), raw, 3));
    int thing = 0;
    CHECK(p.AddPointer(StringView("tag"), &thing));
    CHECK(p.EntryCount() == 4);
    CHECK(p.Text() == StringView("hi"));
    CHECK(p.PointerOf(StringView("tag")) == &thing);
    CHECK(p.PointerOf(StringView("other")) == nullptr);
    CHECK(p.Find(DragEntryKind::Custom, StringView("application/x-test")) != nullptr);
    CHECK(p.OsRepresentable());
    DragPayload only_pointer;
    only_pointer.AddPointer(StringView("tag"), &thing);
    CHECK_FALSE(only_pointer.OsRepresentable());  // a pointer never leaves the process
    DragPayload bad;
    const char invalid[] = {'a', '\xFF',
                            'b'};  // an invalid UTF-8 byte (a literal: a cast of 0xFF to char truncates, C4310 on MSVC)
    bad.AddText(StringView(invalid, 3));
    CHECK(bad.Entry(0).bytes.Size() == 5);  // the bad byte became U+FFFD (three bytes)
    DragPayload full;
    for (int i = 0; i < 20; ++i)
    {
        full.AddText(StringView("x"));
    }
    CHECK(full.EntryCount() == DragPayload::kMaxEntries);
}

TEST_CASE("a drag begins only after a press and past the slop, and a release over nothing cancels it", "[ui][drag]")
{
    DdRig rig;
    rig.h.PointerDown(20, 20);
    rig.h.PointerMove(22, 21);  // within the slop
    CHECK_FALSE(rig.S().Dragging());
    rig.h.PointerMove(40, 30);
    CHECK(rig.S().Dragging());
    CHECK(rig.S().DragSource() == rig.src);
    rig.h.PointerUp(40, 30);
    CHECK_FALSE(rig.S().Dragging());
    CHECK(rig.src->ended == 1);
    CHECK(rig.src->ended_with == DragAction::Nothing);  // nothing under the pointer: cancelled
    // Without a button down a drag cannot begin.
    DragPayload p;
    p.AddText(StringView("x"));
    CHECK_FALSE(rig.S().BeginDrag(static_cast<DragPayload&&>(p)));
    // An empty payload, or one that allows nothing, never begins.
    rig.h.PointerDown(20, 20);
    DragPayload empty;
    CHECK_FALSE(rig.S().BeginDrag(static_cast<DragPayload&&>(empty)));
    DragPayload none;
    none.AddText(StringView("x"));
    none.SetAllowed(0);
    CHECK_FALSE(rig.S().BeginDrag(static_cast<DragPayload&&>(none)));
    rig.h.PointerUp(20, 20);
}

TEST_CASE("a drop reaches the target with the local position and the requested action", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    rig.h.PointerMove(250, 130);
    CHECK(rig.S().DragTarget() == rig.tgt);
    CHECK(rig.tgt->enters == 1);
    CHECK((rig.tgt->State() & kStateDragOver) != 0);
    CHECK(rig.tgt->last.x == 50);
    CHECK(rig.tgt->last.y == 30);
    CHECK(rig.tgt->last_action == DragAction::Move);  // the default when move is allowed
    rig.h.PointerUp(250, 130);
    CHECK(rig.tgt->drops == 1);
    CHECK(rig.tgt->drop_at.x == 50);
    CHECK(rig.tgt->dropped_text.View() == StringView("payload"));
    CHECK(rig.tgt->drop_action == DragAction::Move);
    CHECK((rig.tgt->State() & kStateDragOver) == 0);
    CHECK(rig.src->ended == 1);
    CHECK(rig.src->ended_with == DragAction::Move);
}

TEST_CASE("Ctrl asks for a copy and Ctrl+Shift for a link, within what the source allows", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    rig.MoveWith(250, 130, 0);
    CHECK(rig.tgt->last_action == DragAction::Move);
    rig.MoveWith(251, 130, os::kModCtrl);
    CHECK(rig.tgt->last_action == DragAction::Copy);
    rig.MoveWith(252, 130, os::kModCtrl | os::kModShift);
    CHECK(rig.tgt->last_action == DragAction::Link);
    rig.S().CancelDrag();
    rig.src->allowed = os::DragActionBit(DragAction::Copy);
    rig.Begin();
    rig.MoveWith(250, 130, 0);
    CHECK(rig.tgt->last_action == DragAction::Copy);  // move is not allowed
    rig.tgt->fixed = DragAction::Move;
    rig.MoveWith(251, 130, 0);
    CHECK(rig.S().DragTarget() == nullptr);  // an answer outside the allowed set is no answer
    rig.h.PointerUp(251, 130);
}

TEST_CASE("Escape cancels a drag and the target and source are told", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    rig.h.PointerMove(250, 130);
    rig.h.Key(Key::Escape);
    CHECK_FALSE(rig.S().Dragging());
    CHECK(rig.tgt->leaves == 1);
    CHECK(rig.tgt->drops == 0);
    CHECK(rig.src->ended_with == DragAction::Nothing);
    rig.h.PointerUp(250, 130);  // the release afterwards does nothing
    CHECK(rig.tgt->drops == 0);
}

TEST_CASE("the target is the topmost accepting widget under the pointer or its accepting ancestor", "[ui][drag]")
{
    DdRig rig;
    // A child that refuses inside a parent that accepts: the parent is the target.
    UniquePtr<DdTarget> parent = MakeUnique<DdTarget>(StringView("parent"));
    parent->SetBounds(DdRect(10, 100, 150, 100));
    DdTarget* parent_raw = parent.Get();
    UniquePtr<DdTarget> child = MakeUnique<DdTarget>(StringView("child"));
    child->SetBounds(DdRect(20, 110, 50, 50));
    child->accepts = false;
    DdTarget* child_raw = child.Get();
    parent->AddChild(UniquePtr<Widget>(child.Release()));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(parent.Release()));
    rig.h.Advance(0);
    rig.Begin();
    rig.h.PointerMove(30, 120);
    CHECK(rig.S().DragTarget() == parent_raw);
    child_raw->accepts = true;
    rig.h.PointerMove(31, 120);
    CHECK(rig.S().DragTarget() == child_raw);  // now the child takes it
    CHECK(parent_raw->leaves == 1);
    // Overlapping siblings: the later one is on top.
    rig.h.PointerMove(250, 130);
    CHECK(rig.S().DragTarget() == rig.tgt);
    UniquePtr<DdTarget> over = MakeUnique<DdTarget>(StringView("over"));
    over->SetBounds(DdRect(220, 110, 40, 40));
    DdTarget* over_raw = over.Get();
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(over.Release()));
    rig.h.PointerMove(230, 120);
    CHECK(rig.S().DragTarget() == over_raw);
    rig.h.PointerUp(230, 120);
    CHECK(over_raw->drops == 1);
    CHECK(rig.tgt->drops == 0);
}

TEST_CASE("a popup receives a drag like any widget and hides what lies under it", "[ui][drag]")
{
    DdRig rig;
    UniquePtr<DdTarget> pop = MakeUnique<DdTarget>(StringView("pop"));
    pop->SetBounds(DdRect(190, 90, 60, 60));
    DdTarget* pop_raw = pop.Get();
    rig.S().PopupLayer()->AddChild(UniquePtr<Widget>(pop.Release()));
    rig.h.Advance(0);
    rig.Begin();
    rig.h.PointerMove(210, 110);  // over both: the popup is hit first
    CHECK(rig.S().DragTarget() == pop_raw);
    rig.h.PointerMove(280, 170);  // only the root target
    CHECK(rig.S().DragTarget() == rig.tgt);
    rig.h.PointerUp(280, 170);
}

TEST_CASE("the cursor shows the action", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    CHECK(rig.S().RequestedCursor() == os::CursorShape::NotAllowed);  // over nothing
    rig.h.PointerMove(250, 130);
    CHECK(rig.S().RequestedCursor() == os::CursorShape::ResizeAll);  // move
    rig.MoveWith(251, 130, os::kModCtrl);
    CHECK(rig.S().RequestedCursor() == os::CursorShape::Arrow);  // copy
    rig.MoveWith(252, 130, os::kModCtrl | os::kModShift);
    CHECK(rig.S().RequestedCursor() == os::CursorShape::Hand);  // link
    rig.h.PointerUp(252, 130);
    CHECK(rig.S().RequestedCursor() == os::CursorShape::Arrow);
}

TEST_CASE("the drag visual follows the pointer in the top layer and goes away at the end", "[ui][drag]")
{
    DdRig rig;
    rig.src->with_visual = true;
    rig.Begin();
    Widget* ghost = rig.S().TooltipLayer()->ChildAt(0);
    REQUIRE(ghost != nullptr);
    CHECK(ghost->Bounds().x == 44);
    CHECK(ghost->Bounds().y == 36);
    rig.h.PointerMove(100, 90);
    CHECK(ghost->Bounds().x == 104);
    CHECK(ghost->Bounds().y == 96);
    CHECK(ghost->Bounds().w == 30);
    rig.h.PointerUp(100, 90);
    CHECK(rig.S().TooltipLayer()->ChildCount() == 0);
}

TEST_CASE("a widget under a drag is highlighted from the theme", "[ui][drag]")
{
    DdRig rig;
    RasterImage before;
    rig.h.Render(&before, 0xFFFFFFFFu);
    rig.Begin();
    rig.h.PointerMove(250, 130);
    RasterImage during;
    rig.h.Render(&during, 0xFFFFFFFFu);
    CHECK_FALSE(DdSamePixel(before, during, 250, 130));  // the drop-target shade is over the widget
    rig.h.PointerUp(250, 130);
    RasterImage after;
    rig.h.Render(&after, 0xFFFFFFFFu);
    CHECK(DdSamePixel(before, after, 250, 130));
}

TEST_CASE("destroying the source or the target mid-drag is safe", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    rig.h.PointerMove(250, 130);
    rig.tgt->RemoveFromParent();
    rig.h.Advance(0);
    rig.h.PointerMove(251, 130);
    CHECK(rig.S().DragTarget() == nullptr);
    rig.src->RemoveFromParent();
    rig.h.Advance(0);
    rig.h.PointerUp(251, 130);
    CHECK_FALSE(rig.S().Dragging());
}

TEST_CASE("a file list dropped on nothing reaches the root's file handler", "[ui][drag]")
{
    DdRig rig;
    struct Got
    {
        int calls = 0;
        String<HeapAllocator> paths{HeapAllocator::Default(), StringView()};
    } got;
    rig.S().OnFilesDropped().Connect(
        [](void* user, Surface*, StringView paths)
        {
            Got* g = static_cast<Got*>(user);
            ++g->calls;
            g->paths = String<HeapAllocator>(HeapAllocator::Default(), paths);
        },
        &got);
    rig.src->text = StringView();
    rig.src->files = StringView("/a/one\n/b/two");
    rig.Begin();
    rig.h.PointerMove(300, 250);
    CHECK(rig.S().CurrentDragAction() == DragAction::Copy);
    rig.h.PointerUp(300, 250);
    CHECK(got.calls == 1);
    CHECK(got.paths.View() == StringView("/a/one\n/b/two"));
}

// ---- auto-scroll and hover delay
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

class DdItems : public ListModel
{
public:
    explicit DdItems(usize n) : count(n)
    {
        for (usize i = 0; i < 6; ++i)
        {
            order[i] = i;
        }
    }
    usize ItemCount() const override { return count; }
    StringView ItemText(usize i) override
    {
        static const char* const names[] = {"A", "B", "C", "D", "E", "F"};
        if (count <= 6)
        {
            return StringView(names[order[i]]);
        }
        text = String<HeapAllocator>(HeapAllocator::Default(), StringView("item"));
        return text.View();
    }
    bool MoveItems(const usize* indices, usize n, usize to) override
    {
        if (!allow)
        {
            return false;
        }
        usize next[6];
        usize k = 0;
        for (usize i = 0; i < count && i < to; ++i)
        {
            bool moved = false;
            for (usize j = 0; j < n; ++j)
            {
                moved = moved || indices[j] == i;
            }
            if (!moved)
            {
                next[k++] = order[i];
            }
        }
        for (usize j = 0; j < n; ++j)
        {
            next[k++] = order[indices[j]];
        }
        for (usize i = to; i < count; ++i)
        {
            bool moved = false;
            for (usize j = 0; j < n; ++j)
            {
                moved = moved || indices[j] == i;
            }
            if (!moved)
            {
                next[k++] = order[i];
            }
        }
        for (usize i = 0; i < count; ++i)
        {
            order[i] = next[i];
        }
        return true;
    }
    String<HeapAllocator> Joined()
    {
        String<HeapAllocator> s(HeapAllocator::Default(), StringView());
        for (usize i = 0; i < count; ++i)
        {
            s.Append(ItemText(i));
        }
        return s;
    }
    usize count;
    usize order[6];
    bool allow = true;
    String<HeapAllocator> text{HeapAllocator::Default(), StringView()};
};

struct ListRig
{
    ListRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE)), items(6)
    {
        UniquePtr<ListBox> l = MakeUnique<ListBox>(StringView("list"));
        list = l.Get();
        list->SetBounds(DdRect(10, 10, 150, 200));
        list->SetModel(&items);
        list->SetMode(SelectionMode::Multiple);
        list->SetReorderable(true);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(l.Release()));
        h.Advance(0);
        h.Advance(0);
        rh = list->RowHeight();
    }
    f32 RowY(f32 row) const { return 10 + row * rh; }
    Harness h;
    DdItems items;
    ListBox* list;
    f32 rh = 0;
};

}  // namespace

TEST_CASE("a list box reorders by dragging the selected item", "[ui][drag][list]")
{
    ListRig rig;
    REQUIRE(rig.rh > 0);
    rig.h.Click(30, rig.RowY(1.5f));  // select B
    rig.h.PointerDown(30, rig.RowY(1.5f));
    rig.h.PointerMove(30, rig.RowY(2.5f));
    CHECK(rig.h.GetSurface().Dragging());
    rig.h.PointerMove(30, rig.RowY(4.0f) + 1);
    CHECK(rig.list->DropIndex() == 4);
    rig.h.PointerUp(30, rig.RowY(4.0f) + 1);
    CHECK(rig.items.Joined().View() == StringView("ACDBEF"));
    CHECK(rig.list->IsSelected(3));
    CHECK(rig.list->SelectedCount() == 1);
    CHECK(rig.list->DropIndex() == ListBox::kNone);
}

TEST_CASE("a list box drop at the ends, on itself and of several items", "[ui][drag][list]")
{
    ListRig rig;
    rig.h.Click(30, rig.RowY(2.5f));  // C
    rig.h.PointerDown(30, rig.RowY(2.5f));
    rig.h.PointerMove(30, rig.RowY(1.5f));
    rig.h.PointerMove(30, 11);  // above the first row
    CHECK(rig.list->DropIndex() == 0);
    rig.h.PointerUp(30, 11);
    CHECK(rig.items.Joined().View() == StringView("CABDEF"));
    // On itself: dropped back where it is.
    rig.h.PointerDown(30, rig.RowY(0.5f));
    rig.h.PointerMove(30, rig.RowY(0.5f) + 8);
    rig.h.PointerUp(30, rig.RowY(0.5f) + 8);
    CHECK(rig.items.Joined().View() == StringView("CABDEF"));
    // Two items, to the end: select the first two rows (C and A), then drag the pair below the last row.
    rig.h.Click(30, rig.RowY(0.5f));
    Event down;
    down.type = EventType::PointerDown;
    down.position = Point(30, rig.RowY(1.5f));
    down.button = 1;
    down.modifiers = os::kModShift;
    rig.h.GetSurface().Post(down);
    rig.h.Advance(0);
    Event up = down;
    up.type = EventType::PointerUp;
    rig.h.GetSurface().Post(up);
    rig.h.Advance(0);
    REQUIRE(rig.list->SelectedCount() == 2);
    rig.h.PointerDown(30, rig.RowY(0.5f));
    rig.h.PointerMove(30, rig.RowY(5.0f));
    rig.h.PointerMove(30, 205);  // past the last row
    CHECK(rig.list->DropIndex() == 6);
    rig.h.PointerUp(30, 205);
    CHECK(rig.items.Joined().View() == StringView("BDEFCA"));
    CHECK(rig.list->SelectedCount() == 2);
    CHECK(rig.list->IsSelected(4));
    CHECK(rig.list->IsSelected(5));
}

TEST_CASE("a model that refuses the move leaves the list as it was", "[ui][drag][list]")
{
    ListRig rig;
    rig.items.allow = false;
    rig.h.Click(30, rig.RowY(0.5f));
    rig.h.PointerDown(30, rig.RowY(0.5f));
    rig.h.PointerMove(30, rig.RowY(1.5f));
    rig.h.PointerMove(30, rig.RowY(4.0f) + 1);
    rig.h.PointerUp(30, rig.RowY(4.0f) + 1);
    CHECK(rig.items.Joined().View() == StringView("ABCDEF"));
}

TEST_CASE("dragging items out offers their texts and the list does not reorder when not asked to", "[ui][drag][list]")
{
    ListRig rig;
    rig.list->SetReorderable(false);
    rig.list->SetDragOut(true);
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("sink"));
    TextField* field = f.Get();
    f->SetBounds(DdRect(200, 20, 150, 28));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    rig.h.Advance(0);
    rig.h.Click(30, rig.RowY(1.5f));
    rig.h.PointerDown(30, rig.RowY(1.5f));
    rig.h.PointerMove(30, rig.RowY(2.5f));
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.h.PointerMove(260, 34);
    rig.h.PointerUp(260, 34);
    CHECK(field->Text() == StringView("B"));
    CHECK(rig.items.Joined().View() == StringView("ABCDEF"));
}

TEST_CASE("a list box scrolls while a drag rests near its edge, faster the deeper it is", "[ui][drag][list]")
{
    Harness h(400, 300, 1, StringView(UI_TEST_FONT_FILE));
    DdItems items(100);
    UniquePtr<ListBox> l = MakeUnique<ListBox>(StringView("list"));
    ListBox* list = l.Get();
    list->SetBounds(DdRect(10, 10, 150, 200));
    list->SetModel(&items);
    list->SetDragOut(true);
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(l.Release()));
    h.Advance(0);
    h.Advance(0);
    h.Click(30, 20);
    h.PointerDown(30, 20);
    h.PointerMove(30, 40);
    REQUIRE(h.GetSurface().Dragging());
    h.PointerMove(30, 200);  // 10 units from the bottom edge: inside the band
    const f32 start = list->Offset();
    h.Advance(0.1f);
    const f32 shallow = list->Offset() - start;
    CHECK(shallow > 0);
    h.PointerMove(30, 209);  // deeper
    const f32 mid = list->Offset();
    h.Advance(0.1f);
    const f32 deep = list->Offset() - mid;
    CHECK(deep > shallow);
    h.PointerMove(30, 100);  // the middle: nothing
    const f32 rest = list->Offset();
    h.Advance(0.5f);
    CHECK(list->Offset() == rest);
    h.PointerMove(30, 11);
    h.Advance(0.2f);
    CHECK(list->Offset() < rest);  // the top edge scrolls back
    h.PointerUp(30, 11);
}

TEST_CASE("a scroll view's content is reached by a drag resting at its edge", "[ui][drag][scroll]")
{
    DdRig rig;
    UniquePtr<ScrollView> sv = MakeUnique<ScrollView>(StringView("sv"));
    ScrollView* view = sv.Get();
    sv->SetBounds(DdRect(10, 100, 150, 100));
    UniquePtr<Widget> tall = MakeUnique<Widget>(StringView("tall"));
    LayoutProps props;
    props.width = 120;
    props.height = 600;
    tall->SetProps(props);
    sv->SetContent(static_cast<UniquePtr<Widget>&&>(tall));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(sv.Release()));
    rig.h.Advance(0);
    rig.h.Advance(0);
    rig.Begin();
    rig.h.PointerMove(60, 195);
    const f32 before = view->Offset().y;
    rig.h.Advance(0.2f);
    CHECK(view->Offset().y > before);
    rig.h.PointerUp(60, 195);
}

TEST_CASE("a drag resting over a tab selects it after the hover delay", "[ui][drag][tab]")
{
    DdRig rig;
    UniquePtr<TabView> tv = MakeUnique<TabView>(StringView("tabs"));
    TabView* tabs = tv.Get();
    tv->SetBounds(DdRect(10, 100, 180, 120));
    tv->AddTab(StringView("One"), MakeUnique<Widget>(StringView("p1")));
    tv->AddTab(StringView("Two"), MakeUnique<Widget>(StringView("p2")));
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(tv.Release()));
    rig.h.Advance(0);
    rig.h.Advance(0);
    struct Count
    {
        int n = 0;
    } count;
    tabs->OnSelectionChanged().Connect([](void* u, TabView*) { ++static_cast<Count*>(u)->n; }, &count);
    rig.Begin();
    const Rect second = tabs->TabRect(1);
    rig.h.PointerMove(second.x + 5, second.y + 5);
    rig.h.Advance(0.3f);
    CHECK(tabs->Selected() == 0);  // not yet
    rig.h.Advance(0.5f);
    CHECK(tabs->Selected() == 1);
    CHECK(count.n == 1);
    rig.h.PointerUp(second.x + 5, second.y + 5);
}

TEST_CASE("tabs reorder by dragging a header", "[ui][drag][tab]")
{
    Harness h(400, 300, 1, StringView(UI_TEST_FONT_FILE));
    UniquePtr<TabView> tv = MakeUnique<TabView>(StringView("tabs"));
    TabView* tabs = tv.Get();
    tv->SetBounds(DdRect(10, 10, 300, 120));
    tv->SetReorderable(true);
    tv->AddTab(StringView("One"), MakeUnique<Widget>(StringView("p1")));
    tv->AddTab(StringView("Two"), MakeUnique<Widget>(StringView("p2")));
    tv->AddTab(StringView("Three"), MakeUnique<Widget>(StringView("p3")));
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(tv.Release()));
    h.Advance(0);
    h.Advance(0);
    struct Moved
    {
        usize from = 99;
        usize to = 99;
    } moved;
    tabs->OnTabMoved().Connect(
        [](void* u, TabView*, usize from, usize to)
        {
            static_cast<Moved*>(u)->from = from;
            static_cast<Moved*>(u)->to = to;
        },
        &moved);
    Widget* page_one = tabs->Page(0);
    const Rect first = tabs->TabRect(0);
    const Rect third = tabs->TabRect(2);
    h.PointerDown(first.x + 5, first.y + 5);
    h.PointerMove(first.x + 20, first.y + 6);
    REQUIRE(h.GetSurface().Dragging());
    h.PointerMove(third.x + third.w - 2, third.y + 5);
    CHECK(tabs->DropIndex() == 3);
    h.PointerUp(third.x + third.w - 2, third.y + 5);
    CHECK(moved.from == 0);
    CHECK(moved.to == 2);
    CHECK(tabs->Page(2) == page_one);
    CHECK(tabs->Selected() == 2);  // the selected tab (One, selected by the press) moved with it
    CHECK(tabs->DropIndex() == TabView::kNone);
}

// ---- text fields
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

struct FieldRig
{
    FieldRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        a = Add("a", 10, 10);
        b = Add("b", 10, 60);
        h.Advance(0);
        h.Advance(0);
    }
    TextField* Add(const char* name, f32 x, f32 y)
    {
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView(name));
        TextField* raw = f.Get();
        f->SetBounds(DdRect(x, y, 240, 28));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        return raw;
    }
    // The surface x of the start of the text, and of the end of "hello" (5 characters).
    f32 TextX() const { return a->Bounds().x + 12; }
    Harness h;
    TextField* a;
    TextField* b;
};

}  // namespace

TEST_CASE("a text field accepts a text drop at the pointer and the insert is one undo step", "[ui][drag][text]")
{
    FieldRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.h.GetSurface().SetFocus(rig.a);
    rig.b->SetText(StringView("ABC"));
    rig.b->SelectAll();
    rig.h.GetSurface().SetFocus(rig.b);
    rig.h.PointerDown(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14);  // inside the selection
    rig.h.PointerMove(rig.b->Bounds().x + 30, rig.b->Bounds().y + 20);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.h.PointerMove(rig.a->Bounds().x + 230, rig.a->Bounds().y + 14);
    CHECK(rig.h.GetSurface().DragTarget() == rig.a);
    rig.h.PointerUp(rig.a->Bounds().x + 230, rig.a->Bounds().y + 14);
    CHECK(rig.a->Text() == StringView("hello worldABC"));
    CHECK(rig.a->HasSelection());
    CHECK(rig.b->Text() == StringView(""));  // moved: the source text is gone
    REQUIRE(rig.a->CanUndo());
    rig.a->Undo();
    CHECK(rig.a->Text() == StringView("hello world"));
}

TEST_CASE("dragging a selection within a field moves it, and with Ctrl copies it", "[ui][drag][text]")
{
    FieldRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.GetSurface().SetFocus(rig.a);
    const f32 y = rig.a->Bounds().y + 14;
    rig.h.PointerDown(rig.TextX() + 6, y);
    rig.h.PointerMove(rig.TextX() + 20, y);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.h.PointerMove(rig.a->Bounds().x + 230, y);
    rig.h.PointerUp(rig.a->Bounds().x + 230, y);
    CHECK(rig.a->Text() == StringView(" worldhello"));
    // Copy: with Ctrl held at the drop.
    rig.h.Advance(1.0f);  // past the double-click time
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.PointerDown(rig.TextX() + 6, y);
    rig.h.PointerMove(rig.TextX() + 20, y);
    REQUIRE(rig.h.GetSurface().Dragging());
    Event move;
    move.type = EventType::PointerMove;
    move.position = Point(rig.a->Bounds().x + 230, y);
    move.modifiers = os::kModCtrl;
    rig.h.GetSurface().Post(move);
    rig.h.Advance(0);
    Event up;
    up.type = EventType::PointerUp;
    up.position = move.position;
    up.button = 1;
    up.modifiers = os::kModCtrl;
    rig.h.GetSurface().Post(up);
    rig.h.Advance(0);
    CHECK(rig.a->Text() == StringView("hello worldhello"));
}

TEST_CASE("a drop onto the dragged selection itself does nothing, and a click on a selection places the caret",
          "[ui][drag][text]")
{
    FieldRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.GetSurface().SetFocus(rig.a);
    const f32 y = rig.a->Bounds().y + 14;
    rig.h.PointerDown(rig.TextX() + 6, y);
    rig.h.PointerMove(rig.TextX() + 20, y);
    REQUIRE(rig.h.GetSurface().Dragging());
    CHECK(rig.h.GetSurface().DragTarget() == nullptr);  // over the dragged text
    rig.h.PointerUp(rig.TextX() + 20, y);
    CHECK(rig.a->Text() == StringView("hello world"));
    // A plain click inside the selection collapses it to a caret.
    rig.h.Advance(1.0f);
    rig.a->SetSelection(0, 5);
    rig.h.Click(rig.TextX() + 6, y);
    CHECK_FALSE(rig.a->HasSelection());
}

TEST_CASE("a read-only or password field refuses a drop and a password field cannot be dragged out", "[ui][drag][text]")
{
    FieldRig rig;
    rig.a->SetText(StringView("hello"));
    rig.a->SetReadOnly(true);
    rig.b->SetText(StringView("secret"));
    rig.b->SelectAll();
    rig.b->SetPassword(true);
    rig.h.GetSurface().SetFocus(rig.b);
    rig.h.PointerDown(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14);
    rig.h.PointerMove(rig.b->Bounds().x + 60, rig.b->Bounds().y + 14);
    CHECK_FALSE(rig.h.GetSurface().Dragging());  // a press in a password field selects, it never drags
    rig.h.PointerUp(rig.b->Bounds().x + 60, rig.b->Bounds().y + 14);
    CHECK(rig.a->Text() == StringView("hello"));
    DdRig other;
    other.tgt->accepts = true;
    other.Begin();
    other.h.PointerMove(250, 130);
    other.h.PointerUp(250, 130);
    CHECK(other.tgt->drops == 1);
}

TEST_CASE("losing the window focus or opening a modal dialog ends a drag held here", "[ui][drag]")
{
    DdRig rig;
    rig.Begin();
    REQUIRE(rig.S().Dragging());
    Event lost;
    lost.type = EventType::WindowFocusLost;
    rig.S().Post(lost);
    rig.h.Advance(0);
    CHECK_FALSE(rig.S().Dragging());
    CHECK(rig.src->ended_with == DragAction::Nothing);
    DdRig other;
    other.Begin();
    REQUIRE(other.S().Dragging());
    other.S().ShowModal(MakeUnique<Widget>(StringView("dialog")));
    CHECK_FALSE(other.S().Dragging());
    CHECK(other.src->ended == 1);
}

TEST_CASE("a move never removes text that is no longer where the drag took it from", "[ui][drag][text]")
{
    FieldRig rig;
    UniquePtr<DdTarget> t = MakeUnique<DdTarget>(StringView("sink"));
    t->SetBounds(DdRect(300, 150, 80, 80));
    DdTarget* sink = t.Get();
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(t.Release()));
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.GetSurface().SetFocus(rig.a);
    const f32 y = rig.a->Bounds().y + 14;
    rig.h.PointerDown(rig.TextX() + 6, y);
    rig.h.PointerMove(rig.TextX() + 20, y);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.a->SetText(StringView("changed!!!!"));
    rig.h.PointerMove(340, 190);
    rig.h.PointerUp(340, 190);
    CHECK(sink->drops == 1);
    CHECK(rig.a->Text() == StringView("changed!!!!"));  // the moved text was not there any more: nothing is removed
}

TEST_CASE("a drop onto the old place of a dragged selection is refused even if the selection changed",
          "[ui][drag][text]")
{
    FieldRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.GetSurface().SetFocus(rig.a);
    const f32 y = rig.a->Bounds().y + 14;
    rig.h.PointerDown(rig.TextX() + 6, y);
    rig.h.PointerMove(rig.TextX() + 20, y);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.a->SetSelection(8, 11);
    rig.h.PointerMove(rig.TextX() + 10, y);  // inside the text that was taken
    CHECK(rig.h.GetSurface().DragTarget() == nullptr);
    rig.h.PointerUp(rig.TextX() + 10, y);
    CHECK(rig.a->Text() == StringView("hello world"));
}
