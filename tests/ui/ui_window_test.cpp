#include <catch2/catch_test_macros.hpp>

#include "platform/window.h"
#include "ui/surface.h"
#include "ui/widget.h"
#include "ui_window/window_adapter.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

// A window that replays queued events and records the cursor calls.
class FakeWindow final : public os::Window
{
public:
    bool Valid() const override { return true; }
    void PumpEvents() override {}
    bool PollEvent(os::InputEvent* out) override
    {
        if (next_ >= events.Size())
        {
            return false;
        }
        *out = events[next_++];
        return true;
    }
    bool ShouldClose() const override { return false; }
    u32 Width() const override { return width; }
    u32 Height() const override { return height; }
    f32 ContentScale() const override { return content_scale; }
    const char* BackendName() const override { return "fake"; }
    os::NativeWindowInfo GetNativeInfo() const override { return os::NativeWindowInfo(); }
    bool IsMinimized() const override { return false; }
    u64 ResizeSerial() const override { return 0; }
    bool SupportsCursorShape() const override { return true; }
    bool SetCursor(os::CursorShape shape) override
    {
        cursor_calls.Push(shape);
        return accept_cursor;
    }
    Vector<os::InputEvent> events;
    Vector<os::CursorShape> cursor_calls;
    bool accept_cursor = true;
    u32 width = 0;
    u32 height = 0;
    f32 content_scale = 1.0f;

private:
    usize next_ = 0;
};

class WinBox : public Widget
{
public:
    WinBox(StringView name, Rect bounds) : Widget(name) { SetBounds(bounds); }
    Vector<Event> got;
    void OnEvent(Event& e) override { got.Push(e); }
};

os::InputEvent WinPointer(os::InputEventType type, f64 x, f64 y)
{
    os::InputEvent e;
    e.type = type;
    e.x = x;
    e.y = y;
    return e;
}

}  // namespace

TEST_CASE("window adapter: pointer positions are divided by the scale", "[window]")
{
    Surface s(100, 100, 2);
    Rect r;
    r.w = 100;
    r.h = 100;
    WinBox* box = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("box"), r)));
    FakeWindow w;
    w.events.Push(WinPointer(os::InputEventType::PointerEnter, 40, 60));
    w.events.Push(WinPointer(os::InputEventType::PointerMove, 100, 50));
    os::InputEvent down = WinPointer(os::InputEventType::PointerButton, 100, 50);
    down.down = true;
    down.button = os::kMouseLeft;
    w.events.Push(down);
    down.down = false;
    w.events.Push(down);
    os::InputEvent wheel = WinPointer(os::InputEventType::Wheel, 1, -2);
    w.events.Push(wheel);

    WindowAdapter a;
    CHECK(a.PumpInto(w, s) == 5);
    s.Frame(0);
    REQUIRE(box->got.Size() >= 4);
    CHECK(s.Hover() == box);
    // enter at (20,30), move to (50,25)
    bool saw_move = false;
    bool saw_wheel = false;
    for (usize i = 0; i < box->got.Size(); ++i)
    {
        const Event& e = box->got[i];
        if (e.type == EventType::PointerMove && e.position.x == 50 && e.position.y == 25)
        {
            saw_move = true;
        }
        if (e.type == EventType::Wheel)
        {
            saw_wheel = true;
            CHECK(e.wheel_x == 1);
            CHECK(e.wheel_y == -2);
        }
        if (e.type == EventType::PointerDown)
        {
            CHECK(e.button == os::kMouseLeft);
            CHECK(e.position.x == 50);
        }
    }
    CHECK(saw_move);
    CHECK(saw_wheel);
}

TEST_CASE("window adapter: keys, text, repeat and modifiers arrive in order", "[window]")
{
    Surface s(100, 100, 1);
    WinBox* box = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("box"), Rect())));
    box->SetFocusable(true);
    box->SetAcceptsText(true);
    s.SetFocus(box);
    FakeWindow w;
    os::InputEvent k;
    k.type = os::InputEventType::KeyDown;
    k.key = Key::B;
    k.repeat = true;
    k.modifiers = os::kModShift;
    w.events.Push(k);
    os::InputEvent t;
    t.type = os::InputEventType::Text;
    t.text_length = 2;
    t.text[0] = static_cast<char>(0xC3);
    t.text[1] = static_cast<char>(0xA9);
    w.events.Push(t);
    k.type = os::InputEventType::KeyUp;
    k.repeat = false;
    w.events.Push(k);

    WindowAdapter a;
    a.PumpInto(w, s);
    s.Frame(0);
    REQUIRE(box->got.Size() == 3 + 1);  // FocusGained from SetFocus is first
    CHECK(box->got[1].type == EventType::KeyDown);
    CHECK(box->got[1].repeat);
    CHECK(box->got[1].key == Key::B);
    CHECK(box->got[1].modifiers == os::kModShift);
    CHECK(box->got[2].type == EventType::Text);
    CHECK(box->got[2].text_length == 2);
    CHECK(static_cast<u8>(box->got[2].text[1]) == 0xA9);
    CHECK(box->got[3].type == EventType::KeyUp);
}

TEST_CASE("window adapter: resize is forwarded in logical units and costs one layout", "[window]")
{
    Surface s(100, 100, 2);
    s.Frame(0);
    const u64 before = s.LayoutPasses();
    FakeWindow w;
    for (u32 i = 1; i <= 5; ++i)
    {
        os::InputEvent e;
        e.type = os::InputEventType::Resize;
        e.width = 200 + i * 20;
        e.height = 120;
        w.events.Push(e);
    }
    WindowAdapter a;
    CHECK(a.PumpInto(w, s) == 0);
    CHECK(s.Width() == 150);
    CHECK(s.Height() == 60);
    CHECK(s.Scale() == 2);
    s.Frame(0);
    CHECK(s.LayoutPasses() == before + 1);
}

TEST_CASE("window adapter: the surface follows the window's content scale only when asked", "[window]")
{
    FakeWindow w;
    w.width = 1280;
    w.height = 720;
    w.content_scale = 2.0f;
    Surface s(1280, 720, 1);
    WindowAdapter a;
    a.PumpInto(w, s);
    CHECK(s.Scale() == 1);  // off by default: an explicit scale is kept
    CHECK(s.Width() == 1280);

    a.SetFollowWindowScale(true);
    w.events.Push(WinPointer(os::InputEventType::PointerMove, 200, 100));
    Rect r;
    r.w = 640;
    r.h = 360;
    WinBox* box = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("box"), r)));
    a.PumpInto(w, s);
    CHECK(s.Scale() == 2);
    CHECK(s.Width() == 640);
    CHECK(s.Height() == 360);
    s.Frame(0);
    REQUIRE(box->got.Size() >= 1);
    CHECK(box->got[box->got.Size() - 1].position.x == 100);  // the pointer of the same pump uses the new scale
    CHECK(box->got[box->got.Size() - 1].position.y == 50);

    // Moved to a 150% monitor: the window's size and scale change together, the widgets keep their logical size.
    w.width = 1920;
    w.height = 1080;
    w.content_scale = 1.5f;
    os::InputEvent resize;
    resize.type = os::InputEventType::Resize;
    resize.width = 1920;
    resize.height = 1080;
    w.events.Push(resize);
    a.PumpInto(w, s);
    CHECK(s.Scale() == 1.5f);
    CHECK(s.Width() == 1280);
    CHECK(s.Height() == 720);

    // A minimized window (no size) changes nothing.
    w.width = 0;
    w.height = 0;
    w.content_scale = 1.0f;
    a.PumpInto(w, s);
    CHECK(s.Scale() == 1.5f);
}

TEST_CASE("window adapter: focus and pointer leave reach the surface", "[window]")
{
    Surface s(100, 100, 1);
    Rect r;
    r.w = 100;
    r.h = 100;
    WinBox* box = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("box"), r)));
    FakeWindow w;
    WindowAdapter a;
    w.events.Push(WinPointer(os::InputEventType::PointerMove, 10, 10));
    a.PumpInto(w, s);
    s.Frame(0);
    REQUIRE(s.Hover() == box);
    os::InputEvent leave;
    leave.type = os::InputEventType::PointerLeave;
    os::InputEvent lost;
    lost.type = os::InputEventType::FocusLost;
    w.events.Clear();
    w.events.Push(leave);
    w.events.Push(lost);
    // The fake replays from its cursor: build a fresh one.
    FakeWindow w2;
    w2.events.Push(leave);
    w2.events.Push(lost);
    a.PumpInto(w2, s);
    s.Frame(0);
    CHECK(s.Hover() == nullptr);
    CHECK(!s.WindowFocused());
    os::InputEvent gained;
    gained.type = os::InputEventType::FocusGained;
    FakeWindow w3;
    w3.events.Push(gained);
    a.PumpInto(w3, s);
    s.Frame(0);
    CHECK(s.WindowFocused());
}

TEST_CASE("window adapter: SetCursor is called only when the shape changes", "[window]")
{
    Surface s(100, 100, 1);
    Rect r;
    r.w = 100;
    r.h = 100;
    WinBox* box = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("box"), r)));
    box->SetCursor(os::CursorShape::IBeam);
    FakeWindow w;
    WindowAdapter a;
    CHECK(!a.UpdateCursor(w, s));  // Arrow is what a window starts with
    CHECK(w.cursor_calls.Size() == 0);

    Event move;
    move.type = EventType::PointerMove;
    move.position = Point(5, 5);
    s.Post(move);
    s.Frame(0);
    CHECK(a.UpdateCursor(w, s));
    CHECK(!a.UpdateCursor(w, s));
    s.Post(move);
    s.Frame(0);
    CHECK(!a.UpdateCursor(w, s));
    REQUIRE(w.cursor_calls.Size() == 1);
    CHECK(w.cursor_calls[0] == os::CursorShape::IBeam);
    CHECK(a.CursorChanges() == 1);

    box->SetCursor(os::CursorShape::Hand);
    CHECK(a.UpdateCursor(w, s));
    CHECK(w.cursor_calls.Size() == 2);

    SECTION("a window that refuses the shape is asked again")
    {
        w.accept_cursor = false;
        box->SetCursor(os::CursorShape::NotAllowed);
        CHECK(a.UpdateCursor(w, s));
        CHECK(a.UpdateCursor(w, s));
        CHECK(a.CursorChanges() == 2);
    }
}

TEST_CASE("window adapter: the wheel turns where the pointer last was", "[window]")
{
    Surface s(200, 100, 2);
    WinBox* left = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("left"), Rect())));
    Rect right_bounds;
    right_bounds.x = 100;
    right_bounds.w = 100;
    right_bounds.h = 100;
    WinBox* right = static_cast<WinBox*>(s.Root()->AddChild(MakeUnique<WinBox>(StringView("right"), right_bounds)));
    Rect left_bounds;
    left_bounds.w = 100;
    left_bounds.h = 100;
    left->SetBounds(left_bounds);
    FakeWindow w;
    w.events.Push(WinPointer(os::InputEventType::PointerMove, 300, 40));  // 150 logical: over the right box
    w.events.Push(WinPointer(os::InputEventType::Wheel, 0, 1));
    WindowAdapter a;
    a.PumpInto(w, s);
    s.Frame(0);
    usize wheels = 0;
    for (usize i = 0; i < right->got.Size(); ++i)
    {
        wheels += right->got[i].type == EventType::Wheel;
    }
    CHECK(wheels == 1);
    CHECK(left->got.Size() == 0);
}
