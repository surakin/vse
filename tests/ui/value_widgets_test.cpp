#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/testing/recording_canvas.h"
#include "ui/widgets/range.h"
#include "ui/widgets/value_widgets.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

Rect VwRect(f32 x, f32 y, f32 w, f32 h) {
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

template <typename T>
T* VwAdd(Widget* parent, UniquePtr<T> w, Rect bounds) {
    T* raw = w.Get();
    raw->SetBounds(bounds);
    parent->AddChild(UniquePtr<Widget>(w.Release()));
    return raw;
}

struct Counts {
    int changed = 0;
    int commit = 0;
};
void OnSliderChanged(void* u, Slider*) { ++static_cast<Counts*>(u)->changed; }
void OnSliderCommit(void* u, Slider*) { ++static_cast<Counts*>(u)->commit; }

} // namespace

TEST_CASE("ValueRange: clamp, snap exactly, policies", "[ui][value]") {
    ValueRange r;
    r.min = -3.5;
    r.max = 96.5;
    r.step = 0.1;
    // A thousand steps: the value is min + k * step computed in one multiplication, never accumulated.
    for (int k = 0; k <= 1000; ++k) {
        const f64 want = r.min + static_cast<f64>(k) * r.step;
        CHECK(r.Snap(want + 0.01) == want);
        CHECK(r.Snap(want - 0.01) == want);
    }
    CHECK(r.Snap(1000) == r.min + 1000.0 * r.step); // the last grid point not above max
    CHECK(r.Snap(-100) == r.min);
    CHECK(r.Snap(std::nan("")) == r.min);
    CHECK(r.Snap(INFINITY) == r.min + 1000.0 * r.step);
    CHECK(r.Snap(-INFINITY) == r.min);

    ValueRange off_grid;
    off_grid.min = 0;
    off_grid.max = 1.05;
    off_grid.step = 0.5;
    CHECK(off_grid.Snap(2) == 1.0); // 1.05 is not on the grid: the last point is 1

    ValueRange whole;
    whole.min = 0;
    whole.max = 10;
    whole.integer = true; // no step: whole numbers
    CHECK(whole.Snap(3.4) == 3.0);
    CHECK(whole.Snap(3.6) == 4.0);
    whole.step = 5;
    CHECK(whole.Snap(7.6) == 10.0);

    ValueRange none;
    none.min = 0;
    none.max = 1;
    CHECK(none.Snap(0.123456) == 0.123456); // no step: clamp only
    CHECK(none.Snap(2) == 1);

    ValueRange reject;
    reject.min = 0;
    reject.max = 10;
    reject.policy = RangePolicy::Reject;
    CHECK(reject.Apply(5, 2) == 5);
    CHECK(reject.Apply(11, 2) == 2);
    CHECK(reject.Apply(-1, 2) == 2);
    CHECK(reject.Apply(std::nan(""), 2) == 2);
    reject.policy = RangePolicy::Clamp;
    CHECK(reject.Apply(11, 2) == 10);
}

TEST_CASE("Slider: drag, jump, page, keys, wheel and commit", "[ui][value]") {
    Harness h(300, 200, 1, StringView(UI_TEST_FONT_FILE));
    Slider* s = VwAdd(h.GetSurface().Root(), MakeUnique<Slider>(StringView("s")), VwRect(10, 10, 200, 24));
    ValueRange range;
    range.min = 0;
    range.max = 100;
    range.step = 1;
    s->SetRange(range);
    h.Advance(0);
    Counts counts;
    s->OnChanged().Connect(OnSliderChanged, &counts);
    s->OnCommit().Connect(OnSliderCommit, &counts);
    CHECK(s->MeasureTotal(Constraints()).w > 100);

    // A press on the track jumps there: x = 10 + 8 + 92 puts the thumb's center at 110, the middle of the track.
    h.PointerMove(110, 20);
    h.PointerDown(110, 20);
    CHECK(s->Value() == 50);
    CHECK(counts.changed == 1);
    h.PointerMove(110 + 46, 20); // drag a quarter of the track
    CHECK(s->Value() == 75);
    h.PointerMove(500, 400); // far outside: clamps to the end
    CHECK(s->Value() == 100);
    h.PointerMove(-500, 20);
    CHECK(s->Value() == 0);
    CHECK(counts.commit == 0);
    h.PointerUp(-500, 20);
    CHECK(counts.commit == 1);
    h.PointerMove(110, 20); // no capture any more
    CHECK(s->Value() == 0);

    // A press on the thumb keeps the grab offset: no jump.
    s->SetValue(40);
    const Rect thumb = s->ThumbRect();
    h.PointerMove(thumb.x + 2, 20);
    h.PointerDown(thumb.x + 2, 20);
    CHECK(s->Value() == 40);
    h.PointerMove(thumb.x + 2 + 18.4f, 20); // ten steps
    CHECK(s->Value() == 50);
    h.PointerUp(thumb.x + 2 + 18.4f, 20);

    // Keys.
    h.GetSurface().SetFocus(s);
    s->SetValue(50);
    counts = Counts();
    h.Key(Key::Right);
    CHECK(s->Value() == 51);
    h.Key(Key::Left);
    h.Key(Key::Left);
    CHECK(s->Value() == 49);
    h.Key(Key::PageUp);
    CHECK(s->Value() == 59);
    h.Key(Key::PageDown);
    h.Key(Key::PageDown);
    CHECK(s->Value() == 39);
    h.Key(Key::Home);
    CHECK(s->Value() == 0);
    h.Key(Key::End);
    CHECK(s->Value() == 100);
    CHECK(counts.commit == 8);
    h.Key(Key::End); // no change, no OnChanged
    CHECK(counts.changed == 8);

    // The wheel is off until asked for.
    h.Wheel(110, 20, 0, -1);
    CHECK(s->Value() == 100);
    s->SetWheel(true);
    h.Wheel(110, 20, 0, 1);
    CHECK(s->Value() == 99);

    SECTION("track presses page when asked") {
        s->SetValue(50);
        s->SetTrackPages(true);
        h.PointerMove(200, 20);
        h.PointerDown(200, 20);
        h.PointerUp(200, 20);
        CHECK(s->Value() == 60);
        h.PointerMove(20, 20);
        h.PointerDown(20, 20);
        h.PointerUp(20, 20);
        CHECK(s->Value() == 50);
    }
    SECTION("a value outside the range is refused under Reject") {
        range.policy = RangePolicy::Reject;
        s->SetRange(range);
        s->SetValue(500);
        CHECK(s->Value() == 99);
        s->SetValue(30);
        s->SetValue(500);
        CHECK(s->Value() == 30);
    }
    SECTION("ticks draw marks") {
        RecordingCanvas before;
        s->Draw(before);
        s->SetTicks(4);
        RecordingCanvas after;
        s->Draw(after);
        CHECK(after.Calls().Size() == before.Calls().Size() + 5);
    }
    SECTION("disabled ignores input") {
        s->SetEnabled(false);
        h.Key(Key::Home);
        h.Click(20, 20);
        CHECK(s->Value() == 99);
    }
}

TEST_CASE("Slider: vertical puts the minimum at the bottom, no step moves by a hundredth", "[ui][value]") {
    Harness h(100, 300, 1, StringView(UI_TEST_FONT_FILE));
    Slider* s = VwAdd(h.GetSurface().Root(), MakeUnique<Slider>(StringView("v"), true), VwRect(10, 10, 24, 200));
    h.Advance(0);
    CHECK(s->MeasureTotal(Constraints()).h > s->MeasureTotal(Constraints()).w);
    s->SetValue(0);
    CHECK(s->ThumbRect().y > 150); // at the bottom
    s->SetValue(1);
    CHECK(s->ThumbRect().y < 20); // at the top
    h.PointerMove(20, 10 + 8); // thumb centered at the top of the track
    h.PointerDown(20, 10 + 8);
    CHECK(s->Value() == 1);
    h.PointerMove(20, 10 + 8 + 92);
    CHECK(std::fabs(s->Value() - 0.5) < 0.01);
    h.PointerUp(20, 10 + 8 + 92);
    h.GetSurface().SetFocus(s);
    const f64 before = s->Value();
    h.Key(Key::Up);
    CHECK(std::fabs(s->Value() - (before + 0.01)) < 1e-9);
}

TEST_CASE("SpinBox: buttons, wheel, keys, range, decimals and policy", "[ui][value]") {
    Harness h(200, 100, 1, StringView(UI_TEST_FONT_FILE));
    SpinBox* box = VwAdd(h.GetSurface().Root(), MakeUnique<SpinBox>(StringView("spin")), VwRect(10, 10, 100, 30));
    h.Advance(0);
    int changes = 0;
    box->OnChanged().Connect([](void* u, SpinBox*) { ++*static_cast<int*>(u); }, &changes);
    CHECK(box->Value() == 0);
    CHECK(box->MeasureTotal(Constraints()).w > 20);

    h.Click(100, 15); // the upper button
    CHECK(box->Value() == 1);
    h.Click(100, 35); // the lower button
    h.Click(100, 35);
    CHECK(box->Value() == 0); // the minimum stops it
    CHECK(changes == 3 - 1);
    h.Wheel(30, 20, 0, -1);
    CHECK(box->Value() == 1);
    h.GetSurface().SetFocus(box);
    h.Key(Key::PageUp);
    CHECK(box->Value() == 11);
    h.Key(Key::End);
    CHECK(box->Value() == 100);
    h.Key(Key::Up);
    CHECK(box->Value() == 100);
    h.Key(Key::Home);
    CHECK(box->Value() == 0);
    CHECK(box->ValueText().View() == StringView("0"));

    ValueRange range;
    range.min = 0;
    range.max = 2;
    range.step = 0.25;
    box->SetRange(range);
    box->SetDecimals(2);
    h.Key(Key::Up);
    CHECK(box->Value() == 0.25);
    CHECK(box->ValueText().View() == StringView("0.25"));
    box->SetValue(1.9);
    CHECK(box->Value() == 2.0);

    range.policy = RangePolicy::Reject;
    box->SetRange(range);
    box->SetValue(5);
    CHECK(box->Value() == 2.0);
    box->SetValue(-5);
    CHECK(box->Value() == 2.0);
    // Draws its text and buttons without a layout pass.
    RecordingCanvas c;
    box->Draw(c);
    usize triangles = 0;
    bool text = false;
    for (usize i = 0; i < c.Calls().Size(); ++i) {
        triangles += c.Calls()[i].kind == CanvasCallKind::Triangle;
        text = text || c.Calls()[i].kind == CanvasCallKind::Text;
    }
    CHECK(triangles == 2);
    CHECK(text);
}
