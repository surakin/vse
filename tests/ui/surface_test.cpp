#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "ui/testing/software_raster.h"
#include "ui/widget.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

constexpr u32 kRed = 0xFF0000FFu;
constexpr u32 kGreen = 0xFF00FF00u;
constexpr u32 kBlue = 0xFFFF0000u;

// Fills its bounds and logs events.
class Box : public Widget {
public:
    Box(StringView name, Rect bounds, u32 color) : Widget(name), color_(color) { SetBounds(bounds); }
    Vector<EventType> log;
    Vector<Point> positions;
    bool handle = false;
    f32 updates = 0;
    bool wants_update = false;
    void Draw(Canvas& c) override { c.DrawRect(Bounds(), color_); }
    void OnEvent(Event& e) override {
        log.Push(e.type);
        positions.Push(e.position);
        if (handle) e.handled = true;
    }
    void Update(f32 dt) override { updates += dt; }
    bool WantsUpdate() const override { return wants_update; }
    usize Count(EventType t) const {
        usize n = 0;
        for (usize i = 0; i < log.Size(); ++i) n += log[i] == t;
        return n;
    }

private:
    u32 color_;
};

Box* AddBox(Widget* parent, StringView name, Rect bounds, u32 color = kRed) {
    UniquePtr<Box> b = MakeUnique<Box>(name, bounds, color);
    Box* raw = b.Get();
    parent->AddChild(UniquePtr<Widget>(b.Release()));
    return raw;
}

bool PixelIs(const RasterImage& img, u32 x, u32 y, u32 abgr) {
    const u8* p = img.Pixel(x, y);
    return p[0] == (abgr & 0xFF) && p[1] == ((abgr >> 8) & 0xFF) && p[2] == ((abgr >> 16) & 0xFF);
}

} // namespace

TEST_CASE("Surface: runs frames with no window object at all", "[ui][surface]") {
    Harness h(100, 80, 1, StringView(UI_TEST_FONT_FILE));
    REQUIRE(h.Valid());
    CHECK(h.GetSurface().LayoutPasses() == 0);
    CHECK(h.GetSurface().NeedsRedraw());
    AddBox(h.GetSurface().Root(), "box", {10, 10, 20, 20});
    CHECK(h.Advance(0.016f)); // the first frame builds the list
    CHECK(h.GetSurface().LayoutPasses() == 1);
    CHECK_FALSE(h.GetSurface().NeedsRedraw());
    CHECK(h.List().commands.Size() == 1);
    CHECK_FALSE(h.Advance(0.016f)); // nothing changed: the list is kept
    CHECK(h.GetSurface().LayoutPasses() == 1);
    CHECK(h.GetSurface().Time() > 0.03);
    h.Find("box")->MarkDirty(kDrawDirty);
    CHECK(h.GetSurface().NeedsRedraw());
    CHECK(h.Advance(0));
    // An empty list is rebuilt every frame (as the spec says), a logic-only frame with no list never builds.
    Harness empty(10, 10, 1, StringView(UI_TEST_FONT_FILE));
    CHECK(empty.Advance(0));
    CHECK(empty.Advance(0));
    CHECK_FALSE(empty.GetSurface().Frame(0));
}

TEST_CASE("Surface: pointer events hover, capture, bubble and arrive in local space", "[ui][surface]") {
    Harness h(200, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* panel = AddBox(h.GetSurface().Root(), "panel", {50, 20, 100, 60});
    Box* leaf = AddBox(panel, "leaf", {60, 30, 20, 20});
    h.PointerMove(65, 35);
    CHECK(h.GetSurface().Hover() == leaf);
    CHECK((leaf->State() & kStateHover) != 0);
    CHECK(leaf->Count(EventType::PointerEnter) == 1);
    CHECK(leaf->positions[leaf->positions.Size() - 1].x == 5); // local space: 65 - 60
    CHECK(leaf->positions[leaf->positions.Size() - 1].y == 5);
    // Unhandled events bubble; the parent sees them in its own local space.
    CHECK(panel->Count(EventType::PointerMove) == 1);
    CHECK(panel->positions[panel->positions.Size() - 1].x == 15);
    CHECK(panel->Count(EventType::PointerEnter) == 0); // enter and leave do not bubble
    leaf->handle = true;
    h.PointerMove(66, 36);
    CHECK(panel->Count(EventType::PointerMove) == 1); // handled: it stopped at the leaf
    // Moving to the panel changes the hover.
    h.PointerMove(120, 70);
    CHECK(h.GetSurface().Hover() == panel);
    CHECK(leaf->Count(EventType::PointerLeave) == 1);
    CHECK((leaf->State() & kStateHover) == 0);
    h.PointerMove(5, 5);
    CHECK(h.GetSurface().Hover() == h.GetSurface().Root());
    // Press captures, so a drag outside still reaches the widget and the release too.
    h.PointerDown(65, 35);
    CHECK(h.GetSurface().Capture() == leaf);
    CHECK((leaf->State() & kStatePressed) != 0);
    h.PointerMove(190, 95);
    CHECK(leaf->Count(EventType::PointerMove) >= 3);
    CHECK(leaf->positions[leaf->positions.Size() - 1].x == 130);
    h.PointerUp(190, 95);
    CHECK(leaf->Count(EventType::PointerUp) == 1);
    CHECK(h.GetSurface().Capture() == nullptr);
    CHECK((leaf->State() & kStatePressed) == 0);
    CHECK(h.GetSurface().Hover() == h.GetSurface().Root()); // hover follows the pointer after the release
    // The wheel goes to the widget under the pointer.
    h.Wheel(65, 35, 0, 2);
    CHECK(leaf->Count(EventType::Wheel) == 1);
}

TEST_CASE("Surface: a pointer leave posted to the surface clears the hover", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* a = AddBox(h.GetSurface().Root(), "a", {0, 0, 50, 50});
    h.PointerMove(10, 10);
    CHECK(h.GetSurface().Hover() == a);
    Event leave;
    leave.type = EventType::PointerLeave;
    h.GetSurface().Post(leave);
    h.Advance(0);
    CHECK(h.GetSurface().Hover() == nullptr);
    CHECK(a->Count(EventType::PointerLeave) == 1);
    CHECK((a->State() & kStateHover) == 0);
}

TEST_CASE("Surface: two buttons keep the capture until the last one is up", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* a = AddBox(h.GetSurface().Root(), "a", {0, 0, 50, 50});
    h.PointerDown(10, 10, 1);
    h.PointerDown(10, 10, 2);
    h.PointerUp(10, 10, 1);
    CHECK(h.GetSurface().Capture() == a);
    h.PointerUp(10, 10, 2);
    CHECK(h.GetSurface().Capture() == nullptr);
}

TEST_CASE("Surface: keys and text go to the focus owner, and focus changes are events", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* a = AddBox(h.GetSurface().Root(), "a", {0, 0, 50, 50});
    Box* b = AddBox(h.GetSurface().Root(), "b", {50, 0, 50, 50});
    h.Key(Key::A);
    h.TypeText("x");
    CHECK(a->log.Size() == 0); // nobody has focus: nothing is delivered
    h.GetSurface().SetFocus(a);
    CHECK(a->Count(EventType::FocusGained) == 1);
    CHECK((a->State() & kStateFocused) != 0);
    h.Key(Key::A);
    h.TypeText("hé");
    CHECK(a->Count(EventType::KeyDown) == 1);
    CHECK(a->Count(EventType::KeyUp) == 1);
    CHECK(a->Count(EventType::Text) == 2);
    h.GetSurface().SetFocus(b);
    CHECK(a->Count(EventType::FocusLost) == 1);
    CHECK((a->State() & kStateFocused) == 0);
    CHECK(b->Count(EventType::FocusGained) == 1);
    h.Key(Key::B, false);
    CHECK(b->Count(EventType::KeyDown) == 1);
    CHECK(a->Count(EventType::KeyDown) == 1);
    h.GetSurface().SetFocus(nullptr);
    CHECK(b->Count(EventType::FocusLost) == 1);
    CHECK(h.GetSurface().Focus() == nullptr);
}

TEST_CASE("Surface: a disabled widget takes events and does nothing with them", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* behind = AddBox(h.GetSurface().Root(), "behind", {0, 0, 100, 100});
    Box* a = AddBox(h.GetSurface().Root(), "a", {0, 0, 50, 50});
    a->SetEnabled(false);
    h.PointerMove(10, 10);
    h.PointerDown(10, 10);
    h.PointerUp(10, 10);
    CHECK(a->Count(EventType::PointerDown) == 0);
    CHECK(behind->Count(EventType::PointerDown) == 0); // not passed on
    CHECK((a->State() & kStateDisabled) != 0);
}

TEST_CASE("Surface: widgets that want updates get dt each frame, others do not", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* ticker = AddBox(h.GetSurface().Root(), "ticker", {0, 0, 10, 10});
    Box* idle = AddBox(h.GetSurface().Root(), "idle", {20, 0, 10, 10});
    Box* hidden = AddBox(h.GetSurface().Root(), "hidden", {40, 0, 10, 10});
    ticker->wants_update = true;
    hidden->wants_update = true;
    hidden->SetVisible(false);
    h.Advance(0.25f);
    h.Advance(0.5f);
    CHECK(ticker->updates == 0.75f);
    CHECK(idle->updates == 0);
    CHECK(hidden->updates == 0);
    CHECK(h.GetSurface().Time() == 0.75);
}

TEST_CASE("Surface: popups are hit first and drawn last, resize and theme changes dirty it", "[ui][surface]") {
    Harness h(100, 100, 1, StringView(UI_TEST_FONT_FILE));
    Box* base = AddBox(h.GetSurface().Root(), "base", {0, 0, 100, 100}, kRed);
    Box* popup = AddBox(h.GetSurface().PopupLayer(), "popup", {20, 20, 30, 30}, kBlue);
    h.Advance(0);
    h.PointerMove(25, 25);
    CHECK(h.GetSurface().Hover() == popup);
    h.PointerMove(80, 80);
    CHECK(h.GetSurface().Hover() == base);
    RasterImage img;
    h.Render(&img);
    CHECK(PixelIs(img, 25, 25, kBlue)); // the popup is over the base
    CHECK(PixelIs(img, 80, 80, kRed));
    CHECK(h.Find("popup") == popup);

    const u64 passes = h.GetSurface().LayoutPasses();
    h.GetSurface().Resize(120, 90, 2);
    CHECK(h.GetSurface().NeedsRedraw());
    h.Advance(0);
    CHECK(h.GetSurface().LayoutPasses() == passes + 1);
    CHECK(h.GetSurface().Root()->Bounds().w == 120);
    CHECK(h.GetSurface().Scale() == 2);

    ThemeBuilder colors;
    colors.SetColor(ColorId::Accent, 0xFF123456u);
    const Theme color_theme = colors.Build();
    h.GetSurface().SetTheme(&color_theme);
    CHECK(h.GetSurface().NeedsRedraw());
    h.Advance(0);
    CHECK(h.GetSurface().LayoutPasses() == passes + 1); // colors do not move layout
    ThemeBuilder metrics;
    metrics.SetMetric(MetricId::Padding, 30);
    const Theme metric_theme = metrics.Build();
    h.GetSurface().SetTheme(&metric_theme);
    h.Advance(0);
    CHECK(h.GetSurface().LayoutPasses() == passes + 2); // a metric did
    CHECK(h.GetSurface().Themes().Revision() == 2);
}

TEST_CASE("Software raster: rectangles land on the right pixels, at scale 2 too", "[ui][surface][raster]") {
    Harness h(40, 30, 2, StringView(UI_TEST_FONT_FILE));
    AddBox(h.GetSurface().Root(), "box", {10, 5, 12, 8}, kGreen);
    h.Advance(0);
    RasterImage img;
    h.Render(&img, 0xFF000000u);
    CHECK(img.width == 80);
    CHECK(img.height == 60);
    u32 inside = 0, outside = 0;
    for (u32 y = 0; y < img.height; ++y) {
        for (u32 x = 0; x < img.width; ++x) {
            const bool in = x >= 20 && x < 44 && y >= 10 && y < 26;
            if (PixelIs(img, x, y, kGreen)) {
                if (in) ++inside; else ++outside;
            } else if (in) {
                ++outside;
            }
        }
    }
    CHECK(inside == 24 * 16);
    CHECK(outside == 0);
}

TEST_CASE("Software raster: a translucent rectangle is blended once (no double draw on the diagonal)", "[ui][surface][raster]") {
    Harness h(20, 20, 1, StringView(UI_TEST_FONT_FILE));
    AddBox(h.GetSurface().Root(), "square", {2, 2, 16, 16}, 0x80FFFFFFu); // half-transparent white on black
    h.Advance(0);
    RasterImage img;
    h.Render(&img, 0xFF000000u);
    const u8 first = img.Pixel(2, 2)[0];
    CHECK(first >= 126);
    CHECK(first <= 129);
    for (u32 y = 2; y < 18; ++y) {
        for (u32 x = 2; x < 18; ++x) CHECK(img.Pixel(x, y)[0] == first); // the diagonal pixels are not brighter
    }
    CHECK(img.Pixel(1, 1)[0] == 0);
    CHECK(img.Pixel(18, 18)[0] == 0);
}

TEST_CASE("Software raster: rounded corners are anti-aliased, clips and layers apply, text leaves ink", "[ui][surface][raster]") {
    Harness h(60, 40, 1, StringView(UI_TEST_FONT_FILE));
    class Scene : public Widget {
    public:
        Scene() : Widget("scene") {}
        text::TextLayout layout;
        text::TextStyle style;
        void Draw(Canvas& c) override {
            c.DrawRoundedRect({5, 5, 30, 30}, CornerRadii(10), kGreen);
            c.PushClip({40, 0, 10, 10});
            c.DrawRect({30, 0, 30, 30}, kBlue); // only x 40..50, y 0..10 shows
            c.PopClip();
            c.BeginLayer(1);
            c.DrawRect({0, 30, 10, 10}, kRed);
            c.EndLayer();
            c.DrawRect({0, 30, 10, 10}, kGreen); // drawn later but under the layer-1 rectangle
            c.DrawText(layout, style, {36, 20}, 0xFFFFFFFFu);
        }
    };
    UniquePtr<Scene> scene = MakeUnique<Scene>();
    ui_test::Opened font;
    scene->style.font = &font.font;
    scene->style.pixel_size = 14;
    REQUIRE(text::Layout(StringView("Hi"), scene->style, text::LayoutOptions{}, &scene->layout).Ok());
    h.GetSurface().Root()->AddChild(UniquePtr<Widget>(scene.Release()));
    h.Advance(0);
    CHECK(h.List().stats.glyphs_dropped == 0);
    RasterImage img;
    h.Render(&img, 0xFF000000u);
    CHECK(PixelIs(img, 20, 20, kGreen));   // the middle of the rounded rect
    CHECK(PixelIs(img, 5, 5, 0xFF000000u)); // the sharp corner is cut away
    u32 blended = 0;
    for (u32 y = 5; y < 15; ++y)
        for (u32 x = 5; x < 15; ++x) {
            const u8* p = img.Pixel(x, y);
            if (p[1] != 0 && p[1] != 255) ++blended;
        }
    CHECK(blended > 4);
    CHECK(PixelIs(img, 45, 5, kBlue));
    CHECK(PixelIs(img, 45, 15, 0xFF000000u)); // below the clip
    CHECK(PixelIs(img, 5, 35, kRed));         // layer 1 wins over the later base-layer draw
    u32 ink = 0;
    for (u32 y = 15; y < 40; ++y)
        for (u32 x = 36; x < 60; ++x) ink += img.Pixel(x, y)[0] > 100 ? 1 : 0;
    CHECK(ink > 8);
}
