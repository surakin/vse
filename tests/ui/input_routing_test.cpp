#include <catch2/catch_test_macros.hpp>

#include "platform/input.h"
#include "ui/surface.h"
#include "ui/widget.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

// A rectangle that logs the events it gets and can handle chosen kinds.
class RouteBox : public Widget
{
public:
    RouteBox(StringView name, Rect bounds) : Widget(name) { SetBounds(bounds); }
    Vector<EventType> log;
    bool handle_wheel = false;
    bool handle_keys = false;
    f32 last_wheel_x = 0;
    f32 last_wheel_y = 0;
    void OnEvent(Event& e) override
    {
        log.Push(e.type);
        if (e.type == EventType::Wheel)
        {
            last_wheel_x = e.wheel_x;
            last_wheel_y = e.wheel_y;
            if (handle_wheel)
            {
                e.handled = true;
            }
        }
        if ((e.type == EventType::KeyDown || e.type == EventType::KeyUp) && handle_keys)
        {
            e.handled = true;
        }
    }
    usize Count(EventType t) const
    {
        usize n = 0;
        for (usize i = 0; i < log.Size(); ++i)
        {
            n += log[i] == t;
        }
        return n;
    }
};

Event RoutePointer(EventType type, f32 x, f32 y, u32 button = 0)
{
    Event e;
    e.type = type;
    e.position = Point(x, y);
    e.button = button;
    return e;
}

Event RouteKey(Key key, u32 modifiers = 0)
{
    Event e;
    e.type = EventType::KeyDown;
    e.key = key;
    e.modifiers = modifiers;
    return e;
}

RouteBox* AddRouteBox(Widget* parent, StringView name, Rect r)
{
    return static_cast<RouteBox*>(parent->AddChild(MakeUnique<RouteBox>(name, r)));
}

Rect R(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

}  // namespace

TEST_CASE("input routing: the popup layer is hit before the tree, and enter and leave follow the topmost widget",
          "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* below = AddRouteBox(s.Root(), "below", R(0, 0, 200, 100));
    RouteBox* above = AddRouteBox(s.Root(), "above", R(50, 0, 100, 100));
    RouteBox* popup = AddRouteBox(s.PopupLayer(), "popup", R(60, 10, 30, 30));

    s.Dispatch(RoutePointer(EventType::PointerMove, 10, 10));
    CHECK(s.Hover() == below);
    s.Dispatch(RoutePointer(EventType::PointerMove, 55, 50));
    CHECK(s.Hover() == above);  // the later sibling is on top
    CHECK(below->Count(EventType::PointerLeave) == 1);
    s.Dispatch(RoutePointer(EventType::PointerMove, 70, 20));
    CHECK(s.Hover() == popup);
    CHECK(above->Count(EventType::PointerLeave) == 1);
    CHECK(popup->Count(EventType::PointerEnter) == 1);
}

TEST_CASE("input routing: the widget under a stationary pointer is found again after layout changes", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* back = AddRouteBox(s.Root(), "back", R(0, 0, 200, 100));
    RouteBox* front = AddRouteBox(s.Root(), "front", R(0, 0, 100, 100));
    s.Frame(0);
    s.Dispatch(RoutePointer(EventType::PointerMove, 50, 50));
    s.Frame(0);
    REQUIRE(s.Hover() == front);

    front->SetBounds(R(120, 0, 80, 100));  // moves away from under the pointer
    s.Frame(0);
    CHECK(s.Hover() == back);
    CHECK(front->Count(EventType::PointerLeave) == 1);
    CHECK(back->Count(EventType::PointerEnter) == 1);

    front->SetBounds(R(0, 0, 100, 100));  // and back
    s.Frame(0);
    CHECK(s.Hover() == front);

    front->SetVisible(false);
    s.Frame(0);
    CHECK(s.Hover() == back);

    s.Post(RoutePointer(
        EventType::PointerLeave, 0,
        0));  // the pointer left the surface: nothing is hovered, and a layout change does not bring a hover back
    s.Frame(0);
    CHECK(s.Hover() == nullptr);
    front->SetVisible(true);
    s.Frame(0);
    CHECK(s.Hover() == nullptr);
}

TEST_CASE(
    "input routing: a press captures the pointer through a drag outside the surface, and the release reaches the capturer",
    "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* a = AddRouteBox(s.Root(), "a", R(0, 0, 100, 100));
    RouteBox* b = AddRouteBox(s.Root(), "b", R(100, 0, 100, 100));
    s.Dispatch(RoutePointer(EventType::PointerDown, 10, 10, os::kMouseLeft));
    CHECK(s.Capture() == a);
    CHECK((a->State() & kStatePressed) != 0);
    s.Dispatch(RoutePointer(EventType::PointerMove, 150, 10));    // over b
    s.Dispatch(RoutePointer(EventType::PointerMove, -500, 900));  // far outside the surface
    CHECK(a->Count(EventType::PointerMove) == 2);
    CHECK(b->Count(EventType::PointerMove) == 0);
    s.Dispatch(RoutePointer(EventType::PointerUp, -500, 900, os::kMouseLeft));
    CHECK(a->Count(EventType::PointerUp) == 1);
    CHECK(s.Capture() == nullptr);
    CHECK((a->State() & kStatePressed) == 0);

    SECTION("a second button keeps the capture until the last one is up")
    {
        s.Dispatch(RoutePointer(EventType::PointerDown, 10, 10, os::kMouseLeft));
        s.Dispatch(RoutePointer(EventType::PointerDown, 150, 10, os::kMouseRight));
        CHECK(s.Capture() == a);
        s.Dispatch(RoutePointer(EventType::PointerUp, 150, 10, os::kMouseLeft));
        CHECK(s.Capture() == a);
        s.Dispatch(RoutePointer(EventType::PointerUp, 150, 10, os::kMouseRight));
        CHECK(s.Capture() == nullptr);
    }
    SECTION("RequestCapture and ReleaseCapture")
    {
        b->RequestCapture();
        CHECK(s.Capture() == b);
        s.Dispatch(RoutePointer(EventType::PointerMove, 10, 10));
        CHECK(b->Count(EventType::PointerMove) == 1);
        a->ReleaseCapture();  // not the holder: nothing changes
        CHECK(s.Capture() == b);
        b->ReleaseCapture();
        CHECK(s.Capture() == nullptr);
    }
}

TEST_CASE("input routing: the wheel bubbles to the first handler, never to a widget behind a popup", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* scroll = AddRouteBox(s.Root(), "scroll", R(0, 0, 200, 100));
    RouteBox* inner = AddRouteBox(scroll, "inner", R(0, 0, 100, 100));
    RouteBox* popup = AddRouteBox(s.PopupLayer(), "popup", R(10, 10, 40, 40));
    scroll->handle_wheel = true;

    Event w = RoutePointer(EventType::Wheel, 150, 50);
    w.wheel_y = -2;
    s.Dispatch(w);
    CHECK(scroll->Count(EventType::Wheel) == 1);  // over the scroll view itself
    w.position = Point(60, 50);
    w.wheel_x = 3;
    w.wheel_y = 0;
    s.Dispatch(w);
    CHECK(inner->Count(EventType::Wheel) == 1);   // the inner widget saw it first, did not handle it
    CHECK(scroll->Count(EventType::Wheel) == 2);  // and it bubbled to the scroll view
    CHECK(scroll->last_wheel_x == 3);
    CHECK(scroll->last_wheel_y == 0);

    w.position = Point(20, 20);  // under the popup
    s.Dispatch(w);
    CHECK(popup->Count(EventType::Wheel) == 1);
    CHECK(scroll->Count(EventType::Wheel) ==
          2);  // the popup layer is not inside the root: the tree behind the popup did not see it
}

namespace
{

RouteBox* AddStop(Widget* parent, StringView name, i32 tab_index = 0)
{
    RouteBox* b = AddRouteBox(parent, name, R(0, 0, 10, 10));
    b->SetFocusable(true);
    b->SetTabIndex(tab_index);
    return b;
}

}  // namespace

TEST_CASE("input routing: Tab walks the tab order, skipping disabled and hidden widgets, and wraps both ways",
          "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* a = AddStop(s.Root(), "a");
    RouteBox* b = AddStop(s.Root(), "b");
    RouteBox* c = AddStop(s.Root(), "c");
    RouteBox* d = AddStop(s.Root(), "d");
    b->SetEnabled(false);
    c->SetVisible(false);
    AddRouteBox(s.Root(), "plain", R(0, 0, 10, 10));  // not focusable

    s.Dispatch(RouteKey(Key::Tab));
    CHECK(s.Focus() == a);  // with no focus, forward starts at the first
    s.Dispatch(RouteKey(Key::Tab));
    CHECK(s.Focus() == d);
    s.Dispatch(RouteKey(Key::Tab));
    CHECK(s.Focus() == a);  // wrapped
    s.Dispatch(RouteKey(Key::Tab, os::kModShift));
    CHECK(s.Focus() == d);  // shift-Tab wraps backwards
    s.Dispatch(RouteKey(Key::Tab, os::kModShift));
    CHECK(s.Focus() == a);
    CHECK(a->Count(EventType::FocusLost) == 2);

    SECTION("a widget that handles Tab keeps it")
    {
        a->handle_keys = true;
        s.Dispatch(RouteKey(Key::Tab));
        CHECK(s.Focus() == a);
    }
    SECTION("Ctrl+Tab is left to the program")
    {
        s.Dispatch(RouteKey(Key::Tab, os::kModCtrl));
        CHECK(s.Focus() == a);
    }
    SECTION("an unhandled key bubbles to the ancestors")
    {
        s.Root()->SetFocusable(false);
        RouteBox* outer = AddRouteBox(s.Root(), "outer", R(0, 0, 50, 50));
        RouteBox* inner = AddStop(outer, "inner");
        outer->handle_keys = true;
        s.SetFocus(inner);
        s.Dispatch(RouteKey(Key::A));
        CHECK(inner->Count(EventType::KeyDown) == 1);
        CHECK(outer->Count(EventType::KeyDown) == 1);
    }
}

TEST_CASE("input routing: tab_index orders the stops, zero follows the tree, negative is skipped", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* first_in_tree = AddStop(s.Root(), "t0a");
    RouteBox* three = AddStop(s.Root(), "t3", 3);
    RouteBox* skipped = AddStop(s.Root(), "neg", -1);
    RouteBox* one = AddStop(s.Root(), "t1", 1);
    RouteBox* second_in_tree = AddStop(s.Root(), "t0b");
    RouteBox* also_three = AddStop(s.Root(), "t3b", 3);

    Widget* expected[] = {one, three, also_three, first_in_tree, second_in_tree, one};
    for (Widget* w : expected)
    {
        s.Dispatch(RouteKey(Key::Tab));
        CHECK(s.Focus() == w);
    }
    // A click still focuses the widget that Tab skips.
    skipped->SetBounds(R(100, 0, 50, 50));
    s.Dispatch(RoutePointer(EventType::PointerDown, 120, 10, os::kMouseLeft));
    s.Dispatch(RoutePointer(EventType::PointerUp, 120, 10, os::kMouseLeft));
    CHECK(s.Focus() == skipped);
}

TEST_CASE(
    "input routing: a click focuses the nearest focusable widget, and a click on nothing focusable clears the focus",
    "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* field = AddStop(s.Root(), "field");
    field->SetBounds(R(0, 0, 100, 100));
    RouteBox* label = AddRouteBox(field, "label",
                                  R(10, 10, 20, 20));  // not focusable: the click goes to its focusable parent
    RouteBox* off = AddStop(s.Root(), "off");
    off->SetBounds(R(120, 0, 50, 50));
    off->SetEnabled(false);
    AddRouteBox(s.Root(), "bg", R(100, 50, 100, 50));

    s.Dispatch(RoutePointer(EventType::PointerDown, 15, 15, os::kMouseLeft));
    s.Dispatch(RoutePointer(EventType::PointerUp, 15, 15, os::kMouseLeft));
    CHECK(s.Focus() == field);
    CHECK(label->Count(EventType::PointerDown) == 1);
    s.Dispatch(
        RoutePointer(EventType::PointerDown, 130, 10, os::kMouseLeft));  // a disabled widget does not take the focus
    s.Dispatch(RoutePointer(EventType::PointerUp, 130, 10, os::kMouseLeft));
    CHECK(s.Focus() == nullptr);
    CHECK(field->Count(EventType::FocusLost) == 1);
}

TEST_CASE("input routing: text goes only to a focus owner that accepts it, and repeat and modifiers are carried",
          "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* button = AddStop(s.Root(), "button");
    RouteBox* field = AddStop(s.Root(), "field");
    field->SetAcceptsText(true);
    Event t;
    t.type = EventType::Text;
    t.text_length = 1;
    t.text[0] = 'x';

    s.SetFocus(button);
    s.Dispatch(t);
    CHECK(button->Count(EventType::Text) == 0);
    s.SetFocus(field);
    s.Dispatch(t);
    CHECK(field->Count(EventType::Text) == 1);

    Event k = RouteKey(Key::A, os::kModCtrl);
    k.repeat = true;
    s.Dispatch(k);
    CHECK(s.Modifiers() == os::kModCtrl);
    CHECK(field->Count(EventType::KeyDown) == 1);
}

TEST_CASE(
    "input routing: window focus loss releases the capture, closes popups, resets modifiers and tells the focus owner",
    "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* a = AddStop(s.Root(), "a");
    a->SetBounds(R(0, 0, 100, 100));
    AddRouteBox(s.PopupLayer(), "menu", R(120, 0, 40, 40));
    s.SetFocus(a);
    s.Dispatch(RoutePointer(EventType::PointerDown, 10, 10, os::kMouseLeft));
    Event key = RouteKey(Key::LeftCtrl, os::kModCtrl);
    s.Dispatch(key);
    REQUIRE(s.Capture() == a);
    REQUIRE(s.Modifiers() == os::kModCtrl);

    Event lost;
    lost.type = EventType::WindowFocusLost;
    s.Dispatch(lost);
    CHECK(!s.WindowFocused());
    CHECK(s.Capture() == nullptr);
    CHECK((a->State() & kStatePressed) == 0);
    CHECK(s.Modifiers() == 0);
    CHECK(s.PopupLayer()->ChildCount() == 0);
    CHECK(s.Hover() == nullptr);
    CHECK(a->Count(EventType::FocusLost) == 1);
    CHECK(s.Focus() == a);  // the keyboard focus is kept for when the window comes back
    s.Dispatch(RouteKey(Key::A));
    CHECK(a->Count(EventType::KeyDown) ==
          1);  // only the Ctrl from before: keys do not reach a surface whose window has no focus

    Event gained;
    gained.type = EventType::WindowFocusGained;
    s.Dispatch(gained);
    CHECK(s.WindowFocused());
    CHECK(a->Count(EventType::FocusGained) == 2);
    CHECK((a->State() & kStateFocused) != 0);
}

TEST_CASE("input routing: closing the last popup gives the focus back, and Tab stays inside an open popup", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* main_field = AddStop(s.Root(), "main");
    AddStop(s.Root(), "other");
    s.SetFocus(main_field);
    s.Frame(0);

    Widget* menu = s.PopupLayer()->AddChild(MakeUnique<Widget>(StringView("menu")));
    menu->SetBounds(R(100, 0, 80, 80));
    RouteBox* item1 = AddStop(menu, "item1");
    RouteBox* item2 = AddStop(menu, "item2");
    s.Frame(0);
    s.Dispatch(RouteKey(Key::Tab));  // from a focus outside the popup: the first stop inside it
    CHECK(s.Focus() == item1);
    s.Dispatch(RouteKey(Key::Tab));
    CHECK(s.Focus() == item2);
    s.Dispatch(RouteKey(Key::Tab));
    CHECK(s.Focus() == item1);

    s.PopupLayer()->RemoveChild(menu);
    s.Frame(0);
    CHECK(s.Focus() == main_field);
}

TEST_CASE("input routing: the cursor follows the hovered widget, its ancestors and the capture", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* panel = AddRouteBox(s.Root(), "panel", R(0, 0, 100, 100));
    RouteBox* text = AddRouteBox(panel, "text", R(10, 10, 30, 30));
    RouteBox* grip = AddRouteBox(s.Root(), "grip", R(100, 0, 100, 100));
    panel->SetCursor(os::CursorShape::Hand);
    text->SetCursor(os::CursorShape::IBeam);
    grip->SetCursor(os::CursorShape::ResizeHorizontal);

    CHECK(s.RequestedCursor() == os::CursorShape::Arrow);
    s.Dispatch(RoutePointer(EventType::PointerMove, 20, 20));
    CHECK(s.RequestedCursor() == os::CursorShape::IBeam);
    s.Dispatch(RoutePointer(EventType::PointerMove, 80, 80));
    CHECK(s.RequestedCursor() == os::CursorShape::Hand);
    text->ClearCursor();
    s.Dispatch(RoutePointer(EventType::PointerMove, 20, 20));
    CHECK(s.RequestedCursor() == os::CursorShape::Hand);  // the inner widget names none: the parent's applies

    s.Dispatch(RoutePointer(EventType::PointerDown, 150, 50, os::kMouseLeft));
    s.Dispatch(RoutePointer(EventType::PointerMove, 20, 20));  // dragging over the panel: the grip keeps the cursor
    CHECK(s.RequestedCursor() == os::CursorShape::ResizeHorizontal);
    s.Dispatch(RoutePointer(EventType::PointerUp, 20, 20, os::kMouseLeft));
    CHECK(s.RequestedCursor() == os::CursorShape::Hand);
}

TEST_CASE("input routing: a focus given while the window has no focus shows no focus state until it returns", "[input]")
{
    Surface s(200, 100, 1);
    RouteBox* a = AddStop(s.Root(), "a");
    Event lost;
    lost.type = EventType::WindowFocusLost;
    s.Dispatch(lost);
    s.SetFocus(a);
    CHECK((a->State() & kStateFocused) == 0);
    Event gained;
    gained.type = EventType::WindowFocusGained;
    s.Dispatch(gained);
    CHECK((a->State() & kStateFocused) != 0);
}
