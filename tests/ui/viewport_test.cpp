#include <catch2/catch_test_macros.hpp>

#include "font_support.h"
#include "ui/surface.h"
#include "ui/testing/harness.h"
#include "scripted_window.h"
#include "ui/viewport.h"
#include "ui_window/window_adapter.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ui;

namespace
{

Rect VpRect(f32 x, f32 y, f32 w, f32 h)
{
    Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// A binding that only counts and remembers.
class FakeBinding : public ViewportBinding
{
public:
    void OnResize(u32 w, u32 h) override
    {
        ++resizes;
        width = w;
        height = h;
    }
    void Render(const ViewportFrame& f) override
    {
        ++renders;
        last = f;
    }
    ImageId Image() const override { return image; }
    void OnInput(ViewportInput& in) override
    {
        inputs.Push(in);
        if (take_keys && (in.type == ViewportInputType::KeyDown || in.type == ViewportInputType::Wheel))
        {
            in.handled = true;
        }
    }
    usize Count(ViewportInputType t) const
    {
        usize n = 0;
        for (usize i = 0; i < inputs.Size(); ++i)
        {
            n += inputs[i].type == t ? 1 : 0;
        }
        return n;
    }
    const ViewportInput* Last(ViewportInputType t) const
    {
        for (usize i = inputs.Size(); i > 0; --i)
        {
            if (inputs[i - 1].type == t)
            {
                return &inputs[i - 1];
            }
        }
        return nullptr;
    }
    u32 resizes = 0;
    u32 renders = 0;
    u32 width = 0;
    u32 height = 0;
    ViewportFrame last;
    ImageId image = 7;
    bool take_keys = false;
    Vector<ViewportInput> inputs;
};

class FakeLockHost : public PointerLockHost
{
public:
    bool LockSupported() const override { return supported; }
    bool SetLocked(bool l) override
    {
        ++calls;
        if (!supported)
        {
            return false;
        }
        locked = l;
        return true;
    }
    bool supported = true;
    bool locked = false;
    u32 calls = 0;
};

struct VpRig
{
    explicit VpRig(f32 scale = 1.0f) : h(400, 300, scale, StringView(UI_TEST_FONT_FILE))
    {
        h.GetSurface().SetPointerLockHost(&host);
        vp = Add("vp", &binding, VpRect(10, 20, 200, 100));
        h.Advance(0.016f);
    }
    Viewport* Add(StringView name, FakeBinding* b, Rect bounds)
    {
        UniquePtr<Viewport> v = MakeUnique<Viewport>(name);
        Viewport* raw = v.Get();
        raw->SetBounds(bounds);
        raw->SetBinding(b);
        h.GetSurface().Root()->AddChild(UniquePtr<Widget>(v.Release()));
        return raw;
    }
    Surface& S() { return h.GetSurface(); }
    void Rel(f32 dx, f32 dy)
    {
        Event e;
        e.type = EventType::RelativeMotion;
        e.rel_x = dx;
        e.rel_y = dy;
        S().Post(e);
        h.Advance(0);
    }
    Harness h;
    FakeBinding binding;
    FakeLockHost host;
    Viewport* vp;
};

class VpMark : public Widget
{
public:
    explicit VpMark(StringView name) : Widget(name) {}
    void OnEvent(Event& e) override
    {
        if (e.type == EventType::PointerDown)
        {
            ++downs;
            e.handled = true;
        }
    }
    void Draw(Canvas& c) override { c.DrawRoundedRect(Bounds(), CornerRadii(), 0xFF0000FFu); }
    int downs = 0;
};

}  // namespace

TEST_CASE("the binding is told the size in physical pixels once, then rendered every frame", "[ui][viewport]")
{
    VpRig rig(2.0f);
    CHECK(rig.binding.resizes == 1);
    CHECK(rig.binding.width == 400);
    CHECK(rig.binding.height == 200);
    CHECK(rig.vp->PixelWidth() == 400);
    CHECK(rig.binding.renders == 1);
    CHECK(rig.binding.last.scale == 2.0f);
    rig.h.Advance(0.5f);
    rig.h.Advance(0.5f);
    CHECK(rig.binding.renders == 3);
    CHECK(rig.binding.resizes == 1);  // the size did not change
    CHECK(rig.binding.last.delta == 0.5f);
    CHECK(rig.vp->RenderCount() == 3);
}

TEST_CASE("ten surface resizes between two frames cost one OnResize with the final size", "[ui][viewport]")
{
    VpRig rig;
    LayoutProps fill;
    fill.fill_parent = true;
    rig.vp->SetProps(fill);
    rig.h.Advance(0.016f);
    const u32 before = rig.binding.resizes;
    for (u32 i = 0; i < 10; ++i)
    {
        rig.S().Resize(400.0f + static_cast<f32>(i) * 10.0f, 300.0f + static_cast<f32>(i) * 5.0f, 1.0f);
    }
    rig.h.Advance(0.016f);
    CHECK(rig.binding.resizes == before + 1);
    CHECK(rig.binding.width == 490);
    CHECK(rig.binding.height == 345);
    rig.h.Advance(0.016f);
    CHECK(rig.binding.resizes == before + 1);
}

TEST_CASE("the image is drawn over the bounds and an empty image draws nothing", "[ui][viewport]")
{
    VpRig rig;
    bool found = false;
    for (usize i = 0; i < rig.h.List().commands.Size(); ++i)
    {
        const DrawCommand& c = rig.h.List().commands[i];
        if (c.texture.kind == TextureKind::Image && c.texture.id == 7)
        {
            found = true;
        }
    }
    CHECK(found);
    rig.binding.image = 0;
    rig.h.Advance(0.016f);
    for (usize i = 0; i < rig.h.List().commands.Size(); ++i)
    {
        CHECK(rig.h.List().commands[i].texture.kind != TextureKind::Image);
    }
    CHECK(rig.binding.renders == 2);  // still rendered: the picture may arrive next frame
}

TEST_CASE("a zero-size viewport calls nothing and keeps its size", "[ui][viewport]")
{
    VpRig rig;
    const u32 renders = rig.binding.renders;
    rig.vp->SetBounds(VpRect(10, 20, 0, 100));
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == renders);
    CHECK(rig.binding.resizes == 1);
    CHECK(rig.vp->PixelWidth() == 200);
    rig.vp->SetBounds(VpRect(10, 20, 200, 100));
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == renders + 1);
    CHECK(rig.binding.resizes == 1);  // the same size as before: the target was kept
}

TEST_CASE("a hidden viewport renders nothing and a collapsed one neither", "[ui][viewport]")
{
    VpRig rig;
    const u32 renders = rig.binding.renders;
    rig.vp->SetVisible(false);
    rig.h.Advance(0.016f);
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == renders);
    rig.vp->SetVisible(true);
    rig.vp->SetCollapsed(true);
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == renders);
    rig.vp->SetCollapsed(false);
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == renders + 1);
    CHECK(rig.binding.resizes == 1);
}

TEST_CASE("AspectOf gives width over height and 1 for an empty size", "[ui][viewport]")
{
    CHECK(AspectOf(800, 400) == 2.0f);
    CHECK(AspectOf(0, 400) == 1.0f);
    CHECK(AspectOf(800, 0) == 1.0f);
}

TEST_CASE("pointer positions reach the binding in viewport pixels", "[ui][viewport]")
{
    VpRig rig(2.0f);
    rig.h.PointerMove(15, 30);  // logical (5, 10) inside the viewport
    const ViewportInput* move = rig.binding.Last(ViewportInputType::PointerMove);
    REQUIRE(move != nullptr);
    CHECK(move->x == 10.0f);
    CHECK(move->y == 20.0f);
    CHECK(move->scale == 2.0f);
    CHECK(rig.binding.Count(ViewportInputType::Enter) == 1);
    rig.h.PointerDown(15, 30, os::kMouseLeft);
    const ViewportInput* down = rig.binding.Last(ViewportInputType::PointerDown);
    REQUIRE(down != nullptr);
    CHECK(down->button == os::kMouseLeft);
    CHECK(down->x == 10.0f);
    CHECK(rig.S().Capture() == rig.vp);
    CHECK(rig.S().Focus() == rig.vp);
    CHECK(rig.binding.Count(ViewportInputType::FocusGained) == 1);
    rig.h.PointerMove(300, 200);  // dragging outside: still delivered, in pixel space that may lie outside the viewport
    move = rig.binding.Last(ViewportInputType::PointerMove);
    CHECK(move->x == 580.0f);
    rig.h.PointerUp(300, 200, os::kMouseLeft);
    CHECK(rig.binding.Count(ViewportInputType::PointerUp) == 1);
    CHECK(rig.S().Capture() == nullptr);
    rig.h.PointerMove(399, 299);
    CHECK(rig.binding.Count(ViewportInputType::Leave) == 1);
}

TEST_CASE("wheel and key events reach the binding and stop bubbling only when it takes them", "[ui][viewport]")
{
    VpRig rig;
    rig.h.PointerMove(20, 30);
    rig.h.Wheel(20, 30, 0, 2);
    REQUIRE(rig.binding.Last(ViewportInputType::Wheel) != nullptr);
    CHECK(rig.binding.Last(ViewportInputType::Wheel)->wheel_y == 2.0f);
    rig.h.Click(20, 30);
    rig.h.Key(Key::W);
    CHECK(rig.binding.Count(ViewportInputType::KeyDown) == 1);
    CHECK(rig.binding.Count(ViewportInputType::KeyUp) == 1);
    CHECK(rig.binding.Last(ViewportInputType::KeyDown)->key == Key::W);
    rig.binding.take_keys = true;
    rig.h.Key(Key::A);
    CHECK(rig.binding.Count(ViewportInputType::KeyDown) == 2);
}

TEST_CASE("mouse look locks the pointer and reports relative motion as deltas", "[ui][viewport]")
{
    VpRig rig;
    rig.h.PointerMove(20, 30);
    rig.h.PointerDown(20, 30, os::kMouseRight);
    CHECK(rig.vp->Looking());
    CHECK(rig.vp->LookLocked());
    CHECK(rig.host.locked);
    CHECK(rig.S().PointerLockOwner() == rig.vp);
    REQUIRE(rig.binding.Last(ViewportInputType::LookBegin) != nullptr);
    CHECK(rig.binding.Last(ViewportInputType::LookBegin)->locked);
    rig.Rel(3, -4);
    const ViewportInput* look = rig.binding.Last(ViewportInputType::Look);
    REQUIRE(look != nullptr);
    CHECK(look->delta_x == 3.0f);
    CHECK(look->delta_y == -4.0f);
    CHECK(look->locked);
    rig.h.PointerMove(50, 50);  // an absolute move while locked is not reported
    CHECK(rig.binding.Count(ViewportInputType::Look) == 1);
    rig.h.PointerUp(50, 50, os::kMouseRight);
    CHECK_FALSE(rig.vp->Looking());
    CHECK_FALSE(rig.host.locked);
    CHECK(rig.S().PointerLockOwner() == nullptr);
    CHECK(rig.binding.Count(ViewportInputType::LookEnd) == 1);
    rig.Rel(1, 1);  // after the look nothing is delivered
    CHECK(rig.binding.Count(ViewportInputType::Look) == 1);
}

TEST_CASE("without a pointer lock mouse look falls back to captured absolute motion", "[ui][viewport]")
{
    VpRig rig(2.0f);
    rig.host.supported = false;
    rig.h.PointerMove(20, 30);
    rig.h.PointerDown(20, 30, os::kMouseRight);
    CHECK(rig.vp->Looking());
    CHECK_FALSE(rig.vp->LookLocked());
    CHECK_FALSE(rig.binding.Last(ViewportInputType::LookBegin)->locked);
    rig.h.PointerMove(25, 28);
    const ViewportInput* look = rig.binding.Last(ViewportInputType::Look);
    REQUIRE(look != nullptr);
    CHECK(look->delta_x == 10.0f);  // 5 logical units at scale 2
    CHECK(look->delta_y == -4.0f);
    CHECK_FALSE(look->locked);
    rig.h.PointerUp(25, 28, os::kMouseRight);
    CHECK_FALSE(rig.vp->Looking());
    CHECK(rig.binding.Count(ViewportInputType::LookEnd) == 1);
}

TEST_CASE("no look button turns mouse look off, and another button can be chosen", "[ui][viewport]")
{
    VpRig rig;
    rig.vp->SetLookButton(0);
    rig.h.PointerDown(20, 30, os::kMouseRight);
    CHECK_FALSE(rig.vp->Looking());
    rig.h.PointerUp(20, 30, os::kMouseRight);
    rig.vp->SetLookButton(os::kMouseLeft);
    rig.h.PointerDown(20, 30, os::kMouseLeft);
    CHECK(rig.vp->Looking());
    CHECK(rig.host.locked);
}

TEST_CASE("the window losing focus, a hidden viewport or a destroyed one frees the lock", "[ui][viewport]")
{
    {
        VpRig rig;
        rig.h.PointerDown(20, 30, os::kMouseRight);
        REQUIRE(rig.host.locked);
        Event lost;
        lost.type = EventType::WindowFocusLost;
        rig.S().Post(lost);
        rig.h.Advance(0.016f);
        CHECK_FALSE(rig.host.locked);
        CHECK_FALSE(rig.vp->Looking());
        CHECK(rig.binding.Count(ViewportInputType::LookEnd) == 1);
    }
    {
        VpRig rig;
        rig.h.PointerDown(20, 30, os::kMouseRight);
        REQUIRE(rig.host.locked);
        rig.vp->SetVisible(false);
        rig.h.Advance(0.016f);
        CHECK_FALSE(rig.host.locked);
        CHECK(rig.S().PointerLockOwner() == nullptr);
        rig.vp->SetVisible(true);
        rig.h.Advance(0.016f);
        CHECK_FALSE(rig.vp->Looking());
        CHECK(rig.binding.Count(ViewportInputType::LookEnd) == 1);
    }
    {
        VpRig rig;
        rig.h.PointerDown(20, 30, os::kMouseRight);
        REQUIRE(rig.host.locked);
        rig.S().Root()->RemoveChild(rig.vp);
        rig.h.Advance(0.016f);
        CHECK_FALSE(rig.host.locked);
    }
}

TEST_CASE("two viewports keep their own binding, focus and capture", "[ui][viewport]")
{
    VpRig rig;
    FakeBinding second;
    Viewport* other = rig.Add("other", &second, VpRect(220, 20, 150, 100));
    rig.h.Advance(0.016f);
    CHECK(second.resizes == 1);
    CHECK(second.width == 150);
    CHECK(rig.binding.width == 200);
    rig.h.PointerDown(230, 30, os::kMouseLeft);
    CHECK(rig.S().Focus() == other);
    CHECK(rig.S().Capture() == other);
    CHECK(second.Count(ViewportInputType::PointerDown) == 1);
    CHECK(rig.binding.Count(ViewportInputType::PointerDown) == 0);
    rig.h.PointerUp(230, 30, os::kMouseLeft);
    rig.h.Click(20, 30);
    CHECK(rig.S().Focus() == rig.vp);
    CHECK(second.Count(ViewportInputType::FocusLost) == 1);
    CHECK(rig.binding.Count(ViewportInputType::FocusGained) == 1);
    // Rendering order is tree order: both render each frame.
    const u32 a = rig.binding.renders;
    const u32 b = second.renders;
    rig.h.Advance(0.016f);
    CHECK(rig.binding.renders == a + 1);
    CHECK(second.renders == b + 1);
    // Only one holds the pointer lock at a time.
    rig.h.PointerDown(20, 30, os::kMouseRight);
    CHECK(rig.S().PointerLockOwner() == rig.vp);
    CHECK_FALSE(rig.S().RequestPointerLock(other));
    rig.h.PointerUp(20, 30, os::kMouseRight);
}

TEST_CASE("children of a viewport draw after the image and get the pointer first", "[ui][viewport]")
{
    VpRig rig;
    UniquePtr<VpMark> m = MakeUnique<VpMark>(StringView("hud"));
    VpMark* mark = m.Get();
    mark->SetBounds(VpRect(20, 30, 40, 20));
    rig.vp->AddChild(UniquePtr<Widget>(m.Release()));
    rig.h.Advance(0.016f);
    // The image command comes before the child's rectangle in the draw list.
    isize image_at = -1;
    isize rect_at = -1;
    for (usize i = 0; i < rig.h.List().commands.Size(); ++i)
    {
        const DrawCommand& c = rig.h.List().commands[i];
        if (c.texture.kind == TextureKind::Image)
        {
            image_at = static_cast<isize>(i);
        }
        else if (c.texture.kind == TextureKind::None && c.index_count > 0)
        {
            rect_at = static_cast<isize>(i);
        }
    }
    REQUIRE(image_at >= 0);
    REQUIRE(rect_at >= 0);
    CHECK(rect_at > image_at);
    rig.h.PointerDown(30, 40, os::kMouseLeft);  // on the child
    CHECK(mark->downs == 1);
    CHECK(rig.binding.Count(ViewportInputType::PointerDown) == 0);
    rig.h.PointerUp(30, 40, os::kMouseLeft);
    rig.h.PointerDown(100, 80, os::kMouseLeft);  // beside it: the viewport
    CHECK(rig.binding.Count(ViewportInputType::PointerDown) == 1);
    rig.h.PointerUp(100, 80, os::kMouseLeft);
}

TEST_CASE("a viewport without a binding draws and does nothing, and a new binding is told the size", "[ui][viewport]")
{
    VpRig rig;
    rig.vp->SetBinding(nullptr);
    rig.h.Advance(0.016f);
    rig.h.PointerDown(20, 30, os::kMouseLeft);
    CHECK(rig.binding.Count(ViewportInputType::PointerDown) == 0);
    rig.h.PointerUp(20, 30, os::kMouseLeft);
    FakeBinding next;
    rig.vp->SetBinding(&next);
    rig.h.Advance(0.016f);
    CHECK(next.resizes == 1);
    CHECK(next.width == 200);
    CHECK(next.renders == 1);
}

TEST_CASE("a window's pointer lock and relative motion drive mouse look through the adapter", "[ui][viewport]")
{
    ScriptedWindow window;
    window.width = 400;
    window.height = 300;
    WindowAdapter adapter;
    WindowPointerLock lock(&window);
    VpRig rig;
    rig.S().SetPointerLockHost(&lock);  // the window's lock instead of the fake host
    auto push = [&](os::InputEvent e) { window.events.Push(e); };
    os::InputEvent move;
    move.type = os::InputEventType::PointerMove;
    move.x = 30;
    move.y = 40;
    push(move);
    os::InputEvent down;
    down.type = os::InputEventType::PointerButton;
    down.down = true;
    down.button = os::kMouseRight;
    down.x = 30;
    down.y = 40;
    push(down);
    os::InputEvent rel;
    rel.type = os::InputEventType::RelativeMotion;
    rel.x = 6;
    rel.y = -2;
    push(rel);
    adapter.PumpInto(window, rig.S());
    rig.h.Advance(0.016f);
    CHECK(window.pointer_locked);
    CHECK(rig.vp->LookLocked());
    const ViewportInput* look = rig.binding.Last(ViewportInputType::Look);
    REQUIRE(look != nullptr);
    CHECK(look->delta_x == 6.0f);
    CHECK(look->delta_y == -2.0f);
    os::InputEvent up = down;
    up.down = false;
    push(up);
    adapter.PumpInto(window, rig.S());
    rig.h.Advance(0.016f);
    CHECK_FALSE(window.pointer_locked);
    // A window that cannot lock: mouse look runs on absolute motion.
    window.pointer_lock_supported = false;
    push(down);
    adapter.PumpInto(window, rig.S());
    rig.h.Advance(0.016f);
    CHECK(rig.vp->Looking());
    CHECK_FALSE(rig.vp->LookLocked());
    rig.S().SetPointerLockHost(nullptr);
}
