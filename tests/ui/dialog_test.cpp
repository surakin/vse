#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/message_box.h"
#include "ui/widgets/text_field.h"

using namespace vse;
using namespace vse::ui;

namespace
{

Rect DlRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

class DlPane : public Widget
{
public:
    DlPane() : Widget(StringView("pane")) { SetFocusable(true); }
    Size Measure(const Constraints&) override { return Size{60, 40}; }
};

struct DlRig
{
    DlRig() : h(400, 300, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<Button> b = MakeUnique<Button>(StringView("behind"), StringView("behind"));
        behind = b.Get();
        behind->SetBounds(DlRect(10, 10, 100, 30));
        behind->OnClick().Connect(&DlRig::Clicked, this);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(b.Release()));
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
        field = f.Get();
        field->SetBounds(DlRect(10, 250, 100, 30));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        h.Advance(0);
        h.GetSurface().SetFocus(field);
    }
    static void Clicked(void* user, Button*) { ++static_cast<DlRig*>(user)->clicks; }
    static void Closed(void* user, Dialog*, int result)
    {
        DlRig* r = static_cast<DlRig*>(user);
        r->closed_with = result;
        ++r->closes;
    }
    static void CloseAgain(void*, Dialog* dialog, int) { dialog->Close(dialog_result::kNo); }
    // A dialog with a text field and OK / Cancel, shown modally.
    Dialog* Show(StringView title = StringView("Title"), bool field_content = true)
    {
        UniquePtr<Dialog> d = MakeUnique<Dialog>(StringView("dlg"), title);
        if (field_content)
        {
            d->SetContent(UniquePtr<Widget>(MakeUnique<TextField>(StringView("name")).Release()));
        }
        else
        {
            d->SetContent(UniquePtr<Widget>(MakeUnique<DlPane>().Release()));
        }
        d->AddButton(StringView("OK"), dialog_result::kOk, true);
        d->AddButton(StringView("Cancel"), dialog_result::kCancel);
        d->OnClosed().Connect(&DlRig::Closed, this);
        Dialog* raw = d.Get();
        h.GetSurface().ShowModal(UniquePtr<Widget>(d.Release()));
        h.Advance(0);
        return raw;
    }
    bool Inside(const Widget* w, const Widget* root) const
    {
        for (; w != nullptr; w = w->Parent())
        {
            if (w == root)
            {
                return true;
            }
        }
        return false;
    }
    Harness h;
    Button* behind = nullptr;
    TextField* field = nullptr;
    int clicks = 0;
    int closes = 0;
    int closed_with = -1;
};

Point DlCenter(const Rect& r)
{
    return Point{r.x + r.w / 2, r.y + r.h / 2};
}

}  // namespace

TEST_CASE("a modal dialog is centered and takes the focus", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    Surface& s = rig.h.GetSurface();
    CHECK(s.ModalCount() == 1);
    CHECK(s.TopModal() == d);
    const Rect b = d->Bounds();
    CHECK(b.x + b.w / 2 == Catch::Approx(200).margin(1));
    CHECK(b.y + b.h / 2 == Catch::Approx(150).margin(1));
    CHECK(rig.Inside(s.Focus(), d));
}

TEST_CASE("a modal dialog blocks the hover and the clicks beneath it", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    Surface& s = rig.h.GetSurface();
    // Put the pointer over the button behind the dialog (outside the dialog's rectangle).
    const Rect db = d->Bounds();
    REQUIRE(!db.Contains(Point{30, 20}));
    rig.h.PointerMove(30, 20);
    CHECK(s.Hover() != rig.behind);
    rig.h.Click(30, 20);
    CHECK(rig.clicks == 0);
    CHECK(s.ModalCount() == 1);       // a click on the shade closes nothing
    CHECK(rig.Inside(s.Focus(), d));  // and keeps the focus in the dialog
    rig.h.Wheel(30, 20, 0, 1);
    CHECK(rig.clicks == 0);
}

TEST_CASE("the shade is drawn over what lies beneath", "[ui][dialog][modal]")
{
    DlRig rig;
    RasterImage before;
    rig.h.Render(&before, 0xFFFFFFFFu);
    rig.Show();
    RasterImage after;
    rig.h.Render(&after, 0xFFFFFFFFu);
    CHECK(before.Pixel(3, 3)[0] == 255);
    CHECK(after.Pixel(3, 3)[0] < 200);  // darker
    CHECK(after.Pixel(3, 3)[0] > 60);   // but not black: the shade is half transparent
}

TEST_CASE("Tab cycles inside the dialog only", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    Surface& s = rig.h.GetSurface();
    for (int i = 0; i < 8; ++i)
    {
        rig.h.Key(Key::Tab);
        CHECK(rig.Inside(s.Focus(), d));
        CHECK(s.Focus() != rig.behind);
        CHECK(s.Focus() != rig.field);
    }
    for (int i = 0; i < 4; ++i)
    {
        rig.h.Key(Key::Tab, true, os::kModShift);
        CHECK(rig.Inside(s.Focus(), d));
    }
}

TEST_CASE("Escape cancels and the focus returns to the previous owner", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    (void)d;
    rig.h.Key(Key::Escape);
    Surface& s = rig.h.GetSurface();
    CHECK(rig.closed_with == dialog_result::kCancel);
    CHECK(s.ModalCount() == 0);
    CHECK(s.Focus() == rig.field);
    // The shade is gone: the button works again.
    rig.h.Click(30, 20);
    CHECK(rig.clicks == 1);
}

TEST_CASE("Enter presses the default button", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show(StringView("T"), false);
    Surface& s = rig.h.GetSurface();
    s.SetFocus(d);  // not on a button
    REQUIRE(d->DefaultButton() != nullptr);
    rig.h.Key(Key::Enter);
    CHECK(rig.closed_with == dialog_result::kOk);
    CHECK(rig.closes == 1);
    CHECK(s.ModalCount() == 0);
}

TEST_CASE("a button closes the dialog with its result", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    Widget* cancel = d->FindByName(StringView("Cancel"));
    REQUIRE(cancel != nullptr);
    const Point p = DlCenter(cancel->Bounds());
    rig.h.Click(p.x, p.y);
    CHECK(rig.closed_with == dialog_result::kCancel);
    CHECK(rig.h.GetSurface().ModalCount() == 0);
}

TEST_CASE("the close button cancels", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    const Point p = DlCenter(d->CloseButtonRect());
    rig.h.Click(p.x, p.y);
    CHECK(rig.closed_with == dialog_result::kCancel);
}

TEST_CASE("a second close does nothing", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    d->OnClosed().Connect(&DlRig::CloseAgain,
                          &rig);  // closing again from inside the listener (the dialog is gone once Close returns)
    d->Close(dialog_result::kOk);
    CHECK(rig.closes == 1);
    CHECK(rig.closed_with == dialog_result::kOk);
}

TEST_CASE("dialogs over dialogs form a stack", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* a = rig.Show(StringView("A"));
    Dialog* b = rig.Show(StringView("B"));
    Surface& s = rig.h.GetSurface();
    CHECK(s.ModalCount() == 2);
    CHECK(s.TopModal() == b);
    CHECK(rig.Inside(s.Focus(), b));
    for (int i = 0; i < 6; ++i)
    {
        rig.h.Key(Key::Tab);
        CHECK(rig.Inside(s.Focus(), b));
    }
    // A click on A's area is blocked by B's shade.
    const Point pa = DlCenter(a->Bounds());
    rig.h.Click(pa.x - 5, a->Bounds().y + 2);
    CHECK(s.ModalCount() == 2);
    rig.h.Key(Key::Escape);
    CHECK(s.ModalCount() == 1);
    CHECK(s.TopModal() == a);
    CHECK(rig.Inside(s.Focus(), a));  // back where it was in A
    rig.h.Key(Key::Escape);
    CHECK(s.ModalCount() == 0);
    CHECK(s.Focus() == rig.field);
}

TEST_CASE("closing the lower dialog closes the ones above it", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* a = rig.Show(StringView("A"));
    rig.Show(StringView("B"));
    rig.h.GetSurface().CloseModal(a);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().ModalCount() == 0);
    CHECK(rig.h.GetSurface().Focus() == rig.field);
}

TEST_CASE("closing when the previous focus owner is gone does not crash", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    rig.h.GetSurface().Root()->RemoveChild(rig.field);  // the app removed the widget that had the focus
    rig.h.Advance(0);
    d->Close(dialog_result::kOk);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().ModalCount() == 0);
    CHECK(rig.h.GetSurface().Focus() == nullptr);
}

TEST_CASE("a dialog removed by the app takes its shade with it", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    rig.h.GetSurface().ModalLayer()->RemoveChild(d);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().ModalCount() == 0);
    CHECK(rig.h.GetSurface().ModalLayer()->ChildCount() == 0);
    rig.h.Click(30, 20);
    CHECK(rig.clicks == 1);
}

TEST_CASE("popups opened inside a dialog work, and Escape closes the popup first", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    Surface& s = rig.h.GetSurface();
    UniquePtr<DlPane> pane = MakeUnique<DlPane>();
    DlPane* raw = pane.Get();
    s.OpenPopup(UniquePtr<Widget>(pane.Release()), d->Bounds(), PopupOptions());
    rig.h.Advance(0);
    CHECK(s.PopupCount() == 1);
    CHECK(s.Focus() == raw);
    rig.h.Key(Key::Escape);
    CHECK(s.PopupCount() == 0);
    CHECK(s.ModalCount() == 1);
    CHECK(rig.Inside(s.Focus(), d));
}

TEST_CASE("opening a modal closes the open popups", "[ui][dialog][modal]")
{
    DlRig rig;
    Surface& s = rig.h.GetSurface();
    s.OpenPopup(UniquePtr<Widget>(MakeUnique<DlPane>().Release()), DlRect(10, 10, 10, 10), PopupOptions());
    rig.h.Advance(0);
    REQUIRE(s.PopupCount() == 1);
    rig.Show();
    CHECK(s.PopupCount() == 0);
}

TEST_CASE("accelerators and the menu keys do not run under a modal dialog", "[ui][dialog][modal]")
{
    DlRig rig;
    Surface& s = rig.h.GetSurface();
    static int fired = 0;
    fired = 0;
    KeyChord chord;
    chord.key = Key::S;
    chord.modifiers = os::kModCtrl;
    s.RegisterAccelerator(chord, [](void*) { ++fired; }, nullptr);
    Dialog* d = rig.Show(StringView("T"), false);
    s.SetFocus(d);
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(fired == 0);
    d->Close(dialog_result::kOk);
    rig.h.Advance(0);
    rig.h.Key(Key::S, true, os::kModCtrl);
    CHECK(fired == 1);
}

TEST_CASE("keys go to the dialog when the focus is outside it", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    (void)d;
    rig.h.GetSurface().SetFocus(rig.field);  // the program put the focus beneath the dialog
    rig.h.Key(Key::Escape);
    CHECK(rig.closes == 1);
}

TEST_CASE("a surface that shrinks keeps its dialogs inside", "[ui][dialog][modal]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    rig.h.GetSurface().Resize(250, 150, 1);
    rig.h.Advance(0);
    const Rect b = d->Bounds();
    CHECK(b.x >= 0);
    CHECK(b.y >= 0);
    CHECK(b.Right() <= 250.5f);
    CHECK(b.Bottom() <= 150.5f);
}

// ---- window frame ----

TEST_CASE("dragging the title bar moves the frame and keeps it inside the surface", "[ui][dialog][frame]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    const Rect start = d->Bounds();
    const Rect bar = d->TitleBarRect();
    const Point grab{bar.x + 20, bar.y + bar.h / 2};
    rig.h.PointerDown(grab.x, grab.y);
    rig.h.PointerMove(grab.x + 25, grab.y + 15);
    rig.h.PointerUp(grab.x + 25, grab.y + 15);
    CHECK(d->Bounds().x == start.x + 25);
    CHECK(d->Bounds().y == start.y + 15);
    CHECK(d->Bounds().w == start.w);
    // Far outside: clamped to the surface.
    const Rect bar2 = d->TitleBarRect();
    const Point g2{bar2.x + 20, bar2.y + bar2.h / 2};
    rig.h.PointerDown(g2.x, g2.y);
    rig.h.PointerMove(-500, -500);
    rig.h.PointerUp(-500, -500);
    CHECK(d->Bounds().x == 0);
    CHECK(d->Bounds().y == 0);
    const Rect bar3 = d->TitleBarRect();
    const Point g3{bar3.x + 20, bar3.y + bar3.h / 2};
    rig.h.PointerDown(g3.x, g3.y);
    rig.h.PointerMove(900, 900);
    rig.h.PointerUp(900, 900);
    CHECK(d->Bounds().Right() == 400);
    CHECK(d->Bounds().Bottom() == 300);
}

TEST_CASE("a frame that is not movable stays put", "[ui][dialog][frame]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    d->SetMovable(false);
    const Rect start = d->Bounds();
    const Rect bar = d->TitleBarRect();
    rig.h.PointerDown(bar.x + 20, bar.y + 5);
    rig.h.PointerMove(bar.x + 60, bar.y + 40);
    rig.h.PointerUp(bar.x + 60, bar.y + 40);
    CHECK(d->Bounds().x == start.x);
    CHECK(d->Bounds().y == start.y);
}

TEST_CASE("a resizable frame resizes from its edges down to a minimum", "[ui][dialog][frame]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    d->SetResizable(true);
    d->SetMinSize(Size{150, 120});
    const Rect start = d->Bounds();
    // Right edge.
    const Point edge{start.Right() - 2, start.y + start.h / 2};
    rig.h.PointerMove(edge.x, edge.y);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeHorizontal);
    rig.h.PointerDown(edge.x, edge.y);
    rig.h.PointerMove(edge.x + 30, edge.y);
    rig.h.PointerUp(edge.x + 30, edge.y);
    CHECK(d->Bounds().Right() == edge.x + 30);
    CHECK(d->Bounds().x == start.x);
    // Left edge, dragged far to the right: the minimum width stays.
    const Rect now = d->Bounds();
    const Point left{now.x + 2, now.y + now.h / 2};
    rig.h.PointerDown(left.x, left.y);
    rig.h.PointerMove(left.x + 600, left.y);
    rig.h.PointerUp(left.x + 600, left.y);
    CHECK(d->Bounds().w == 150);
    CHECK(d->Bounds().Right() == now.Right());
}

TEST_CASE("the corners show the diagonal cursors", "[ui][dialog][frame]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    d->SetResizable(true);
    const Rect b = d->Bounds();
    rig.h.PointerMove(b.Right() - 2, b.Bottom() - 2);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeDiagonalNWSE);
    rig.h.PointerMove(b.Right() - 2, b.y + 2);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeDiagonalNESW);
    rig.h.PointerMove(b.x + b.w / 2, b.Bottom() - 2);
    CHECK(rig.h.GetSurface().RequestedCursor() == os::CursorShape::ResizeVertical);
}

TEST_CASE("a frame that is not resizable has no resize edges", "[ui][dialog][frame]")
{
    DlRig rig;
    Dialog* d = rig.Show();
    const Rect start = d->Bounds();
    rig.h.PointerDown(start.Right() - 2, start.y + start.h / 2);
    rig.h.PointerMove(start.Right() + 30, start.y + start.h / 2);
    rig.h.PointerUp(start.Right() + 30, start.y + start.h / 2);
    CHECK(d->Bounds().w == start.w);
}

TEST_CASE("a frame outside a modal layer closes through its parent", "[ui][dialog][frame]")
{
    DlRig rig;
    UniquePtr<Dialog> d = MakeUnique<Dialog>(StringView("free"), StringView("Free"));
    d->SetContent(UniquePtr<Widget>(MakeUnique<DlPane>().Release()));
    d->AddButton(StringView("OK"), dialog_result::kOk);
    d->SetBounds(DlRect(20, 20, 200, 100));
    Dialog* raw = d.Get();
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(d.Release()));
    rig.h.Advance(0);
    const usize before = rig.h.GetSurface().Root()->ChildCount();
    raw->Close(dialog_result::kOk);
    rig.h.Advance(0);
    CHECK(rig.h.GetSurface().Root()->ChildCount() == before - 1);
}

// ---- message boxes ----

namespace
{
struct MbLog
{
    int calls = 0;
    int result = -1;
    static void Done(void* user, int result)
    {
        MbLog* l = static_cast<MbLog*>(user);
        ++l->calls;
        l->result = result;
    }
};
}  // namespace

TEST_CASE("a message box returns the button that was pressed", "[ui][dialog][messagebox]")
{
    DlRig rig;
    MbLog log;
    Dialog* d = ShowMessageBox(&rig.h.GetSurface(), StringView("Quit"),
                               StringView("Do you really want to quit the program and lose all of your unsaved work?"),
                               MessageButtons::YesNoCancel, &MbLog::Done, &log);
    rig.h.Advance(0);
    REQUIRE(rig.h.GetSurface().IsModalOpen(d));
    Widget* no = d->FindByName(StringView("No"));
    REQUIRE(no != nullptr);
    const Point p = DlCenter(no->Bounds());
    rig.h.Click(p.x, p.y);
    CHECK(log.calls == 1);
    CHECK(log.result == dialog_result::kNo);
    CHECK(rig.h.GetSurface().ModalCount() == 0);
}

TEST_CASE("a message box wraps its text and fits the surface", "[ui][dialog][messagebox]")
{
    DlRig rig;
    Dialog* d = ShowMessageBox(
        &rig.h.GetSurface(), StringView("Note"),
        StringView(
            "A long message that goes on and on and on and on and on and on and on and on and on and on and on."),
        MessageButtons::Ok, nullptr, nullptr);
    rig.h.Advance(0);
    const Rect b = d->Bounds();
    CHECK(b.w <= 400);
    CHECK(b.x >= 0);
    CHECK(b.Right() <= 400);
    CHECK(b.h > 100);  // more than one line
}

TEST_CASE("Enter confirms a message box, Escape gives the cancel result of its button set", "[ui][dialog][messagebox]")
{
    DlRig rig;
    MbLog log;
    ShowMessageBox(&rig.h.GetSurface(), StringView("T"), StringView("Sure?"), MessageButtons::YesNo, &MbLog::Done,
                   &log);
    rig.h.Advance(0);
    rig.h.Key(Key::Enter);
    CHECK(log.result == dialog_result::kYes);
    ShowMessageBox(&rig.h.GetSurface(), StringView("T"), StringView("Sure?"), MessageButtons::YesNo, &MbLog::Done,
                   &log);
    rig.h.Advance(0);
    rig.h.Key(Key::Escape);
    CHECK(log.result == dialog_result::kNo);
    ShowMessageBox(&rig.h.GetSurface(), StringView("T"), StringView("Hi"), MessageButtons::OkCancel, &MbLog::Done,
                   &log);
    rig.h.Advance(0);
    rig.h.Key(Key::Escape);
    CHECK(log.result == dialog_result::kCancel);
    ShowMessageBox(&rig.h.GetSurface(), StringView("T"), StringView("Hi"), MessageButtons::Ok, &MbLog::Done, &log);
    rig.h.Advance(0);
    rig.h.Key(Key::Escape);
    CHECK(log.result == dialog_result::kOk);
    CHECK(log.calls == 4);
}

TEST_CASE("a confirm dialog has a named default button and Cancel", "[ui][dialog][messagebox]")
{
    DlRig rig;
    MbLog log;
    Dialog* d = ShowConfirm(&rig.h.GetSurface(), StringView("Restart"), StringView("The change needs a restart."),
                            StringView("Restart now"), &MbLog::Done, &log);
    rig.h.Advance(0);
    REQUIRE(d->DefaultButton() != nullptr);
    CHECK(d->DefaultButton()->GetText() == StringView("Restart now"));
    rig.h.Key(Key::Enter);
    CHECK(log.result == dialog_result::kOk);
}
