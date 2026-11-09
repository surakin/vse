#include <catch2/catch_test_macros.hpp>

#include "ui/canvas.h"
#include "ui/draw_list.h"
#include "ui/draw_list_canvas.h"
#include "ui/testing/recording_canvas.h"
#include "lang/scalar.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace {

// A small scene drawn only through the abstract interface (what a widget does).
void DrawScene(Canvas& c) {
    c.DrawRect({10, 10, 100, 50}, 0xFF0000FFu);
    c.DrawRoundedRect({20, 20, 80, 30}, CornerRadii(6), 0xFF00FF00u);
    c.DrawBorder({20, 20, 80, 30}, CornerRadii(6), 1, 0xFFFFFFFFu);
    c.PushClip({0, 0, 60, 60});
    c.DrawLine({0, 0}, {50, 40}, 2, 0xFF123456u);
    c.DrawTriangle({5, 5}, {25, 5}, {15, 25}, 0xFF654321u);
    c.PopClip();
    c.DrawImage({30, 30, 16, 16}, 7, {0, 0, 1, 1}, 0xFFFFFFFFu);
    c.DrawShadow({40, 40, 60, 30}, 4, 8, 0x80000000u);
    c.BeginLayer(3);
    c.DrawRect({0, 0, 20, 20}, 0xFFABCDEFu);
    c.EndLayer();
}

} // namespace

TEST_CASE("Canvas: a thickness is whole pixels after scaling, never 1.25", "[ui][canvas]") {
    RecordingCanvas at1(1.0f), at125(1.25f), at2(2.0f);
    CHECK(at1.SnapLength(1) == 1.0f);
    CHECK(at125.SnapLength(1) == 1.0f);
    CHECK(at2.SnapLength(1) == 2.0f);
    CHECK(at125.SnapLength(2) == 3.0f); // 2.5 rounds up
    CHECK(at1.SnapLength(0.1f) == 1.0f); // a visible thickness never vanishes
    CHECK(at1.SnapLength(0) == 0.0f);
    CHECK(at1.SnapLength(-3) == 0.0f);
    CHECK(at125.ToPixels(8) == 10.0f); // radii are scaled, not rounded
}

TEST_CASE("Canvas: rectangles round their edges so neighbours share them", "[ui][canvas]") {
    for (f32 scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        RecordingCanvas c(scale);
        const Rect a = c.SnapRect({0, 0, 10.4f, 5});
        const Rect b = c.SnapRect({10.4f, 0, 10, 5});
        CHECK(a.Right() == b.x); // no gap and no overlap
        CHECK(a.x == Floor(a.x));
        CHECK(a.w == Floor(a.w));
    }
    RecordingCanvas c(1.0f);
    CHECK(c.SnapRect({0, 0, 0.2f, 0.2f}).w == 1.0f); // thin but present
    CHECK(c.SnapRect({5, 5, 0, 10}).Empty());
}

TEST_CASE("Canvas: RecordingCanvas records the calls with clip depth and layer", "[ui][canvas][recording]") {
    RecordingCanvas c;
    DrawScene(c);
    const Vector<CanvasCall>& calls = c.Calls();
    REQUIRE(calls.Size() == 8);
    CHECK(calls[0].kind == CanvasCallKind::Rect);
    CHECK(calls[0].color == 0xFF0000FFu);
    CHECK(calls[0].rect.w == 100);
    CHECK(calls[1].kind == CanvasCallKind::RoundedRect);
    CHECK(calls[1].radii.top_left == 6);
    CHECK(calls[2].kind == CanvasCallKind::Border);
    CHECK(calls[2].width == 1);
    CHECK(calls[3].kind == CanvasCallKind::Line);
    CHECK(calls[3].clip_depth == 1);
    CHECK(calls[4].kind == CanvasCallKind::Triangle);
    CHECK(calls[4].p2.y == 25);
    CHECK(calls[5].kind == CanvasCallKind::Image);
    CHECK(calls[5].image == 7);
    CHECK(calls[5].clip_depth == 0);
    CHECK(calls[6].kind == CanvasCallKind::Shadow);
    CHECK(calls[6].width == 8);
    CHECK(calls[7].layer == 3);
    c.Clear();
    CHECK(c.Calls().Empty());
}

TEST_CASE("Canvas: a layer starts unclipped in the recording canvas too", "[ui][canvas][recording]") {
    RecordingCanvas c;
    c.PushClip({0, 0, 5, 5});
    c.BeginLayer(1);
    c.DrawRect({0, 0, 1, 1}, 0xFFFFFFFFu);
    c.PushClip({0, 0, 2, 2});
    c.DrawRect({0, 0, 1, 1}, 0xFFFFFFFFu);
    c.PopClip();
    c.EndLayer();
    c.DrawRect({0, 0, 1, 1}, 0xFFFFFFFFu);
    REQUIRE(c.Calls().Size() == 3);
    CHECK(c.Calls()[0].clip_depth == 0);
    CHECK(c.Calls()[1].clip_depth == 1);
    CHECK(c.Calls()[2].clip_depth == 1); // the outer layer's clip came back
}

TEST_CASE("Canvas: the same scene through both canvases gives matching records", "[ui][canvas][recording]") {
    RecordingCanvas recording(2.0f);
    DrawScene(recording);
    DrawList list;
    DrawListCanvas canvas(&list, nullptr);
    canvas.Begin(2.0f, 400, 300);
    DrawScene(canvas);
    list.SortLayers();
    CHECK(recording.Scale() == canvas.Scale());
    // Eight calls, one of them (the line, clipped to 0..60 logical) and the triangle inside the clip: every call made primitives, none was culled.
    CHECK(list.stats.culled == 0);
    // Seven quads and one triangle: 7 * 4 + 3 vertices, 7 * 6 + 3 indices.
    CHECK(list.vertices.Size() == 31);
    CHECK(list.indices.Size() == 45);
    CHECK(list.commands[list.commands.Size() - 1].layer == 3); // the layer call came last in the records and in the sorted commands
    CHECK(recording.Calls()[7].layer == 3);
}
