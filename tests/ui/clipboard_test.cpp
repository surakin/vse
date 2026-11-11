#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "scripted_window.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/list_box.h"
#include "ui/widgets/menu.h"
#include "ui/widgets/text_field.h"
#include "ui_window/os_clipboard.h"
#include "ui_window/window_adapter.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

Rect CbRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

struct OsRig
{
    OsRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE)), clip(&window)
    {
        h.GetSurface().SetClipboard(&clip);
        adapter.SetClipboard(&clip);
        clip.OnFailed().Connect(
            [](void* u, OsClipboard*, os::ClipboardStatus s)
            {
                static_cast<OsRig*>(u)->failure = s;
                ++static_cast<OsRig*>(u)->failures;
            },
            this);
        a = AddField("a", 10, 10);
        b = AddField("b", 10, 60);
        h.Advance(0);
        h.Advance(0);
        h.GetSurface().SetFocus(a);
    }
    TextField* AddField(const char* name, f32 x, f32 y)
    {
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView(name));
        TextField* raw = f.Get();
        f->SetBounds(CbRect(x, y, 240, 28));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        return raw;
    }
    void Pump()
    {
        adapter.PumpInto(window, h.GetSurface());
        h.Advance(0);
    }
    void Event(os::InputEventType type, f64 x, f64 y, u8 formats = 0, u8 actions = 0, u8 action = 0)
    {
        os::InputEvent e;
        e.type = type;
        e.x = x;
        e.y = y;
        e.drag_formats = formats;
        e.drag_actions = actions;
        e.drag_action = action;
        window.events.Push(e);
    }
    Harness h;
    ScriptedWindow window;
    OsClipboard clip;
    WindowAdapter adapter;
    TextField* a;
    TextField* b;
    os::ClipboardStatus failure = os::ClipboardStatus::Ok;
    int failures = 0;
};

}  // namespace

TEST_CASE("paste from the OS clipboard shows nothing until the data arrives, then inserts as one undo step",
          "[ui][clipboard]")
{
    OsRig rig;
    rig.window.clipboard_has_text = true;
    rig.h.Key(Key::V, true, os::kModCtrl);
    REQUIRE(rig.window.requests.Size() == 1);
    CHECK(rig.window.requests[0].kind == os::ClipboardKind::Clipboard);
    CHECK(rig.a->Text() == StringView(""));
    CHECK(rig.clip.PendingCount() == 1);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView("pasted"));
    CHECK(rig.a->Text() == StringView(""));  // the event is not pumped yet
    rig.Pump();
    CHECK(rig.a->Text() == StringView("pasted"));
    CHECK(rig.clip.PendingCount() == 0);
    rig.a->Undo();
    CHECK(rig.a->Text() == StringView(""));
}

TEST_CASE("a paste that times out or finds nothing inserts nothing and reports the failure", "[ui][clipboard]")
{
    OsRig rig;
    rig.window.clipboard_has_text = true;
    rig.h.Key(Key::V, true, os::kModCtrl);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Timeout, StringView());
    rig.Pump();
    CHECK(rig.a->Text() == StringView(""));
    CHECK(rig.failures == 1);
    CHECK(rig.failure == os::ClipboardStatus::Timeout);
    rig.window.refuse_requests = true;
    rig.h.Key(Key::V, true, os::kModCtrl);
    CHECK(rig.failures == 2);
    CHECK(rig.failure == os::ClipboardStatus::Failed);
    CHECK(rig.clip.PendingCount() == 0);
}

TEST_CASE("a field destroyed while its paste is pending is not called when the data arrives", "[ui][clipboard]")
{
    OsRig rig;
    rig.window.clipboard_has_text = true;
    rig.h.Key(Key::V, true, os::kModCtrl);
    REQUIRE(rig.window.requests.Size() == 1);
    const u32 id = rig.window.requests[0].id;
    rig.a->RemoveFromParent();
    rig.h.Advance(0);
    CHECK(rig.clip.PendingCount() == 0);
    rig.window.Finish(id, os::ClipboardStatus::Ok, StringView("late"));
    rig.Pump();  // the result is taken and dropped
    CHECK(rig.b->Text() == StringView(""));
}

TEST_CASE("copy and cut offer the text to the OS; Paste enables from the window's record of the selection",
          "[ui][clipboard]")
{
    OsRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.Key(Key::X, true, os::kModCtrl);
    REQUIRE(rig.window.offered.Size() == 1);
    CHECK(rig.window.offered[0].kind == os::ClipboardKind::Clipboard);
    CHECK(rig.window.offered[0].bytes.Size() == 5);
    CHECK(rig.a->Text() == StringView(" world"));
    CHECK_FALSE(rig.a->CanEdit(Widget::EditCommand::Paste));
    rig.window.clipboard_has_text = true;
    os::InputEvent changed;
    changed.type = os::InputEventType::ClipboardChanged;
    rig.window.events.Push(changed);
    rig.Pump();
    CHECK(rig.adapter.ClipboardChanges() == 1);
    CHECK(rig.a->CanEdit(Widget::EditCommand::Paste));
    rig.a->SetReadOnly(true);
    CHECK_FALSE(rig.a->CanEdit(Widget::EditCommand::Paste));
}

TEST_CASE("selecting text fills the primary selection and a middle click pastes it at the pointer", "[ui][clipboard]")
{
    OsRig rig;
    rig.window.primary_supported = true;
    rig.a->SetText(StringView("hello world"));
    rig.h.Click(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    rig.h.Click(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);  // a double click selects a word
    REQUIRE(rig.a->HasSelection());
    bool primary = false;
    for (usize i = 0; i < rig.window.offered.Size(); ++i)
    {
        primary = primary || rig.window.offered[i].kind == os::ClipboardKind::Primary;
    }
    CHECK(primary);
    // Middle click in the second field.
    rig.h.PointerDown(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14, os::kMouseMiddle);
    rig.h.PointerUp(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14, os::kMouseMiddle);
    REQUIRE(rig.window.requests.Size() == 1);
    CHECK(rig.window.requests[0].kind == os::ClipboardKind::Primary);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView("hello"));
    rig.Pump();
    CHECK(rig.b->Text() == StringView("hello"));
}

TEST_CASE("without a primary selection nothing is offered and a middle click does nothing", "[ui][clipboard]")
{
    OsRig rig;  // the scripted window has no primary selection
    rig.a->SetText(StringView("hello world"));
    rig.h.Click(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    rig.h.Click(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    CHECK(rig.window.offered.Size() == 0);
    rig.h.PointerDown(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14, os::kMouseMiddle);
    rig.h.PointerUp(rig.b->Bounds().x + 14, rig.b->Bounds().y + 14, os::kMouseMiddle);
    CHECK(rig.window.requests.Size() == 0);
}

TEST_CASE("the in-process clipboard serves offscreen surfaces, with the primary selection when turned on",
          "[ui][clipboard]")
{
    Harness h(300, 100, 1, StringView(UI_TEST_FONT_FILE));
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("f"));
    TextField* field = f.Get();
    f->SetBounds(CbRect(10, 10, 200, 28));
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    h.Advance(0);
    h.GetSurface().SetFocus(field);
    field->SetText(StringView("hello world"));
    h.Click(field->Bounds().x + 14, field->Bounds().y + 14);
    h.Click(field->Bounds().x + 14, field->Bounds().y + 14);
    LocalClipboard& local = *static_cast<LocalClipboard*>(&h.GetSurface().GetClipboard());
    CHECK(local.PrimaryText().Length() == 0);  // off by default
    local.SetPrimarySupported(true);
    h.Click(field->Bounds().x + 14, field->Bounds().y + 14);
    h.Click(field->Bounds().x + 14, field->Bounds().y + 14);
    CHECK(local.PrimaryText().Length() > 0);
    field->SetText(StringView(""));
    h.PointerDown(field->Bounds().x + 14, field->Bounds().y + 14, os::kMouseMiddle);
    h.PointerUp(field->Bounds().x + 14, field->Bounds().y + 14, os::kMouseMiddle);
    CHECK(field->Text().Length() > 0);
}

TEST_CASE("the Edit menu items enable from the focus owner and run on it", "[ui][clipboard][menu]")
{
    OsRig rig;
    rig.window.clipboard_has_text = true;
    Menu menu;
    const usize first = AddEditItems(menu, &rig.h.GetSurface());
    rig.a->SetText(StringView("hello"));
    rig.a->SetSelection(0, 5);
    RefreshEditItems(menu, first, rig.h.GetSurface());
    CHECK(menu.Item(first).enabled);      // cut
    CHECK(menu.Item(first + 1).enabled);  // copy
    CHECK(menu.Item(first + 2).enabled);  // paste
    CHECK(menu.Item(first + 4).enabled);  // select all
    rig.a->SetPassword(true);
    RefreshEditItems(menu, first, rig.h.GetSurface());
    CHECK_FALSE(menu.Item(first).enabled);
    CHECK_FALSE(menu.Item(first + 1).enabled);
    CHECK(menu.Item(first + 2).enabled);
    rig.a->SetPassword(false);
    rig.h.GetSurface().SetFocus(nullptr);
    RefreshEditItems(menu, first, rig.h.GetSurface());
    for (usize i = 0; i < 3; ++i)
    {
        CHECK_FALSE(menu.Item(first + i).enabled);
    }
    CHECK_FALSE(menu.Item(first + 4).enabled);
    rig.h.GetSurface().SetFocus(rig.a);
    RefreshEditItems(menu, first, rig.h.GetSurface());
    CHECK(menu.Choose(first + 1));  // copy
    REQUIRE(rig.window.offered.Size() == 1);
    CHECK(rig.window.offered[0].bytes.Size() == 5);
}

TEST_CASE("a drag from another program drops text into a field only once its data is read", "[ui][drag][os]")
{
    OsRig rig;
    rig.adapter.EnableDrops(rig.window);
    CHECK((rig.window.accept_formats & os::kDropText) != 0);
    CHECK((rig.window.accept_formats & os::kDropFiles) != 0);
    const f64 x = rig.b->Bounds().x + 100;
    const f64 y = rig.b->Bounds().y + 14;
    rig.Event(os::InputEventType::DragEnter, x, y, os::kDropText, os::kAllDragActions);
    rig.Event(os::InputEventType::DragOver, x + 2, y, os::kDropText, os::kAllDragActions);
    rig.Pump();
    CHECK(rig.h.GetSurface().DragIsExternal());
    CHECK(rig.h.GetSurface().DragTarget() == rig.b);
    CHECK(rig.window.last_drop_action == os::DragAction::Move);
    rig.Event(os::InputEventType::Drop, x + 2, y, os::kDropText, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    REQUIRE(rig.window.requests.Size() == 1);
    CHECK(rig.window.requests[0].drop);
    CHECK(rig.b->Text() == StringView(""));  // the data has not arrived
    CHECK(rig.window.finished_drops == 0);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView("dropped"));
    rig.Pump();
    CHECK(rig.b->Text() == StringView("dropped"));
    CHECK(rig.window.finished_drops == 1);
    CHECK(rig.window.finished_with == os::DragAction::Copy);  // the source asked for a copy
    CHECK_FALSE(rig.h.GetSurface().Dragging());
}

TEST_CASE("a drop whose data cannot be read reports a failure and drops nothing", "[ui][drag][os]")
{
    OsRig rig;
    rig.adapter.EnableDrops(rig.window);
    struct Msg
    {
        int n = 0;
    } msg;
    rig.h.GetSurface().OnDropFailed().Connect([](void* u, Surface*, StringView) { ++static_cast<Msg*>(u)->n; }, &msg);
    const f64 x = rig.b->Bounds().x + 100;
    const f64 y = rig.b->Bounds().y + 14;
    rig.Event(os::InputEventType::DragEnter, x, y, os::kDropText, os::kAllDragActions);
    rig.Event(os::InputEventType::Drop, x, y, os::kDropText, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    REQUIRE(rig.window.requests.Size() == 1);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Timeout, StringView());
    rig.Pump();
    CHECK(msg.n == 1);
    CHECK(rig.b->Text() == StringView(""));
    CHECK(rig.window.finished_drops == 1);
    CHECK(rig.window.finished_with == os::DragAction::Nothing);
    CHECK_FALSE(rig.h.GetSurface().Dragging());
    // A request the window refuses fails the same way.
    rig.window.refuse_requests = true;
    rig.Event(os::InputEventType::DragEnter, x, y, os::kDropText, os::kAllDragActions);
    rig.Event(os::InputEventType::Drop, x, y, os::kDropText, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    CHECK(msg.n == 2);
    CHECK(rig.b->Text() == StringView(""));
}

TEST_CASE("dropped text is made valid UTF-8, and a drag that leaves is dropped", "[ui][drag][os]")
{
    OsRig rig;
    rig.adapter.EnableDrops(rig.window);
    const f64 x = rig.b->Bounds().x + 100;
    const f64 y = rig.b->Bounds().y + 14;
    rig.Event(os::InputEventType::DragEnter, x, y, os::kDropText, os::kAllDragActions);
    rig.Event(os::InputEventType::Drop, x, y, os::kDropText, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    const char bad[] = {'a', '\xFF',
                        'b'};  // an invalid UTF-8 byte (a literal: a cast of 0xFF to char truncates, C4310 on MSVC)
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView(bad, 3));
    rig.Pump();
    CHECK(rig.b->Text().Length() == 5);  // a, U+FFFD (three bytes), b
    rig.Event(os::InputEventType::DragEnter, x, y, os::kDropText, os::kAllDragActions);
    rig.Pump();
    CHECK(rig.h.GetSurface().DragTarget() == rig.b);
    rig.Event(os::InputEventType::DragLeave, x, y);
    rig.Pump();
    CHECK_FALSE(rig.h.GetSurface().Dragging());
    CHECK(rig.b->State() == (rig.b->State() & ~kStateDragOver));
}

TEST_CASE("files dropped from another program reach the root's handler", "[ui][drag][os]")
{
    OsRig rig;
    rig.adapter.EnableDrops(rig.window);
    struct Got
    {
        int n = 0;
        String<HeapAllocator> paths{HeapAllocator::Default(), StringView()};
    } got;
    rig.h.GetSurface().OnFilesDropped().Connect(
        [](void* u, Surface*, StringView p)
        {
            ++static_cast<Got*>(u)->n;
            static_cast<Got*>(u)->paths = String<HeapAllocator>(HeapAllocator::Default(), p);
        },
        &got);
    rig.Event(os::InputEventType::DragEnter, 300, 250, os::kDropFiles, os::kAllDragActions);
    rig.Event(os::InputEventType::Drop, 300, 250, os::kDropFiles, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    REQUIRE(rig.window.requests.Size() == 1);
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView("/x/y\n/z"));
    rig.Pump();
    CHECK(got.n == 1);
    CHECK(got.paths.View() == StringView("/x/y\n/z"));
    CHECK(rig.window.finished_with == os::DragAction::Copy);
}

TEST_CASE("a drag nobody accepts finishes the OS drop with nothing", "[ui][drag][os]")
{
    OsRig rig;
    rig.adapter.EnableDrops(rig.window);
    rig.Event(os::InputEventType::DragEnter, 300, 250, os::kDropText, os::kAllDragActions);
    rig.Pump();
    CHECK(rig.window.last_drop_action == os::DragAction::Nothing);
    rig.Event(os::InputEventType::Drop, 300, 250, os::kDropText, os::kAllDragActions,
              static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    rig.window.Finish(rig.window.requests[0].id, os::ClipboardStatus::Ok, StringView("x"));
    rig.Pump();
    CHECK(rig.window.finished_with == os::DragAction::Nothing);
}

TEST_CASE("a drag that leaves the window becomes an OS drag with its text, files and custom entries", "[ui][drag][os]")
{
    OsRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.PointerDown(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    rig.h.PointerMove(rig.a->Bounds().x + 30, rig.a->Bounds().y + 14);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.Event(os::InputEventType::PointerLeave, 0, 0);
    rig.Pump();
    CHECK(rig.window.drags_begun == 1);
    REQUIRE(rig.window.offered_drag.Size() == 1);
    CHECK(rig.window.offered_drag[0].format == os::ClipboardFormat::Text);
    CHECK(rig.window.offered_drag[0].bytes.Size() == 5);  // the selected text only: the pointer entry stays home
    CHECK((rig.window.drag_allowed & os::DragActionBit(os::DragAction::Move)) != 0);
    CHECK(rig.h.GetSurface().DragHandedOff());
    CHECK(rig.h.GetSurface().DragTarget() == nullptr);
    rig.Event(os::InputEventType::DragEnded, 0, 0, 0, 0, static_cast<u8>(os::DragAction::Move));
    rig.Pump();
    CHECK_FALSE(rig.h.GetSurface().Dragging());
    CHECK(rig.a->Text() == StringView(" world"));  // the OS target took it with a move: the source removes it
}

TEST_CASE("a cancelled OS drag leaves the source as it was; a refusing window keeps the drag here", "[ui][drag][os]")
{
    OsRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.PointerDown(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    rig.h.PointerMove(rig.a->Bounds().x + 30, rig.a->Bounds().y + 14);
    rig.window.accept_drag = false;
    rig.Event(os::InputEventType::PointerLeave, 0, 0);
    rig.Pump();
    CHECK(rig.window.drags_begun == 1);
    CHECK_FALSE(rig.h.GetSurface().DragHandedOff());
    rig.window.accept_drag = true;
    rig.Event(os::InputEventType::PointerLeave, 0, 0);
    rig.Pump();
    CHECK(rig.h.GetSurface().DragHandedOff());
    rig.Event(os::InputEventType::DragEnded, 0, 0, 0, 0, static_cast<u8>(os::DragAction::Nothing));
    rig.Pump();
    CHECK_FALSE(rig.h.GetSurface().Dragging());
    CHECK(rig.a->Text() == StringView("hello world"));
}

TEST_CASE("a modal OS drag (Win32) is handed off when it starts, not when BeginDrag returns", "[ui][drag][os]")
{
    OsRig rig;
    rig.a->SetText(StringView("hello world"));
    rig.a->SetSelection(0, 5);
    rig.h.PointerDown(rig.a->Bounds().x + 14, rig.a->Bounds().y + 14);
    rig.h.PointerMove(rig.a->Bounds().x + 30, rig.a->Bounds().y + 14);
    REQUIRE(rig.h.GetSurface().Dragging());
    struct Seen
    {
        Surface* surface = nullptr;
        bool handed_off = false;
        bool target_left = false;
    } seen;
    seen.surface = &rig.h.GetSurface();
    rig.window.modal_drag = true;
    rig.window.during_drag = [](void* u)
    {
        Seen* s = static_cast<Seen*>(u);
        s->handed_off = s->surface->DragHandedOff();
        s->target_left = s->surface->DragTarget() == nullptr;
    };
    rig.window.during_drag_user = &seen;
    rig.Event(os::InputEventType::PointerLeave, 0, 0);
    rig.Pump();
    CHECK(rig.window.drags_begun == 1);
    CHECK(seen.handed_off);  // while the program draws during the drag, the drag is no longer the surface's own
    CHECK(seen.target_left);
    CHECK(rig.h.GetSurface().DragHandedOff());
    rig.Event(os::InputEventType::DragEnded, 0, 0, 0, 0, static_cast<u8>(os::DragAction::Copy));
    rig.Pump();
    CHECK_FALSE(rig.h.GetSurface().Dragging());
    CHECK(rig.a->Text() == StringView("hello world"));  // a copy leaves the source as it was
}

TEST_CASE("a drag of pointers only never leaves the process", "[ui][drag][os]")
{
    OsRig rig;
    struct Items : ListModel
    {
        usize ItemCount() const override { return 3; }
        StringView ItemText(usize) override { return StringView("row"); }
        bool MoveItems(const usize*, usize, usize) override { return true; }
    } items;
    UniquePtr<ListBox> l = MakeUnique<ListBox>(StringView("list"));
    ListBox* list = l.Get();
    l->SetBounds(CbRect(10, 110, 150, 100));
    l->SetModel(&items);
    l->SetReorderable(true);
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(l.Release()));
    rig.h.Advance(0);
    rig.h.Advance(0);
    rig.h.Click(30, 110 + list->RowHeight() * 0.5f);
    rig.h.PointerDown(30, 110 + list->RowHeight() * 0.5f);
    rig.h.PointerMove(30, 110 + list->RowHeight() * 1.5f);
    REQUIRE(rig.h.GetSurface().Dragging());
    rig.Event(os::InputEventType::PointerLeave, 0, 0);
    rig.Pump();
    CHECK(rig.window.drags_begun == 0);
    CHECK_FALSE(rig.h.GetSurface().DragHandedOff());
    rig.h.PointerUp(30, 110 + list->RowHeight() * 1.5f);
}

TEST_CASE("a payload is capped, so a hostile source cannot hand over more than a clipboard could", "[ui][drag]")
{
    DragPayload p;
    Vector<char> huge;
    huge.Resize(DragPayload::kMaxEntryBytes + 1);
    CHECK_FALSE(p.AddText(StringView(huge.Data(), huge.Size())));
    CHECK(p.EntryCount() == 0);
    CHECK(p.AddText(StringView(huge.Data(), 1000)));
}
