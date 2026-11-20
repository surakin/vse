#include "lang/limits.h"
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/callback.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/testing/recording_canvas.h"
#include "ui/text_run.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/display.h"
#include "ui/widgets/label.h"

using namespace vse;
using namespace vse::ui;

namespace
{

Rect BwRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

template <typename T> T* BwAdd(Widget* parent, UniquePtr<T> w, Rect bounds)
{
    T* raw = w.Get();
    raw->SetBounds(bounds);
    parent->AddChild(UniquePtr<Widget>(w.Release()));
    return raw;
}

struct Clicks
{
    int count = 0;
};
void CountClick(void* user, Button*)
{
    ++static_cast<Clicks*>(user)->count;
}

// Draws a widget alone into a recording canvas.
Vector<CanvasCall> Record(Widget* w, f32 scale = 1.0f)
{
    RecordingCanvas c(scale);
    w->Draw(c);
    Vector<CanvasCall> out;
    for (usize i = 0; i < c.Calls().Size(); ++i)
    {
        out.Push(c.Calls()[i]);
    }
    return out;
}

const CanvasCall* FirstOf(const Vector<CanvasCall>& calls, CanvasCallKind kind)
{
    for (usize i = 0; i < calls.Size(); ++i)
    {
        if (calls[i].kind == kind)
        {
            return &calls[i];
        }
    }
    return nullptr;
}

bool BwPixelNear(const RasterImage& img, u32 x, u32 y, u32 abgr)
{
    const u8* p = img.Pixel(x, y);
    auto close = [](u8 a, u32 b) { return Abs(static_cast<int>(a) - static_cast<int>(b)) <= 1; };
    return close(p[0], abgr & 0xFF) && close(p[1], (abgr >> 8) & 0xFF) && close(p[2], (abgr >> 16) & 0xFF);
}

void PressAt(Harness& h, f32 x, f32 y)
{
    h.PointerMove(x, y);
    h.PointerDown(x, y);
}

}  // namespace

TEST_CASE("Callback: order, disconnect by token, and changes while it runs", "[ui][widgets]")
{
    struct Log
    {
        Vector<int> seen;
    } log;
    Callback<int> cb;
    auto a = [](void* u, int v) { static_cast<Log*>(u)->seen.Push(v); };
    auto b = [](void* u, int v) { static_cast<Log*>(u)->seen.Push(v * 10); };
    const ConnectionId ida = cb.Connect(a, &log);
    const ConnectionId idb = cb.Connect(b, &log);
    CHECK(ida != 0);
    CHECK(ida != idb);
    cb.Invoke(1);
    REQUIRE(log.seen.Size() == 2);
    CHECK(log.seen[0] == 1);
    CHECK(log.seen[1] == 10);
    CHECK(cb.Disconnect(ida));
    CHECK(!cb.Disconnect(ida));
    CHECK(cb.Count() == 1);
    cb.Invoke(2);
    CHECK(log.seen.Size() == 3);
    CHECK(log.seen[2] == 20);

    struct Reentrant
    {
        Callback<int>* cb = nullptr;
        ConnectionId other = 0;
        int calls = 0;
    } re;
    Callback<int> cb2;
    re.cb = &cb2;
    const ConnectionId first = cb2.Connect(
        [](void* u, int)
        {
            Reentrant* r = static_cast<Reentrant*>(u);
            ++r->calls;
            r->cb->Disconnect(r->other);  // a listener removes the next one while the callback runs: it is not called
        },
        &re);
    (void)first;
    re.other = cb2.Connect([](void* u, int) { ++static_cast<Reentrant*>(u)->calls; }, &re);
    cb2.Invoke(0);
    CHECK(re.calls == 1);
}

TEST_CASE("Label: sized by its text, variants, wrapping, alignment and the ellipsis", "[ui][widgets]")
{
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    Label* plain = BwAdd(h.GetSurface().Root(), MakeUnique<Label>(StringView("plain"), StringView("Hello world")),
                         BwRect(0, 0, 200, 30));
    h.Advance(0);
    const Size natural = plain->MeasureTotal(Constraints());
    CHECK(natural.w > 20);
    CHECK(natural.h > 8);

    Label* heading = BwAdd(h.GetSurface().Root(), MakeUnique<Label>(StringView("heading"), StringView("Hello world")),
                           BwRect(0, 40, 200, 30));
    heading->SetVariant(StringView("heading"));
    h.Advance(0);
    CHECK(heading->MeasureTotal(Constraints()).h > natural.h);  // the heading variant is larger

    Label* wrapped = BwAdd(
        h.GetSurface().Root(),
        MakeUnique<Label>(StringView("wrapped"), StringView("one two three four five six seven eight")),
        BwRect(0, 80, 60, 100));
    wrapped->SetWrap(true);
    h.Advance(0);
    Constraints narrow;
    narrow.max_w = 60;
    Constraints wide;
    wide.max_w = 600;
    CHECK(wrapped->MeasureTotal(narrow).h > wrapped->MeasureTotal(wide).h);

    SECTION("an ellipsis cuts a long line inside the bounds")
    {
        Label* cut = BwAdd(h.GetSurface().Root(),
                           MakeUnique<Label>(StringView("cut"), StringView("a very long line of text that cannot fit")),
                           BwRect(0, 150, 60, 30));
        cut->SetEllipsis(true);
        Label* spill = BwAdd(
            h.GetSurface().Root(),
            MakeUnique<Label>(StringView("spill"), StringView("a very long line of text that cannot fit")),
            BwRect(0, 110, 60, 30));
        plain->SetCollapsed(true);
        heading->SetCollapsed(true);
        wrapped->SetCollapsed(true);
        h.Advance(0);
        Vector<CanvasCall> cut_calls = Record(cut);
        Vector<CanvasCall> spill_calls = Record(spill);
        const CanvasCall* cut_text = FirstOf(cut_calls, CanvasCallKind::Text);
        const CanvasCall* spill_text = FirstOf(spill_calls, CanvasCallKind::Text);
        REQUIRE(cut_text != nullptr);
        REQUIRE(spill_text != nullptr);
        CHECK(cut_text->glyph_count < spill_text->glyph_count);  // fewer glyphs: the cut text and the ellipsis
        CHECK(cut_text->glyph_count > 1);
    }
    SECTION("alignment moves the text inside the bounds")
    {
        Label* centered = BwAdd(h.GetSurface().Root(), MakeUnique<Label>(StringView("c"), StringView("Hi")),
                                BwRect(0, 150, 200, 30));
        centered->SetAlign(text::Align::Center);
        Label* right = BwAdd(h.GetSurface().Root(), MakeUnique<Label>(StringView("r"), StringView("Hi")),
                             BwRect(0, 110, 200, 30));
        right->SetAlign(text::Align::Right);
        plain->SetCollapsed(true);
        heading->SetCollapsed(true);
        wrapped->SetCollapsed(true);
        h.GetSurface().SetTheme(nullptr);
        h.Advance(0);
        RasterImage img;
        h.Render(&img, 0xFF000000u);
        auto ink_left = [&](u32 y0, u32 y1)
        {
            u32 left = img.width;
            for (u32 y = y0; y < y1; ++y)
            {
                for (u32 x = 0; x < img.width; ++x)
                {
                    const u8* p = img.Pixel(x, y);
                    if ((p[0] != 0 || p[1] != 0 || p[2] != 0) && x < left)
                    {
                        left = x;
                    }
                }
            }
            return left;
        };
        CHECK(ink_left(150, 180) > 60);
        CHECK(ink_left(110, 140) > ink_left(150, 180));
    }
    SECTION("a disabled label draws in the disabled color")
    {
        const u32 normal = h.GetSurface().Themes().Resolve(widget_kind::kLabel, VariantId(0), 0, 1).text_color;
        const u32
            disabled = h.GetSurface().Themes().Resolve(widget_kind::kLabel, VariantId(0), kStateDisabled, 1).text_color;
        REQUIRE(normal != disabled);
        plain->SetEnabled(false);
        Vector<CanvasCall> calls = Record(plain);
        const CanvasCall* text = FirstOf(calls, CanvasCallKind::Text);
        REQUIRE(text != nullptr);
        CHECK(text->color == disabled);
    }
}

TEST_CASE("TextRun: the cut layout fits the limit and ends with an ellipsis", "[ui][widgets]")
{
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    TextRun run;
    run.SetText(StringView("a very long line of text that cannot fit"));
    REQUIRE(run.Ensure(h.GetFont(), 0, 14, 0, 1));
    const f32 full = run.Full().width;
    CHECK(&run.Fit(0) == &run.Full());
    CHECK(&run.Fit(full + 1) == &run.Full());
    const text::TextLayout& cut = run.Fit(60);
    CHECK(&cut != &run.Full());
    CHECK(cut.width <= 60);
    CHECK(cut.width > 40);
    CHECK(cut.byte_length < run.Full().byte_length);
    CHECK(&run.Fit(60) == &cut);  // cached
    const text::TextLayout& tiny = run.Fit(2);
    CHECK(tiny.glyphs.Size() == 1);  // only the ellipsis
    // A cut never splits a cluster: text ending in a combining sequence stays whole.
    run.SetText(StringView("e"
                           "\xCC\x81"
                           "e"
                           "\xCC\x81"
                           "e"
                           "\xCC\x81"
                           "e"
                           "\xCC\x81"
                           "e"
                           "\xCC\x81"
                           "e"
                           "\xCC\x81"));
    REQUIRE(run.Ensure(h.GetFont(), 0, 14, 0, 1));
    const text::TextLayout& marks = run.Fit(30);
    for (usize i = 0; i + 1 < marks.clusters.Size(); ++i)
    {
        CHECK(marks.clusters[i].byte_end == marks.clusters[i + 1].byte_begin);
    }
}

TEST_CASE("Image: the fit modes place the picture and cut it by texture coordinates", "[ui][widgets]")
{
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    Image* img = BwAdd(h.GetSurface().Root(), MakeUnique<Image>(StringView("img")), BwRect(10, 10, 100, 50));
    img->SetImage(7, Size{200, 100});  // natural 2:1, bounds 2:1 after contain... use a 4:1 bounds below
    CHECK(img->MeasureTotal(Constraints()).w == 200);
    img->SetBounds(BwRect(0, 0, 100, 100));

    img->SetFit(ImageFit::Stretch);
    CanvasCall c = *FirstOf(Record(img), CanvasCallKind::Image);
    CHECK(c.rect.w == 100);
    CHECK(c.rect.h == 100);
    CHECK(c.uv.w == 1);
    CHECK(c.image == 7);

    img->SetFit(ImageFit::Contain);
    c = *FirstOf(Record(img), CanvasCallKind::Image);
    CHECK(c.rect.w == 100);
    CHECK(c.rect.h == 50);
    CHECK(c.rect.y == 25);
    CHECK(c.uv.w == 1);
    CHECK(c.uv.h == 1);

    img->SetFit(ImageFit::Cover);
    c = *FirstOf(Record(img), CanvasCallKind::Image);
    CHECK(c.rect.w == 100);
    CHECK(c.rect.h == 100);
    CHECK(c.uv.w == 0.5f);  // half of the width of the picture is shown
    CHECK(c.uv.x == 0.25f);
    CHECK(c.uv.h == 1);

    img->SetFit(ImageFit::None);
    img->SetBounds(BwRect(0, 0, 150, 150));
    c = *FirstOf(Record(img), CanvasCallKind::Image);
    CHECK(c.rect.w == 150);  // 200 wide, cut to the bounds
    CHECK(c.uv.w == 0.75f);
    CHECK(c.rect.h == 100);

    img->SetTint(0x80FFFFFFu);
    CHECK(FirstOf(Record(img), CanvasCallKind::Image)->color == 0x80FFFFFFu);
    img->SetImage(0, Size());
    CHECK(FirstOf(Record(img), CanvasCallKind::Image) == nullptr);
}

TEST_CASE("ProgressBar: fills with the value and slides while indeterminate", "[ui][widgets]")
{
    Harness h(300, 100, 1, StringView(UI_TEST_FONT_FILE));
    ProgressBar* bar = BwAdd(h.GetSurface().Root(), MakeUnique<ProgressBar>(StringView("bar")), BwRect(0, 0, 100, 10));
    const u32 accent = h.GetSurface().Themes().Color(ColorId::Accent);
    auto fill_width = [&]()
    {
        Vector<CanvasCall> calls = Record(bar);
        f32 w = 0;
        for (usize i = 0; i < calls.Size(); ++i)
        {
            if ((calls[i].kind == CanvasCallKind::Rect || calls[i].kind == CanvasCallKind::RoundedRect) &&
                calls[i].color == accent)
            {
                w = calls[i].rect.w;
            }
        }
        return w;
    };
    CHECK(fill_width() == 0);
    bar->SetValue(0.5f);
    const f32 half = fill_width();
    CHECK(half > 40);
    CHECK(half < 50);
    bar->SetValue(1.0f);
    CHECK(fill_width() > 2 * half - 2);
    bar->SetValue(5.0f);
    CHECK(bar->Value() == 1.0f);
    bar->SetValue(Limits<f32>::QuietNaN());
    CHECK(bar->Value() == 0.0f);
    CHECK(!bar->WantsUpdate());

    bar->SetIndeterminate(true);
    CHECK(bar->WantsUpdate());
    h.GetSurface().SetTime(0.3);
    const CanvasCall* a = nullptr;
    Vector<CanvasCall> first = Record(bar);
    for (usize i = 0; i < first.Size(); ++i)
    {
        if (first[i].color == accent)
        {
            a = &first[i];
        }
    }
    REQUIRE(a != nullptr);
    const f32 x0 = a->rect.x;
    h.GetSurface().SetTime(0.6);
    Vector<CanvasCall> second = Record(bar);
    const CanvasCall* b = nullptr;
    for (usize i = 0; i < second.Size(); ++i)
    {
        if (second[i].color == accent)
        {
            b = &second[i];
        }
    }
    REQUIRE(b != nullptr);
    CHECK(b->rect.x > x0);  // the segment moved on
    CHECK(b->rect.w == a->rect.w);

    // The animation repaints each frame and never lays out again.
    h.Advance(0);
    const u64 passes = h.GetSurface().LayoutPasses();
    h.Advance(0.1f);
    h.Advance(0.1f);
    CHECK(h.GetSurface().LayoutPasses() == passes);
    CHECK(h.GetSurface().NeedsRedraw() == false);  // just drawn
    bar->Update(0);
    CHECK(h.GetSurface().NeedsRedraw());
}

TEST_CASE("Separator, Panel and the tooltip and description hooks", "[ui][widgets]")
{
    Harness h(300, 100, 1, StringView(UI_TEST_FONT_FILE));
    Separator* line = BwAdd(h.GetSurface().Root(), MakeUnique<Separator>(StringView("line")), BwRect(0, 10, 100, 4));
    Separator* vline = BwAdd(h.GetSurface().Root(), MakeUnique<Separator>(StringView("vline"), true),
                             BwRect(120, 0, 4, 50));
    const Size across = line->MeasureTotal(Constraints());
    CHECK(across.h == 1);
    CHECK(across.w == 0);
    CHECK(vline->MeasureTotal(Constraints()).w == 1);
    const CanvasCall* drawn = nullptr;
    Vector<CanvasCall> calls = Record(line);
    drawn = FirstOf(calls, CanvasCallKind::Rect);
    REQUIRE(drawn != nullptr);
    CHECK(drawn->rect.w == 100);
    CHECK(drawn->rect.h == 1);
    CHECK(drawn->color == h.GetSurface().Themes().Color(ColorId::Border));

    Panel* panel = BwAdd(h.GetSurface().Root(), MakeUnique<Panel>(StringView("panel")), BwRect(0, 60, 100, 40));
    Label* inside = static_cast<Label*>(panel->AddChild(MakeUnique<Label>(StringView("in"), StringView("x"))));
    h.Advance(0);
    CHECK(inside->Parent() == panel);
    Vector<CanvasCall> pcalls = Record(panel);
    const CanvasCall* bg = FirstOf(pcalls, CanvasCallKind::RoundedRect);
    REQUIRE(bg != nullptr);
    CHECK(bg->color == h.GetSurface().Themes().Resolve(widget_kind::kPanel, VariantId(0), 0, 1).background);
    CHECK(FirstOf(pcalls, CanvasCallKind::Border) != nullptr);

    panel->SetTooltip(StringView("Settings"));
    panel->SetDescription(StringView("Groups the settings"));
    CHECK(panel->Tooltip() == StringView("Settings"));
    CHECK(panel->Description() == StringView("Groups the settings"));
    CHECK(inside->Tooltip().Length() == 0);
}

TEST_CASE("Button: size from content, click on release inside, keys, disabled", "[ui][widgets]")
{
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    Button* ok = BwAdd(h.GetSurface().Root(), MakeUnique<Button>(StringView("ok"), StringView("OK")),
                       BwRect(10, 10, 80, 30));
    Button* longer = BwAdd(h.GetSurface().Root(),
                           MakeUnique<Button>(StringView("longer"), StringView("A much longer caption")),
                           BwRect(10, 60, 200, 30));
    h.Advance(0);
    const ResolvedStyle style = h.GetSurface().Themes().Resolve(widget_kind::kButton, VariantId(0), 0, 1);
    const Size small = ok->MeasureTotal(Constraints());
    CHECK(small.w > style.padding_x * 2);
    CHECK(small.h > style.padding_y * 2);
    CHECK(longer->MeasureTotal(Constraints()).w > small.w);
    ok->SetImage(3);
    CHECK(ok->MeasureTotal(Constraints()).w > small.w);  // the icon adds to the width
    ok->SetImage(0);

    Clicks clicks;
    const ConnectionId token = ok->OnClick().Connect(CountClick, &clicks);
    h.Click(20, 20);
    CHECK(clicks.count == 1);

    // A press, a drag out and a release elsewhere is no click; a press, drag out and back in is.
    PressAt(h, 20, 20);
    h.PointerMove(150, 150);
    h.PointerUp(150, 150);
    CHECK(clicks.count == 1);
    PressAt(h, 20, 20);
    h.PointerMove(150, 150);
    h.PointerMove(30, 25);
    h.PointerUp(30, 25);
    CHECK(clicks.count == 2);

    // A right click is not a click.
    h.PointerMove(20, 20);
    h.PointerDown(20, 20, 2);
    h.PointerUp(20, 20, 2);
    CHECK(clicks.count == 2);

    // Keyboard: Space and Enter on the focused button, not a repeat.
    h.GetSurface().SetFocus(ok);
    h.Key(Key::Space);
    CHECK(clicks.count == 3);
    h.Key(Key::Enter);
    CHECK(clicks.count == 4);
    h.Key(Key::A);
    CHECK(clicks.count == 4);
    h.GetSurface().SetFocus(longer);
    h.Key(Key::Space);
    CHECK(clicks.count == 4);  // goes to the focused button only

    ok->OnClick().Disconnect(token);
    h.Click(20, 20);
    CHECK(clicks.count == 4);
    ok->OnClick().Connect(CountClick, &clicks);

    SECTION("a disabled button ignores the pointer and the keys and draws disabled")
    {
        ok->SetEnabled(false);
        h.Click(20, 20);
        h.GetSurface().SetFocus(ok);
        h.Key(Key::Space);
        ok->Click();
        CHECK(clicks.count == 4);
        Vector<CanvasCall> calls = Record(ok);
        const CanvasCall* text = FirstOf(calls, CanvasCallKind::Text);
        const CanvasCall* bg = FirstOf(calls, CanvasCallKind::RoundedRect);
        REQUIRE(text != nullptr);
        REQUIRE(bg != nullptr);
        const ResolvedStyle disabled = h.GetSurface().Themes().Resolve(widget_kind::kButton, VariantId(0),
                                                                       kStateDisabled, 1);
        CHECK(text->color == disabled.text_color);
        CHECK(bg->color == disabled.background);
    }
    SECTION("the default flag and the accessibility strings")
    {
        CHECK(!ok->IsDefault());
        ok->SetDefault(true);
        CHECK(ok->IsDefault());
        ok->SetDescription(StringView("Accepts the dialog"));
        CHECK(ok->Description() == StringView("Accepts the dialog"));
        CHECK(ok->Name() == StringView("ok"));
    }
    SECTION("the primary variant uses the accent")
    {
        ok->SetVariant(StringView("primary"));
        Vector<CanvasCall> calls = Record(ok);
        CHECK(FirstOf(calls, CanvasCallKind::RoundedRect)->color == h.GetSurface().Themes().Color(ColorId::Accent));
    }
}

TEST_CASE("ToggleButton and CheckBox: states and cycling", "[ui][widgets]")
{
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    ToggleButton* toggle = BwAdd(h.GetSurface().Root(),
                                 MakeUnique<ToggleButton>(StringView("toggle"), StringView("Bold")),
                                 BwRect(10, 10, 80, 30));
    CheckBox* box = BwAdd(h.GetSurface().Root(), MakeUnique<CheckBox>(StringView("box"), StringView("Accept")),
                          BwRect(10, 60, 150, 24));
    h.Advance(0);
    struct Seen
    {
        int toggled = 0;
        int changed = 0;
    } seen;
    toggle->OnToggled().Connect([](void* u, ToggleButton*) { ++static_cast<Seen*>(u)->toggled; }, &seen);
    box->OnChanged().Connect([](void* u, CheckBox*) { ++static_cast<Seen*>(u)->changed; }, &seen);

    CHECK(!toggle->On());
    h.Click(20, 20);
    CHECK(toggle->On());
    CHECK((toggle->State() & kStateChecked) != 0);
    h.Click(20, 20);
    CHECK(!toggle->On());
    CHECK(seen.toggled == 2);
    toggle->SetOn(true);  // programmatic: no listener
    CHECK(seen.toggled == 2);

    CHECK(box->GetCheckState() == CheckState::Unchecked);
    h.Click(20, 70);
    CHECK(box->GetCheckState() == CheckState::Checked);
    CHECK(box->Checked());
    h.Click(20, 70);
    CHECK(box->GetCheckState() == CheckState::Unchecked);  // two states by default
    CHECK(seen.changed == 2);

    box->SetTriState(true);
    h.Click(20, 70);
    h.Click(20, 70);
    CHECK(box->GetCheckState() == CheckState::Indeterminate);
    CHECK(!box->Checked());
    Vector<CanvasCall> calls = Record(box);
    CHECK(FirstOf(calls, CanvasCallKind::Line) != nullptr);  // the dash
    h.Click(20, 70);
    CHECK(box->GetCheckState() == CheckState::Unchecked);
    box->SetCheckState(CheckState::Indeterminate);
    box->SetTriState(false);
    CHECK(box->GetCheckState() == CheckState::Unchecked);
    box->SetCheckState(CheckState::Indeterminate);  // refused without tri-state
    CHECK(box->GetCheckState() == CheckState::Unchecked);

    box->SetCheckState(CheckState::Checked);
    Vector<CanvasCall> checked = Record(box);
    CHECK(FirstOf(checked, CanvasCallKind::Line) != nullptr);  // the mark
    const CanvasCall* fill = FirstOf(checked, CanvasCallKind::RoundedRect);
    REQUIRE(fill != nullptr);
    CHECK(fill->color ==
          h.GetSurface().Themes().Resolve(widget_kind::kCheckbox, VariantId(0), kStateChecked, 1).background);
}

TEST_CASE("RadioGroup: one selected, arrows move within it, only the selected one is a Tab stop", "[ui][widgets]")
{
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    RadioGroup* group = BwAdd(h.GetSurface().Root(), MakeUnique<RadioGroup>(StringView("group")),
                              BwRect(0, 0, 200, 150));
    RadioButton* a = static_cast<RadioButton*>(
        group->AddChild(MakeUnique<RadioButton>(StringView("a"), StringView("Alpha"))));
    RadioButton* b = static_cast<RadioButton*>(
        group->AddChild(MakeUnique<RadioButton>(StringView("b"), StringView("Beta"))));
    RadioButton* c = static_cast<RadioButton*>(
        group->AddChild(MakeUnique<RadioButton>(StringView("c"), StringView("Gamma"))));
    RadioButton* lone = BwAdd(h.GetSurface().Root(), MakeUnique<RadioButton>(StringView("lone"), StringView("Loose")),
                              BwRect(220, 10, 70, 24));
    h.Advance(0);
    int changes = 0;
    group->OnChanged().Connect([](void* u, RadioGroup*) { ++*static_cast<int*>(u); }, &changes);

    CHECK(group->Selected() == nullptr);
    CHECK(a->Group() == group);
    h.Click(a->Bounds().x + 5, a->Bounds().y + 5);
    CHECK(group->Selected() == a);
    CHECK(a->Selected());
    CHECK(changes == 1);
    h.Click(b->Bounds().x + 5, b->Bounds().y + 5);
    CHECK(group->Selected() == b);
    CHECK(!a->Selected());
    CHECK(changes == 2);
    h.Click(b->Bounds().x + 5, b->Bounds().y + 5);  // again: no change
    CHECK(changes == 2);

    // Tab stops: only the selected one.
    CHECK(b->TabIndex() == 0);
    CHECK(a->TabIndex() == -1);
    CHECK(c->TabIndex() == -1);

    // Arrow keys move the selection and the focus, wrapping.
    h.GetSurface().SetFocus(b);
    h.Key(Key::Down);
    CHECK(group->Selected() == c);
    CHECK(h.GetSurface().Focus() == c);
    h.Key(Key::Right);
    CHECK(group->Selected() == a);  // wrapped
    h.Key(Key::Up);
    CHECK(group->Selected() == c);
    h.Key(Key::Left);
    CHECK(group->Selected() == b);

    b->SetEnabled(false);
    h.GetSurface().SetFocus(a);
    group->Select(a);
    h.Key(Key::Down);
    CHECK(group->Selected() == c);  // the disabled one is skipped
    b->SetEnabled(true);

    group->Select(nullptr);
    CHECK(group->Selected() == nullptr);
    CHECK(a->TabIndex() == 0);

    group->Select(b);
    group->Select(lone);  // not a member: ignored
    CHECK(group->Selected() == b);
    CHECK(!lone->Selected());
    group->Select(nullptr);

    // Outside a group a radio button just turns on.
    CHECK(lone->Group() == nullptr);
    h.Click(225, 15);
    CHECK(lone->Selected());
}

TEST_CASE("widgets: a state change repaints and does not lay out, a theme swap changes the colors", "[ui][widgets]")
{
    Harness h(200, 100, 1, StringView(UI_TEST_FONT_FILE));
    Button* button = BwAdd(h.GetSurface().Root(), MakeUnique<Button>(StringView("button"), StringView("Go")),
                           BwRect(10, 10, 80, 30));
    h.Advance(0);
    const u64 passes = h.GetSurface().LayoutPasses();
    h.PointerMove(20, 20);
    h.Advance(0);
    h.PointerDown(20, 20);
    h.Advance(0);
    h.PointerUp(20, 20);
    h.Advance(0);
    h.GetSurface().SetFocus(button);
    h.Advance(0);
    CHECK(h.GetSurface().LayoutPasses() == passes);

    // The same button under a theme with other colors, same metrics: redrawn, not laid out.
    const Theme& dark = DefaultDarkTheme();  // same metrics as the light one
    RasterImage before;
    RasterImage after;
    h.PointerMove(150, 80);  // off the button, so the normal state is drawn
    h.Advance(0);
    h.GetSurface().SetFocus(nullptr);
    h.Advance(0);
    h.Render(&before, 0xFF000000u);
    h.GetSurface().SetTheme(&dark);
    h.Advance(0);
    h.Render(&after, 0xFF000000u);
    CHECK(h.GetSurface().LayoutPasses() == passes);
    const ResolvedStyle light = h.GetSurface().Themes().Resolve(widget_kind::kButton, VariantId(0), 0, 1);
    CHECK(BwPixelNear(after, 14, 25, light.background));
    CHECK(!BwPixelNear(before, 14, 25,
                       light.background));  // the light theme drew its own, so the dark one differs from it
}

TEST_CASE("Button pixels: each state draws its own background from the theme", "[ui][widgets]")
{
    Harness h(200, 100, 1, StringView(UI_TEST_FONT_FILE));
    Button* button = BwAdd(h.GetSurface().Root(), MakeUnique<Button>(StringView("button"), StringView("Go")),
                           BwRect(10, 10, 100, 40));
    h.Advance(0);
    auto expected = [&](u32 state)
    { return h.GetSurface().Themes().Resolve(widget_kind::kButton, VariantId(0), state, 1).background; };
    auto sample = [&]()
    {
        RasterImage img;
        h.Render(&img, 0xFF000000u);
        return BwPixelNear(img, 16, 30, 0)
                   ? 0u
                   : (static_cast<u32>(img.Pixel(16, 30)[0]) | (static_cast<u32>(img.Pixel(16, 30)[1]) << 8) |
                      (static_cast<u32>(img.Pixel(16, 30)[2]) << 16) | 0xFF000000u);
    };
    auto matches = [&](u32 state)
    {
        RasterImage img;
        h.Render(&img, 0xFF000000u);
        return BwPixelNear(img, 16, 30, expected(state));
    };
    (void)sample;
    CHECK(matches(0));

    h.PointerMove(60, 30);
    h.Advance(0);
    CHECK(matches(kStateHover));
    h.PointerDown(60, 30);
    h.Advance(0);
    CHECK(matches(kStateHover | kStatePressed));
    h.PointerUp(60, 30);
    h.PointerMove(150, 80);
    h.Advance(0);
    CHECK(matches(kStateFocused));  // the click focused it: its background is the normal one, only the border changed
    button->SetEnabled(false);
    h.Advance(0);
    CHECK(matches(kStateDisabled));
    // The shades differ, so the checks above are not all the same color.
    CHECK(expected(0) != expected(kStateHover));
    CHECK(expected(kStateHover) != expected(kStateHover | kStatePressed));
    CHECK(expected(0) != expected(kStateDisabled));
}
