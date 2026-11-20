#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "string/grapheme.h"
#include "ui/clipboard.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/testing/recording_canvas.h"
#include "ui/text_words.h"
#include "ui/widgets/text_field.h"
#include "ui/widgets/value_widgets.h"

using namespace vse;
using namespace vse::ui;

namespace
{

Rect TfRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// A field in a harness, focused.
struct Rig
{
    explicit Rig(f32 width = 200) : h(400, 200, 1, StringView(UI_TEST_FONT_FILE))
    {
        UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
        field = f.Get();
        field->SetBounds(TfRect(10, 10, width, 30));
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
        h.Advance(0);
        h.GetSurface().SetFocus(field);
    }
    void Key(ui::Key k, u32 modifiers = 0) { h.Key(k, true, modifiers); }
    void Type(const char* s) { h.TypeText(StringView(s)); }
    // The x (surface coordinates) of the caret at `offset`.
    f32 XOf(u32 offset)
    {
        const u32 anchor = field->Anchor();
        const u32 caret = field->Caret();
        field->SetSelection(offset, offset);
        const f32 x = field->CaretRect().x;
        field->SetSelection(anchor, caret);
        return x;
    }
    void Press(f32 x, u32 modifiers = 0)
    {
        Event e;
        e.position = Point{x, 25};
        e.modifiers = modifiers;
        e.type = EventType::PointerMove;
        h.GetSurface().Dispatch(e);
        e.type = EventType::PointerDown;
        e.button = os::kMouseLeft;
        h.GetSurface().Dispatch(e);
    }
    void Release(f32 x)
    {
        Event e;
        e.position = Point{x, 25};
        e.type = EventType::PointerUp;
        e.button = os::kMouseLeft;
        h.GetSurface().Dispatch(e);
    }
    Harness h;
    TextField* field = nullptr;
};

struct Counter
{
    int changed = 0;
    int submitted = 0;
    int cancelled = 0;
};
void CountChanged(void* u, TextField*)
{
    ++static_cast<Counter*>(u)->changed;
}
void CountSubmit(void* u, TextField*)
{
    ++static_cast<Counter*>(u)->submitted;
}
void CountCancel(void* u, TextField*)
{
    ++static_cast<Counter*>(u)->cancelled;
}

bool NoDigits(void*, const TextField*, StringView proposed)
{
    for (usize i = 0; i < proposed.Length(); ++i)
    {
        if (proposed[i] >= '0' && proposed[i] <= '9')
        {
            return false;
        }
    }
    return true;
}

// A clipboard that answers later, to prove the field does not rely on the answer arriving at once.
class DelayedClipboard : public Clipboard
{
public:
    void SetText(StringView text) override
    {
        text_.Clear();
        for (usize i = 0; i < text.Length(); ++i)
        {
            text_.Push(text[i]);
        }
    }
    bool HasText() override { return !text_.Empty(); }
    Ticket RequestPaste(PasteFunction function, void* user) override
    {
        function_ = function;
        user_ = user;
        ticket_ = ++next_;
        return ticket_;
    }
    void CancelPaste(Ticket ticket) override
    {
        if (ticket == ticket_)
        {
            function_ = nullptr;
            ticket_ = 0;
            ++cancelled;
        }
    }
    bool Deliver()
    {
        if (function_ == nullptr)
        {
            return false;
        }
        PasteFunction f = function_;
        function_ = nullptr;
        ticket_ = 0;
        f(user_, StringView(text_.Data(), text_.Size()));
        return true;
    }
    int cancelled = 0;

private:
    Vector<char> text_;
    PasteFunction function_ = nullptr;
    void* user_ = nullptr;
    Ticket ticket_ = 0;
    Ticket next_ = 0;
};

usize CaretRects(const RecordingCanvas& c)
{
    usize n = 0;
    for (usize i = 0; i < c.Calls().Size(); ++i)
    {
        n += c.Calls()[i].kind == CanvasCallKind::Rect && c.Calls()[i].rect.w == TextField::kCaretWidth;
    }
    return n;
}

usize TextCalls(const RecordingCanvas& c, u32* glyphs = nullptr)
{
    usize n = 0;
    for (usize i = 0; i < c.Calls().Size(); ++i)
    {
        if (c.Calls()[i].kind != CanvasCallKind::Text)
        {
            continue;
        }
        ++n;
        if (glyphs != nullptr)
        {
            *glyphs += c.Calls()[i].glyph_count;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("Word classes and boundaries", "[ui][textfield]")
{
    CHECK(ClassOf('a') == CharClass::Word);
    CHECK(ClassOf('_') == CharClass::Word);
    CHECK(ClassOf('7') == CharClass::Word);
    CHECK(ClassOf(' ') == CharClass::Space);
    CHECK(ClassOf(0x3000) == CharClass::Space);
    CHECK(ClassOf(',') == CharClass::Punct);
    CHECK(ClassOf(0x2026) == CharClass::Punct);
    CHECK(ClassOf(0xE9) == CharClass::Word);  // é
    CHECK(ClassOf(0x4E2D) == CharClass::Word);
    const StringView s("foo bar,, baz");
    CHECK(WordEnd(s, 0) == 3);
    CHECK(WordEnd(s, 3) == 7);   // skips the space, ends the word
    CHECK(WordEnd(s, 7) == 9);   // the run of punctuation is a word
    CHECK(WordEnd(s, 9) == 13);  // spaces skipped
    CHECK(WordEnd(s, 13) == 13);
    CHECK(WordStart(s, 13) == 10);
    CHECK(WordStart(s, 10) == 7);
    CHECK(WordStart(s, 7) == 4);
    CHECK(WordStart(s, 4) == 0);
    CHECK(WordStart(s, 0) == 0);
    u32 b = 0;
    u32 e = 0;
    WordRangeAt(s, 5, &b, &e);
    CHECK(b == 4);
    CHECK(e == 7);
    WordRangeAt(s, 3, &b, &e);  // on the space between words: the space run
    CHECK(b == 3);
    CHECK(e == 4);
    WordRangeAt(s, 13, &b, &e);  // at the end: the last run
    CHECK(b == 10);
    CHECK(e == 13);
    WordRangeAt(StringView(), 0, &b, &e);
    CHECK(b == 0);
    CHECK(e == 0);
    const StringView utf8("caf\xC3\xA9 \xE4\xB8\xAD");
    CHECK(WordEnd(utf8, 0) == 5);
    CHECK(WordStart(utf8, 5) == 0);
}

TEST_CASE("TextField: typing, selection, deleting and Home/End", "[ui][textfield]")
{
    Rig r;
    Counter counts;
    r.field->OnChanged().Connect(CountChanged, &counts);
    r.Type("hello");
    CHECK(r.field->Text() == StringView("hello"));
    CHECK(r.field->Caret() == 5);
    CHECK(counts.changed == 5);
    r.Key(Key::Home);
    CHECK(r.field->Caret() == 0);
    r.Key(Key::Right);
    CHECK(r.field->Caret() == 1);
    r.Key(Key::Right, os::kModShift);
    r.Key(Key::Right, os::kModShift);
    CHECK(r.field->HasSelection());
    CHECK(r.field->SelectedText() == StringView("el"));
    r.Key(Key::Left);  // collapses to the start of the selection
    CHECK(r.field->Caret() == 1);
    CHECK(!r.field->HasSelection());
    r.Key(Key::Right, os::kModShift);
    r.Key(Key::Right, os::kModShift);
    r.Key(Key::Right);  // collapses to its end
    CHECK(r.field->Caret() == 3);
    r.Key(Key::Left, os::kModShift);
    r.Type("X");  // typing replaces the selection
    CHECK(r.field->Text() == StringView("heXlo"));
    CHECK(r.field->Caret() == 3);
    r.Key(Key::Backspace);
    CHECK(r.field->Text() == StringView("helo"));
    r.Key(Key::Delete);
    CHECK(r.field->Text() == StringView("heo"));
    r.Key(Key::A, os::kModShortcut);
    CHECK(r.field->SelectedText() == StringView("heo"));
    r.Key(Key::Delete);
    CHECK(r.field->Text().Length() == 0);
    r.Key(Key::Backspace);  // nothing left: no change
    r.Key(Key::Delete);
    CHECK(r.field->Text().Length() == 0);
    // Keys that mean nothing here are left alone; controls and Tab do not insert.
    Event tab;
    tab.type = EventType::KeyDown;
    tab.key = Key::Tab;
    r.field->OnEvent(tab);
    CHECK(!tab.handled);
    Event f1;
    f1.type = EventType::KeyDown;
    f1.key = Key::F1;
    r.field->OnEvent(f1);
    CHECK(!f1.handled);
    Event ctl;
    ctl.type = EventType::Text;
    ctl.text[0] = '\n';
    ctl.text_length = 1;
    r.field->OnEvent(ctl);
    CHECK(r.field->Text().Length() == 0);
    r.Type("ab");
    r.Key(Key::End, os::kModShift);
    CHECK(r.field->Caret() == 2);
    r.Key(Key::Home, os::kModShift);
    CHECK(r.field->SelectedText() == StringView("ab"));
}

TEST_CASE("TextField: a cluster is one unit to move and to delete", "[ui][textfield]")
{
    struct Sample
    {
        const char* name;
        const char* text;
    };
    const Sample samples[] = {
        {"combining mark", "e\xCC\x81"},
        {"flag", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"},
        {"ZWJ family", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"},
        {"Hangul syllable", "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB"},
    };
    for (const Sample& sample : samples)
    {
        INFO(sample.name);
        Rig r;
        const u32 len = static_cast<u32>(StringView(sample.text).Length());
        r.field->SetText(StringView(sample.text));
        r.Type("x");
        CHECK(r.field->Caret() == len + 1);
        r.Key(Key::Left);
        CHECK(r.field->Caret() == len);
        r.Key(Key::Left);
        CHECK(r.field->Caret() == 0);
        r.Key(Key::Right);
        CHECK(r.field->Caret() == len);
        r.Key(Key::Backspace);  // removes the whole cluster
        CHECK(r.field->Text() == StringView("x"));
        r.field->SetText(StringView(sample.text));
        r.Key(Key::Home);
        r.Key(Key::Delete);
        CHECK(r.field->Text().Length() == 0);
        // Shift selects the whole cluster in one step.
        r.field->SetText(StringView(sample.text));
        r.Key(Key::Home);
        r.Key(Key::Right, os::kModShift);
        CHECK(r.field->Caret() == len);
        // A selection set inside the cluster snaps to its start.
        r.field->SetSelection(1, 1);
        CHECK(r.field->Caret() == 0);
    }
}

TEST_CASE("TextField: word movement and word deletion", "[ui][textfield]")
{
    Rig r;
    r.field->SetText(StringView("foo bar,, baz"));
    r.Key(Key::Home);
    r.Key(Key::Right, os::kModCtrl);
    CHECK(r.field->Caret() == 3);
    r.Key(Key::Right, os::kModCtrl);
    CHECK(r.field->Caret() == 7);
    r.Key(Key::Right, os::kModCtrl | os::kModShift);
    CHECK(r.field->Caret() == 9);
    CHECK(r.field->SelectedText() == StringView(",,"));
    r.Key(Key::End);
    r.Key(Key::Left, os::kModCtrl);
    CHECK(r.field->Caret() == 10);
    r.Key(Key::Backspace, os::kModCtrl);
    CHECK(r.field->Text() == StringView("foo barbaz"));  // from the start of "baz" the word before is ",,"
    r.Key(Key::Home);
    r.Key(Key::Delete, os::kModCtrl);
    CHECK(r.field->Text() == StringView(" barbaz"));
}

TEST_CASE("TextField: the mouse places the caret, drags, double and triple clicks", "[ui][textfield]")
{
    Rig r;
    r.field->SetText(StringView("hello big world"));
    r.h.Advance(0);
    r.h.GetSurface().SetTime(10);
    // A click between "hel" and "lo" (the boundary at 3), nearer the left of 'l'.
    r.Press(r.XOf(3) + 1);
    r.Release(r.XOf(3) + 1);
    CHECK(r.field->Caret() == 3);
    CHECK(!r.field->HasSelection());
    // A click on the left half of a character goes before it, on the right half after it.
    r.h.GetSurface().SetTime(20);
    r.Press(r.XOf(1) + (r.XOf(2) - r.XOf(1)) * 0.75f);
    r.Release(0);
    CHECK(r.field->Caret() == 2);
    // Past the end goes to the end; before the start to the start.
    r.h.GetSurface().SetTime(30);
    r.Press(205);
    r.Release(205);
    CHECK(r.field->Caret() == 15);
    // A drag selects, outside the field too (the pointer is captured).
    r.h.GetSurface().SetTime(40);
    r.Press(r.XOf(2) + 1);
    Event move;
    move.type = EventType::PointerMove;
    move.position = Point{r.XOf(5) + 1, 25};
    r.h.GetSurface().Dispatch(move);
    CHECK(r.field->Anchor() == 2);
    CHECK(r.field->Caret() == 5);
    move.position = Point{-100, 500};
    r.h.GetSurface().Dispatch(move);
    CHECK(r.field->Caret() == 0);
    r.Release(0);
    move.position = Point{r.XOf(8) + 1, 25};
    r.h.GetSurface().Dispatch(move);  // released: no more selecting
    CHECK(r.field->Caret() == 0);
    // Double click: a word; triple click: everything. A press long after is a single click again.
    r.h.GetSurface().SetTime(50);
    const f32 x = r.XOf(7);
    r.Press(x);
    r.Release(x);
    r.h.GetSurface().SetTime(50.1);
    r.Press(x);
    r.Release(x);
    CHECK(r.field->SelectedText() == StringView("big"));
    r.h.GetSurface().SetTime(50.2);
    r.Press(x);
    r.Release(x);
    CHECK(r.field->SelectedText() == StringView("hello big world"));
    r.h.GetSurface().SetTime(52);
    r.Press(x);
    r.Release(x);
    CHECK(!r.field->HasSelection());
    // Shift+click extends from the caret.
    r.h.GetSurface().SetTime(60);
    r.Press(r.XOf(12) + 1, os::kModShift);
    r.Release(0);
    CHECK(r.field->Anchor() == r.field->Anchor());
    CHECK(r.field->Caret() == 12);
    CHECK(r.field->HasSelection());
    // The shape is the I-beam.
    CHECK(r.field->Cursor() == os::CursorShape::IBeam);
}

TEST_CASE("TextField: the text scrolls to keep the caret in view", "[ui][textfield]")
{
    Rig r(80);
    r.Type("the quick brown fox jumps over the lazy dog");
    r.h.Advance(0);
    const Rect b = r.field->Bounds();
    CHECK(r.field->ScrollX() > 0);
    const Rect caret = r.field->CaretRect();
    CHECK(caret.x >= b.x);
    CHECK(caret.x <= b.Right());
    r.Key(Key::Home);
    CHECK(r.field->ScrollX() == 0);
    CHECK(r.field->CaretRect().x >= b.x);
    // Walking right past the edge scrolls with a margin: the caret never leaves the box.
    for (int i = 0; i < 40; ++i)
    {
        r.Key(Key::Right);
        const Rect c = r.field->CaretRect();
        CHECK(c.x >= b.x);
        CHECK(c.x <= b.Right());
    }
    // Text that fits does not scroll.
    r.field->SetText(StringView("ab"));
    CHECK(r.field->ScrollX() == 0);
}

TEST_CASE("TextField: the caret is drawn where the layout says and blinks with the surface clock", "[ui][textfield]")
{
    Rig r;
    r.h.GetSurface().SetTime(100);
    r.Type("ab");
    RecordingCanvas on;
    r.field->Draw(on);
    REQUIRE(CaretRects(on) == 1);
    const Rect caret = r.field->CaretRect();
    for (usize i = 0; i < on.Calls().Size(); ++i)
    {
        if (on.Calls()[i].kind == CanvasCallKind::Rect && on.Calls()[i].rect.w == TextField::kCaretWidth)
        {
            CHECK(on.Calls()[i].rect.x == caret.x);
            CHECK(on.Calls()[i].rect.y == caret.y);
            CHECK(on.Calls()[i].rect.h == caret.h);
        }
    }
    CHECK(r.XOf(0) < r.XOf(1));
    CHECK(r.XOf(1) < r.XOf(2));
    CHECK(r.field->CaretOn());
    r.h.GetSurface().SetTime(100 + TextField::kBlinkSeconds + 0.1);
    CHECK(!r.field->CaretOn());
    RecordingCanvas off;
    r.field->Draw(off);
    CHECK(CaretRects(off) == 0);
    r.h.GetSurface().SetTime(100 + TextField::kBlinkSeconds * 2 + 0.1);
    CHECK(r.field->CaretOn());
    // Input restarts the blink: on at once.
    r.h.GetSurface().SetTime(100 + TextField::kBlinkSeconds + 0.1);
    r.Key(Key::Left);
    CHECK(r.field->CaretOn());
    // Unfocused: no caret, however the clock stands.
    r.h.GetSurface().SetFocus(nullptr);
    RecordingCanvas unfocused;
    r.field->Draw(unfocused);
    CHECK(CaretRects(unfocused) == 0);
    CHECK(!r.field->WantsUpdate());
    r.h.GetSurface().SetFocus(r.field);
    CHECK(r.field->WantsUpdate());
}

TEST_CASE("TextField: maximum length, validator, read-only, password and placeholder", "[ui][textfield]")
{
    SECTION("maximum length counts clusters")
    {
        Rig r;
        r.field->SetMaxLength(3);
        r.Type("e\xCC\x81");
        r.Type("a");
        r.Type("b");
        CHECK(r.field->Text() == StringView("e\xCC\x81"
                                            "ab"));
        r.Type("c");  // a fourth cluster is refused
        CHECK(r.field->Text() == StringView("e\xCC\x81"
                                            "ab"));
        r.field->SetText(StringView("abcdef"));  // set text is cut
        CHECK(r.field->Text() == StringView("abc"));
        r.field->SetSelection(1, 2);
        r.Type("Z");  // replacing a selection keeps the count
        CHECK(r.field->Text() == StringView("aZc"));
    }
    SECTION("the validator can refuse an edit")
    {
        Rig r;
        Counter counts;
        r.field->OnChanged().Connect(CountChanged, &counts);
        r.field->SetValidator(NoDigits, nullptr);
        r.Type("a1b2c");
        CHECK(r.field->Text() == StringView("abc"));
        CHECK(counts.changed == 3);
        r.field->SetValidator(nullptr, nullptr);
        r.Type("9");
        CHECK(r.field->Text() == StringView("abc9"));
    }
    SECTION("read-only keeps the caret and the selection and refuses edits")
    {
        Rig r;
        r.field->SetText(StringView("fixed"));
        r.field->SetReadOnly(true);
        r.Type("x");
        r.Key(Key::Backspace);
        r.Key(Key::Delete);
        r.Key(Key::V, os::kModShortcut);
        CHECK(r.field->Text() == StringView("fixed"));
        r.Key(Key::Home);
        r.Key(Key::Right, os::kModShift);
        CHECK(r.field->SelectedText() == StringView("f"));
        r.Key(Key::C, os::kModShortcut);
        Clipboard& clip = r.h.GetSurface().GetClipboard();
        CHECK(static_cast<LocalClipboard&>(clip).Text() == StringView("f"));
        r.Key(Key::X, os::kModShortcut);  // cut copies but removes nothing
        CHECK(r.field->Text() == StringView("fixed"));
        CHECK(r.field->ReadOnly());
    }
    SECTION("a password draws one bullet per cluster and refuses copy")
    {
        Rig r;
        r.field->SetPassword(true);
        r.Type("pw");
        r.Type("e\xCC\x81");
        CHECK(r.field->Text() == StringView("pwe\xCC\x81"));
        RecordingCanvas c;
        r.field->Draw(c);
        u32 glyphs = 0;
        CHECK(TextCalls(c, &glyphs) == 1);
        CHECK(glyphs == 3);
        r.Key(Key::Left);
        CHECK(r.field->Caret() == 2);  // moves by cluster of the real text
        r.Key(Key::A, os::kModShortcut);
        r.Key(Key::C, os::kModShortcut);
        r.Key(Key::X, os::kModShortcut);
        CHECK(!r.h.GetSurface().GetClipboard().HasText());
        CHECK(r.field->Text() == StringView("pwe\xCC\x81"));
        // The caret x follows the bullets: three bullets, the caret at 2 is left of the end.
        CHECK(r.XOf(2) < r.XOf(5));
        // Word movement does not reveal where the spaces are.
        r.field->SetPassword(true);
        r.field->SetText(StringView("ab cd"));
        r.Key(Key::Home);
        r.Key(Key::Right, os::kModCtrl);
        CHECK(r.field->Caret() == 5);
        r.Key(Key::Left, os::kModCtrl);
        CHECK(r.field->Caret() == 0);
        r.field->SetSelection(1, 1);
        r.Key(Key::Backspace, os::kModCtrl);
        CHECK(r.field->Text() == StringView("b cd"));
        r.field->SetText(StringView("ab cd"));
        r.h.GetSurface().SetTime(500);
        const f32 x = r.XOf(1);
        r.Press(x);
        r.Release(x);
        r.h.GetSurface().SetTime(500.1);
        r.Press(x);
        r.Release(x);
        CHECK(r.field->SelectedText() == StringView("ab cd"));
        r.field->SetPassword(false);
        CHECK(!r.field->Password());
    }
    SECTION("the placeholder shows only while empty and unfocused")
    {
        Rig r;
        r.field->SetPlaceholder(StringView("name"));
        RecordingCanvas focused;
        r.field->Draw(focused);
        CHECK(TextCalls(focused) == 0);
        r.h.GetSurface().SetFocus(nullptr);
        RecordingCanvas idle;
        r.field->Draw(idle);
        CHECK(TextCalls(idle) == 1);
        r.field->SetText(StringView("x"));
        RecordingCanvas filled;
        r.field->Draw(filled);
        CHECK(TextCalls(filled) == 1);
    }
}

TEST_CASE("TextField: undo and redo with coalescing", "[ui][textfield]")
{
    Rig r;
    r.Type("hello");
    r.Type(" ");
    r.Type("world");
    CHECK(r.field->CanUndo());
    CHECK(r.field->Undo());
    CHECK(r.field->Text() == StringView("hello "));
    CHECK(r.field->Undo());
    CHECK(r.field->Text() == StringView("hello"));
    CHECK(r.field->Caret() == 5);
    CHECK(r.field->Undo());
    CHECK(r.field->Text().Length() == 0);
    CHECK(!r.field->CanUndo());
    CHECK(!r.field->Undo());
    r.Key(Key::Z, os::kModShortcut | os::kModShift);
    CHECK(r.field->Text() == StringView("hello"));
    r.Key(Key::Y, os::kModCtrl);
    CHECK(r.field->Text() == StringView("hello "));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("hello"));
    // A new edit drops the redo history.
    r.Type("!");
    CHECK(!r.field->CanRedo());
    // Moving the caret ends a typing run.
    r.field->SetText(StringView());
    r.Type("ab");
    r.Key(Key::Left);
    r.Key(Key::Right);
    r.Type("cd");
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("ab"));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text().Length() == 0);
    // A run of Backspace is one step; so is a run of Delete.
    r.field->SetText(StringView("abcdef"));
    r.Key(Key::Backspace);
    r.Key(Key::Backspace);
    r.Key(Key::Backspace);
    CHECK(r.field->Text() == StringView("abc"));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("abcdef"));
    CHECK(r.field->Caret() == 6);
    r.Key(Key::Z, os::kModShortcut | os::kModShift);
    CHECK(r.field->Text() == StringView("abc"));
    r.field->SetText(StringView("abcdef"));
    r.Key(Key::Home);
    r.Key(Key::Delete);
    r.Key(Key::Delete);
    CHECK(r.field->Text() == StringView("cdef"));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("abcdef"));
    CHECK(r.field->Caret() == 0);
    // Replacing a selection undoes to the selection.
    r.field->SetText(StringView("abcdef"));
    r.field->SetSelection(1, 4);
    r.Type("X");
    CHECK(r.field->Text() == StringView("aXef"));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("abcdef"));
    CHECK(r.field->SelectedText() == StringView("bcd"));
    // Typing after replacing a selection joins the same step.
    r.field->SetText(StringView("abcdef"));
    r.field->SetSelection(1, 4);
    r.Type("X");
    r.Type("Y");
    CHECK(r.field->Text() == StringView("aXYef"));
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text() == StringView("abcdef"));
    // A read-only field refuses undo and redo from the keyboard.
    r.Key(Key::End);
    r.Type("Q");
    r.field->SetReadOnly(true);
    r.Key(Key::Z, os::kModShortcut);
    CHECK(r.field->Text().Length() == 7);
    r.field->SetReadOnly(false);
    r.Key(Key::Z, os::kModShortcut);
    r.field->SetReadOnly(true);
    r.Key(Key::Y, os::kModCtrl);
    r.Key(Key::Z, os::kModShortcut | os::kModShift);
    CHECK(r.field->Text() == StringView("abcdef"));
    r.field->SetReadOnly(false);
    // The history is bounded.
    r.field->SetText(StringView());
    for (usize i = 0; i < TextField::kMaxUndo + 20; ++i)
    {
        r.Type("a");
        r.Key(Key::Left);  // each press is its own step
        r.Key(Key::End);
    }
    usize steps = 0;
    while (r.field->Undo())
    {
        ++steps;
    }
    CHECK(steps == TextField::kMaxUndo);
}

TEST_CASE("TextField: the clipboard, in process and asynchronous", "[ui][textfield]")
{
    SECTION("copy, cut and paste by every key")
    {
        Rig r;
        r.field->SetText(StringView("one two"));
        r.field->SetSelection(0, 3);
        r.Key(Key::C, os::kModShortcut);
        r.Key(Key::End);
        r.Key(Key::V, os::kModShortcut);
        CHECK(r.field->Text() == StringView("one twoone"));
        r.field->SetSelection(0, 4);
        r.Key(Key::X, os::kModShortcut);
        CHECK(r.field->Text() == StringView("twoone"));
        r.Key(Key::Home);
        r.Key(Key::Insert, os::kModShift);
        CHECK(r.field->Text() == StringView("one twoone"));
        r.field->SetSelection(0, 3);
        r.Key(Key::Insert, os::kModCtrl);
        r.field->SetSelection(0, 0);
        r.Key(Key::Delete, os::kModShift);  // nothing selected: nothing cut
        CHECK(r.field->Text() == StringView("one twoone"));
        r.field->SetSelection(0, 3);
        r.Key(Key::Delete, os::kModShift);
        CHECK(r.field->Text() == StringView(" twoone"));
        r.Key(Key::V, os::kModShortcut);
        CHECK(r.field->Text() == StringView("one twoone"));
        // An empty clipboard pastes nothing; a pasted second line is dropped.
        r.h.GetSurface().GetClipboard().SetText(StringView("line one\nline two"));
        r.field->SetText(StringView());
        r.Key(Key::V, os::kModShortcut);
        CHECK(r.field->Text() == StringView("line one"));
    }
    SECTION("a paste that arrives later")
    {
        Rig r;
        DelayedClipboard clip;
        r.h.GetSurface().SetClipboard(&clip);
        clip.SetText(StringView("later"));
        r.Key(Key::V, os::kModShortcut);
        CHECK(r.field->Text().Length() == 0);
        CHECK(clip.Deliver());
        CHECK(r.field->Text() == StringView("later"));
        // Destroying the field cancels the request: a late answer has nobody to go to.
        r.Key(Key::V, os::kModShortcut);
        r.field->RemoveFromParent();
        r.h.GetSurface().SetClipboard(nullptr);
        CHECK(clip.cancelled == 1);
        CHECK(!clip.Deliver());
    }
    SECTION("the in-process clipboard answers at once and keeps its text")
    {
        LocalClipboard clip;
        CHECK(!clip.HasText());
        int called = 0;
        clip.RequestPaste([](void* u, StringView) { ++*static_cast<int*>(u); }, &called);
        CHECK(called == 0);  // nothing to paste
        clip.SetText(StringView("abc"));
        CHECK(clip.HasText());
        clip.RequestPaste([](void* u, StringView t) { *static_cast<int*>(u) += static_cast<int>(t.Length()); },
                          &called);
        CHECK(called == 3);
    }
}

TEST_CASE("TextField: Enter submits, Escape cancels, losing the focus is reported", "[ui][textfield]")
{
    Rig r;
    Counter counts;
    int lost = 0;
    r.field->OnSubmit().Connect(CountSubmit, &counts);
    r.field->OnCancel().Connect(CountCancel, &counts);
    r.field->OnFocusLost().Connect([](void* u, TextField*) { ++*static_cast<int*>(u); }, &lost);
    r.Key(Key::Enter);
    r.Key(Key::Escape);
    CHECK(counts.submitted == 1);
    CHECK(counts.cancelled == 1);
    r.h.GetSurface().SetFocus(nullptr);
    CHECK(lost == 1);
}

TEST_CASE("TextField: a seeded random edit sequence agrees with a simple model", "[ui][textfield]")
{
    Rig r(300);
    // The model: ASCII text, a caret and an anchor; every cluster is one byte.
    struct Model
    {
        Vector<char> text;
        u32 caret = 0;
        u32 anchor = 0;
        void Erase(u32 lo, u32 hi)
        {
            text.RemoveRange(lo, hi - lo);
            caret = anchor = lo;
        }
        void Insert(char c)
        {
            if (caret != anchor)
            {
                Erase(caret < anchor ? caret : anchor, caret < anchor ? anchor : caret);
            }
            text.Insert(caret, c);
            ++caret;
            anchor = caret;
        }
    } model;
    u64 seed = 0x2545F4914F6CDD1Dull;
    auto next = [&]()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return static_cast<u32>(seed >> 33);
    };
    const char alphabet[] = "abc xyz";
    for (int step = 0; step < 600; ++step)
    {
        const u32 op = next() % 10;
        const u32 len = static_cast<u32>(model.text.Size());
        switch (op)
        {
        case 0:
        case 1:
        case 2:
        case 3:
        {
            const char c = alphabet[next() % (sizeof(alphabet) - 1)];
            char s[2] = {c, 0};
            r.Type(s);
            model.Insert(c);
            break;
        }
        case 4:
            r.Key(Key::Backspace);
            if (model.caret != model.anchor)
            {
                model.Erase(model.caret < model.anchor ? model.caret : model.anchor,
                            model.caret < model.anchor ? model.anchor : model.caret);
            }
            else if (model.caret > 0)
            {
                model.Erase(model.caret - 1, model.caret);
            }
            break;
        case 5:
            r.Key(Key::Delete);
            if (model.caret != model.anchor)
            {
                model.Erase(model.caret < model.anchor ? model.caret : model.anchor,
                            model.caret < model.anchor ? model.anchor : model.caret);
            }
            else if (model.caret < len)
            {
                model.Erase(model.caret, model.caret + 1);
            }
            break;
        case 6:
        {
            const bool shift = (next() & 1) != 0;
            r.Key(Key::Left, shift ? static_cast<u32>(os::kModShift) : 0u);
            if (!shift && model.caret != model.anchor)
            {
                model.caret = model.anchor = model.caret < model.anchor ? model.caret : model.anchor;
            }
            else
            {
                if (model.caret > 0)
                {
                    --model.caret;
                }
                if (!shift)
                {
                    model.anchor = model.caret;
                }
            }
            break;
        }
        case 7:
        {
            const bool shift = (next() & 1) != 0;
            r.Key(Key::Right, shift ? static_cast<u32>(os::kModShift) : 0u);
            if (!shift && model.caret != model.anchor)
            {
                model.caret = model.anchor = model.caret > model.anchor ? model.caret : model.anchor;
            }
            else
            {
                if (model.caret < len)
                {
                    ++model.caret;
                }
                if (!shift)
                {
                    model.anchor = model.caret;
                }
            }
            break;
        }
        case 8:
        {
            const bool shift = (next() & 1) != 0;
            const bool home = (next() & 1) != 0;
            r.Key(home ? Key::Home : Key::End, shift ? static_cast<u32>(os::kModShift) : 0u);
            model.caret = home ? 0 : len;
            if (!shift)
            {
                model.anchor = model.caret;
            }
            break;
        }
        default:
            r.Key(Key::A, os::kModShortcut);
            model.anchor = 0;
            model.caret = len;
            break;
        }
        INFO("step " << step << " op " << op);
        REQUIRE(r.field->Text() == StringView(model.text.Data(), model.text.Size()));
        REQUIRE(r.field->Caret() == model.caret);
        REQUIRE(r.field->Anchor() == model.anchor);
    }
    // Undo as far as the history goes and redo it all: the end comes back.
    const Vector<char> final_text = [&]()
    {
        Vector<char> v;
        for (usize i = 0; i < r.field->Text().Length(); ++i)
        {
            v.Push(r.field->Text()[i]);
        }
        return v;
    }();
    usize undone = 0;
    while (r.field->Undo())
    {
        ++undone;  // the history is bounded, so this goes back only so far
    }
    CHECK(undone <= TextField::kMaxUndo);
    CHECK(undone > 0);
    while (r.field->Redo())
    {
    }
    CHECK(r.field->Text() == StringView(final_text.Data(), final_text.Size()));
}

TEST_CASE("SpinBox: typing a number, Enter commits, bad text reverts", "[ui][textfield]")
{
    Harness h(300, 100, 1, StringView(UI_TEST_FONT_FILE));
    UniquePtr<SpinBox> made = MakeUnique<SpinBox>(StringView("spin"));
    SpinBox* box = made.Get();
    box->SetBounds(TfRect(10, 10, 120, 30));
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(made.Release()));
    h.Advance(0);
    int changes = 0;
    box->OnChanged().Connect([](void* u, SpinBox*) { ++*static_cast<int*>(u); }, &changes);
    TextField* field = box->Field();
    CHECK(field->Bounds().w < box->Bounds().w);  // the buttons are outside the field
    h.GetSurface().SetFocus(field);
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("4x2"));  // the letter is refused
    CHECK(field->Text() == StringView("42"));
    CHECK(box->Value() == 0);  // not committed yet
    h.Key(Key::Enter);
    CHECK(box->Value() == 42);
    CHECK(changes == 1);
    // Out of range: the clamp policy pulls it in.
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("500"));
    h.Key(Key::Enter);
    CHECK(box->Value() == 100);
    CHECK(field->Text() == StringView("100"));
    // A minus sign is refused while the range has no negatives; a point while there are no decimals.
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("-7.5"));
    CHECK(field->Text() == StringView("75"));
    // Empty or partial text reverts on Enter and on losing the focus.
    h.Key(Key::A, true, os::kModShortcut);
    h.Key(Key::Backspace);
    h.Key(Key::Enter);
    CHECK(box->Value() == 100);
    CHECK(field->Text() == StringView("100"));
    h.TypeText(StringView("5"));  // appended: "1005" is committed on focus loss and clamped
    h.GetSurface().SetFocus(nullptr);
    CHECK(box->Value() == 100);
    // Escape reverts what was typed.
    h.GetSurface().SetFocus(field);
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("8"));
    h.Key(Key::Escape);
    CHECK(field->Text() == StringView("100"));
    CHECK(box->Value() == 100);
    // Decimals and negatives.
    ValueRange range;
    range.min = -5;
    range.max = 5;
    range.step = 0.25;
    box->SetRange(range);
    box->SetDecimals(2);
    box->SetValue(0);
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("-1.30"));
    CHECK(field->Text() == StringView("-1.30"));
    h.TypeText(StringView("9"));  // a third decimal is refused
    CHECK(field->Text() == StringView("-1.30"));
    h.Key(Key::Enter);
    CHECK(box->Value() == -1.25);  // snapped to the step
    CHECK(field->Text() == StringView("-1.25"));
    // The buttons and the field agree.
    h.Click(125, 15);
    CHECK(box->Value() == -1.0);
    CHECK(field->Text() == StringView("-1.00"));
    // Reject policy: out of range goes back to the old value.
    range.policy = RangePolicy::Reject;
    box->SetRange(range);
    h.Key(Key::A, true, os::kModShortcut);
    h.TypeText(StringView("9"));
    h.Key(Key::Enter);
    CHECK(box->Value() == -1.0);
    CHECK(field->Text() == StringView("-1.00"));
}

TEST_CASE("A paste that holds nothing printable leaves the selection alone", "[ui][textfield]")
{
    Rig r;
    r.field->SetText(StringView("keep me"));
    r.field->SetSelection(0, 4);
    r.h.GetSurface().GetClipboard().SetText(StringView("\t\x1b"));
    r.Key(Key::V, os::kModShortcut);
    CHECK(r.field->Text() == StringView("keep me"));  // the selection was deleted before the fix
    r.h.GetSurface().GetClipboard().SetText(StringView("a\tb\nignored"));
    r.Key(Key::V, os::kModShortcut);
    CHECK(r.field->Text() == StringView("ab me"));  // the tab is dropped, the second line is not pasted
}
