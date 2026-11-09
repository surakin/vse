#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface_input.h"
#include "ui/testing/harness.h"
#include "ui/update_policy.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/text_field.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

SurfaceQuad SiQuad(f32 cx, f32 cy, f32 cz, f32 w, f32 h) {
    // A quad in the plane z = cz facing +z: u to the right (+x), v down (-y); (cx, cy) is its top-left corner.
    SurfaceQuad q;
    q.corner[0] = cx;
    q.corner[1] = cy;
    q.corner[2] = cz;
    q.edge_u[0] = w;
    q.edge_u[1] = 0;
    q.edge_u[2] = 0;
    q.edge_v[0] = 0;
    q.edge_v[1] = -h;
    q.edge_v[2] = 0;
    return q;
}

Ray3 SiRay(f32 ox, f32 oy, f32 oz, f32 dx, f32 dy, f32 dz) {
    Ray3 r;
    r.origin[0] = ox;
    r.origin[1] = oy;
    r.origin[2] = oz;
    r.direction[0] = dx;
    r.direction[1] = dy;
    r.direction[2] = dz;
    return r;
}

struct SiClicks {
    int count = 0;
};
void SiCount(void* user, Button*) { ++static_cast<SiClicks*>(user)->count; }

struct SiRig {
    SiRig(f32 scale = 1.0f) : h(200, 100, scale, StringView(UI_TEST_FONT_FILE)), input(&h.GetSurface()) {
        UniquePtr<Button> b = MakeUnique<Button>(StringView("go"), StringView("Go"));
        button = b.Get();
        button->SetBounds(Rect{20, 20, 80, 30});
        button->OnClick().Connect(SiCount, &clicks);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(b.Release()));
        h.Advance(0);
    }
    Harness h;
    SurfaceInput input;
    Button* button;
    SiClicks clicks;
};

} // namespace

TEST_CASE("RayToSurfacePoint gives the point of the quad a ray crosses", "[ui][surface-input]") {
    const SurfaceQuad quad = SiQuad(-1, 1, 0, 2, 2); // 2 x 2, centred on the origin, facing +z
    SurfaceHit hit = RayToSurfacePoint(SiRay(0, 0, 5, 0, 0, -1), quad);
    REQUIRE(hit.hit);
    CHECK(std::fabs(hit.u - 0.5f) < 1e-5f);
    CHECK(std::fabs(hit.v - 0.5f) < 1e-5f);
    CHECK(std::fabs(hit.t - 5.0f) < 1e-5f);
    hit = RayToSurfacePoint(SiRay(-0.5f, 0.5f, 3, 0, 0, -2), quad); // the direction need not be a unit vector
    REQUIRE(hit.hit);
    CHECK(std::fabs(hit.u - 0.25f) < 1e-5f);
    CHECK(std::fabs(hit.v - 0.25f) < 1e-5f);
    CHECK(std::fabs(hit.t - 1.5f) < 1e-5f);
    hit = RayToSurfacePoint(SiRay(1, -1, 5, 0, 0, -1), quad); // the bottom-right corner
    REQUIRE(hit.hit);
    CHECK(std::fabs(hit.u - 1.0f) < 1e-5f);
    CHECK(std::fabs(hit.v - 1.0f) < 1e-5f);
}

TEST_CASE("RayToSurfacePoint at an angle, with a skewed quad, and the misses", "[ui][surface-input]") {
    const SurfaceQuad quad = SiQuad(-1, 1, 0, 2, 2);
    // A ray from the side that crosses the quad's centre.
    SurfaceHit hit = RayToSurfacePoint(SiRay(4, 0, 4, -1, 0, -1), quad);
    REQUIRE(hit.hit);
    CHECK(std::fabs(hit.u - 0.5f) < 1e-4f); // the line reaches z = 0 at x = 0: the centre
    CHECK(std::fabs(hit.v - 0.5f) < 1e-4f);
    CHECK(std::fabs(hit.t - 4.0f) < 1e-4f);
    // Misses: outside the edges, behind the ray, parallel, a zero direction.
    CHECK_FALSE(RayToSurfacePoint(SiRay(2, 0, 5, 0, 0, -1), quad).hit);
    CHECK_FALSE(RayToSurfacePoint(SiRay(0, 0, 5, 0, 0, 1), quad).hit);
    CHECK_FALSE(RayToSurfacePoint(SiRay(0, 0, 5, 1, 0, 0), quad).hit);
    CHECK_FALSE(RayToSurfacePoint(SiRay(0, 0, 5, 0, 0, 0), quad).hit);
    // A parallelogram: the top edge is sheared to the right as it goes down.
    SurfaceQuad skew = quad;
    skew.edge_v[0] = 1.0f;
    hit = RayToSurfacePoint(SiRay(0.5f, 0, 5, 0, 0, -1), skew); // the point at u = 0.5, v = 0.5: x = -1 + 1 + 0.5 = 0.5, y = 0
    REQUIRE(hit.hit);
    CHECK(std::fabs(hit.u - 0.5f) < 1e-4f);
    CHECK(std::fabs(hit.v - 0.5f) < 1e-4f);
    // A degenerate quad hits nothing.
    SurfaceQuad flat = quad;
    flat.edge_v[0] = flat.edge_u[0];
    flat.edge_v[1] = 0;
    CHECK_FALSE(RayToSurfacePoint(SiRay(0, 0, 5, 0, 0, -1), flat).hit);
}

TEST_CASE("RayToSurfacePoint reports a ray from behind as a back face, not a hit", "[ui][surface-input]") {
    const SurfaceQuad quad = SiQuad(-1, 1, 0, 2, 2);
    const SurfaceHit back = RayToSurfacePoint(SiRay(0, 0, -5, 0, 0, 1), quad);
    CHECK_FALSE(back.hit);
    CHECK(back.back_face);
    CHECK(std::fabs(back.t - 5.0f) < 1e-5f);
    const SurfaceHit away = RayToSurfacePoint(SiRay(0, 0, -5, 0, 0, -1), quad); // behind and pointing away
    CHECK_FALSE(away.hit);
    CHECK_FALSE(away.back_face);
    const SurfaceHit outside = RayToSurfacePoint(SiRay(3, 0, -5, 0, 0, 1), quad);
    CHECK_FALSE(outside.back_face);
}

TEST_CASE("a surface without input ignores everything, an interactive one reacts", "[ui][surface-input]") {
    SiRig rig;
    CHECK_FALSE(rig.input.Interactive());
    rig.input.PointerMove(30, 30);
    rig.input.PointerButton(30, 30, os::kMouseLeft, true);
    rig.input.PointerButton(30, 30, os::kMouseLeft, false);
    rig.h.Advance(0.016f);
    CHECK(rig.clicks.count == 0);
    CHECK(rig.h.GetSurface().Hover() == nullptr);
    rig.input.SetInteractive(true);
    rig.input.PointerMove(30, 30);
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().Hover() == rig.button);
    rig.input.PointerButton(30, 30, os::kMouseLeft, true);
    rig.h.Advance(0.016f);
    CHECK((rig.button->State() & kStatePressed) != 0);
    rig.input.PointerButton(30, 30, os::kMouseLeft, false);
    rig.h.Advance(0.016f);
    CHECK(rig.clicks.count == 1);
}

TEST_CASE("pointer positions are in surface pixels or as a fraction of the surface", "[ui][surface-input]") {
    SiRig rig(2.0f); // 200 x 100 logical, 400 x 200 pixels
    rig.input.SetInteractive(true);
    rig.input.PointerMove(60, 60); // pixels: logical (30, 30), on the button
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().Hover() == rig.button);
    rig.input.PointerMove(300, 150);
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().Hover() != rig.button);
    rig.input.PointerButtonUv(0.15f, 0.35f, os::kMouseLeft, true); // logical (30, 35)
    rig.input.PointerButtonUv(0.15f, 0.35f, os::kMouseLeft, false);
    rig.h.Advance(0.016f);
    CHECK(rig.clicks.count == 1);
}

TEST_CASE("losing the pointer releases the buttons held and clears the hover", "[ui][surface-input]") {
    SiRig rig;
    rig.input.SetInteractive(true);
    rig.input.PointerButton(30, 30, os::kMouseLeft, true);
    rig.h.Advance(0.016f);
    CHECK(rig.input.ButtonsDown() == os::kMouseLeft);
    CHECK(rig.h.GetSurface().Capture() == rig.button);
    rig.input.Lost();
    rig.h.Advance(0.016f);
    CHECK(rig.input.ButtonsDown() == 0);
    CHECK(rig.h.GetSurface().Capture() == nullptr);
    CHECK(rig.h.GetSurface().Hover() == nullptr);
    CHECK((rig.button->State() & kStatePressed) == 0);
    // Turning input off does the same.
    rig.input.PointerButton(30, 30, os::kMouseLeft, true);
    rig.h.Advance(0.016f);
    rig.input.SetInteractive(false);
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().Capture() == nullptr);
}

TEST_CASE("keys and text reach the focus owner of a surface", "[ui][surface-input]") {
    SiRig rig;
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
    TextField* field = f.Get();
    field->SetBounds(Rect{20, 60, 120, 24});
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    rig.h.Advance(0);
    rig.input.SetInteractive(true);
    rig.input.Focus(true);
    rig.input.PointerButton(30, 70, os::kMouseLeft, true);
    rig.input.PointerButton(30, 70, os::kMouseLeft, false);
    rig.input.Text(StringView("hi"));
    rig.h.Advance(0.016f);
    CHECK(field->Text() == StringView("hi"));
    rig.input.Key(Key::Backspace, true);
    rig.input.Key(Key::Backspace, false);
    rig.h.Advance(0.016f);
    CHECK(field->Text() == StringView("h"));
}

TEST_CASE("two surfaces keep their own theme, scale and state", "[ui][surface-input]") {
    SiRig light(1.0f);
    SiRig dark(2.0f);
    dark.h.GetSurface().SetTheme(&DefaultDarkTheme());
    light.h.GetSurface().SetTheme(&DefaultLightTheme());
    light.h.Advance(0.016f);
    dark.h.Advance(0.016f);
    const ResolvedStyle a = light.h.GetSurface().Themes().Resolve(widget_kind::kPanel, VariantId(0), 0, 1);
    const ResolvedStyle b = dark.h.GetSurface().Themes().Resolve(widget_kind::kPanel, VariantId(0), 0, 2);
    CHECK(a.background != b.background);
    light.input.SetInteractive(true);
    dark.input.SetInteractive(true);
    light.input.PointerButton(30, 30, os::kMouseLeft, true);
    light.input.PointerButton(30, 30, os::kMouseLeft, false);
    light.h.Advance(0.016f);
    dark.h.Advance(0.016f);
    CHECK(light.clicks.count == 1);
    CHECK(dark.clicks.count == 0);
    CHECK(light.h.GetSurface().Scale() == 1.0f);
    CHECK(dark.h.GetSurface().Scale() == 2.0f);
}

TEST_CASE("an on-demand surface runs no frame while idle and one when something happens", "[ui][surface-input][policy]") {
    SiRig rig;
    SurfaceUpdater updater(&rig.h.GetSurface());
    updater.SetPolicy(UpdatePolicy::OnDemand);
    DrawList list;
    UpdateResult first = updater.Frame(0.016f, &list, &rig.h.Atlas());
    CHECK(first.ran); // the first frame builds the picture
    CHECK(first.rebuilt);
    for (int i = 0; i < 100; ++i) {
        const UpdateResult r = updater.Frame(0.016f, &list, &rig.h.Atlas());
        CHECK_FALSE(r.ran);
    }
    CHECK(updater.FramesRun() == 1);
    CHECK(updater.FramesSkipped() == 100);
    rig.input.SetInteractive(true);
    rig.input.PointerMove(30, 30);
    const UpdateResult moved = updater.Frame(0.016f, &list, &rig.h.Atlas());
    CHECK(moved.ran);
    rig.input.PointerButton(30, 30, os::kMouseLeft, true);
    const UpdateResult pressed = updater.Frame(0.016f, &list, &rig.h.Atlas());
    CHECK(pressed.ran);
    CHECK(pressed.rebuilt); // the pressed look changed the picture
    // Idle again.
    updater.Frame(0.016f, &list, &rig.h.Atlas());
    CHECK_FALSE(updater.Frame(0.016f, &list, &rig.h.Atlas()).ran);
}

TEST_CASE("an on-demand surface keeps running while a caret blinks or a tooltip waits", "[ui][surface-input][policy]") {
    SiRig rig;
    UniquePtr<TextField> f = MakeUnique<TextField>(StringView("field"));
    TextField* field = f.Get();
    field->SetBounds(Rect{20, 60, 120, 24});
    rig.h.GetSurface().Root()->AddChild(UniquePtr<Widget>(f.Release()));
    rig.h.Advance(0.016f);
    CHECK_FALSE(rig.h.GetSurface().NeedsFrame());
    rig.input.SetInteractive(true);
    rig.input.Focus(true);
    rig.input.PointerButton(30, 70, os::kMouseLeft, true);
    rig.input.PointerButton(30, 70, os::kMouseLeft, false);
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().NeedsFrame()); // the focused field blinks its caret
    rig.button->SetTooltip(StringView("tip"));
    rig.h.Advance(0.016f);
    rig.input.PointerMove(30, 30);
    rig.h.Advance(0.016f);
    CHECK(rig.h.GetSurface().NeedsFrame());
    // Once the tooltip has shown there is nothing left to wait for (the focused field still blinks, so take the focus away first).
    rig.h.GetSurface().SetFocus(nullptr);
    for (int i = 0; i < 40 && !rig.h.GetSurface().TooltipShown(); ++i) rig.h.Advance(0.1f);
    REQUIRE(rig.h.GetSurface().TooltipShown());
    rig.h.Advance(0.1f);
    rig.h.Advance(0.1f);
    CHECK_FALSE(rig.h.GetSurface().NeedsFrame());
}

TEST_CASE("a fixed-rate surface runs at its rate whatever the caller's frame rate", "[ui][surface-input][policy]") {
    SiRig rig;
    SurfaceUpdater updater(&rig.h.GetSurface());
    updater.SetPolicy(UpdatePolicy::FixedRate, 10.0f); // every 0.1 s
    DrawList list;
    u32 runs = 0;
    for (int i = 0; i < 100; ++i) { // one second at 100 calls a second (a fake clock)
        if (updater.Frame(0.01f, &list, &rig.h.Atlas()).ran) ++runs;
    }
    CHECK(runs >= 9);
    CHECK(runs <= 10);
    runs = 0;
    for (int i = 0; i < 20; ++i) { // slower than the rate: every call runs
        if (updater.Frame(0.25f, &list, &rig.h.Atlas()).ran) ++runs;
    }
    CHECK(runs == 20);
}

TEST_CASE("skipped time is handed to the next frame that runs", "[ui][surface-input][policy]") {
    SiRig rig;
    SurfaceUpdater updater(&rig.h.GetSurface());
    updater.SetPolicy(UpdatePolicy::OnDemand);
    DrawList list;
    updater.Frame(0.0f, &list, &rig.h.Atlas());
    const f64 before = rig.h.GetSurface().Time();
    for (int i = 0; i < 10; ++i) updater.Frame(0.5f, &list, &rig.h.Atlas());
    CHECK(rig.h.GetSurface().Time() == before);
    rig.input.SetInteractive(true);
    rig.input.PointerMove(30, 30);
    updater.Frame(0.5f, &list, &rig.h.Atlas());
    CHECK(std::fabs(rig.h.GetSurface().Time() - before - 5.5) < 1e-4);
}
