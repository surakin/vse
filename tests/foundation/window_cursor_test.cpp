#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "containers/vector.h"

#include "platform/cursor.h"
#include "platform/linux/wayland/wayland_input.h"
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/window.h"

// Day 186: the cursor value types and the pointer lock rules, which the Wayland and X11 backends share and which need no display.
namespace window_cursor_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;

namespace {

Vector<byte> Pixels(u32 w, u32 h, byte fill = 0x80) {
    Vector<byte> px;
    px.Resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < px.Size(); ++i) px[i] = fill;
    return px;
}

CursorImage Image(const Vector<byte>& px, u32 w, u32 h) {
    CursorImage image;
    image.rgba = px.Data();
    image.width = w;
    image.height = h;
    return image;
}

using Action = PointerLockState::Action;

} // namespace

TEST_CASE("A cursor image of 1x1, 32x32 and 128x128 is accepted", "[cursor][os]") {
    for (u32 size : {1u, 32u, 128u}) {
        const auto px = Pixels(size, size);
        CursorImage image = Image(px, size, size);
        image.hotspot_x = static_cast<i32>(size - 1);
        image.hotspot_y = 0;
        CHECK(ValidateCursorImage(image) == CursorImageError::Ok);
    }
}

TEST_CASE("A cursor image is refused above the size limit, with a hotspot outside, without pixels or with a bad scale", "[cursor][os]") {
    const auto big = Pixels(129, 16);
    CHECK(ValidateCursorImage(Image(big, 129, 16)) == CursorImageError::TooLarge);
    const auto tall = Pixels(16, 129);
    CHECK(ValidateCursorImage(Image(tall, 16, 129)) == CursorImageError::TooLarge);

    const auto px = Pixels(16, 16);
    CursorImage image = Image(px, 16, 16);
    image.hotspot_x = 16;
    CHECK(ValidateCursorImage(image) == CursorImageError::HotspotOutside);
    image.hotspot_x = 0;
    image.hotspot_y = -1;
    CHECK(ValidateCursorImage(image) == CursorImageError::HotspotOutside);
    image.hotspot_y = 0;
    image.scale = 0;
    CHECK(ValidateCursorImage(image) == CursorImageError::BadScale);
    image.scale = kMaxCursorImageScale + 1;
    CHECK(ValidateCursorImage(image) == CursorImageError::BadScale);
    image.scale = 2;
    CHECK(ValidateCursorImage(image) == CursorImageError::Ok);

    CursorImage none;
    none.width = 4;
    none.height = 4;
    CHECK(ValidateCursorImage(none) == CursorImageError::NoPixels);
    CursorImage empty = Image(px, 0, 4);
    CHECK(ValidateCursorImage(empty) == CursorImageError::Empty);
}

TEST_CASE("HashCursorImage tells images apart by pixels, hotspot, scale and size", "[cursor][os]") {
    auto a = Pixels(8, 8, 1);
    auto b = Pixels(8, 8, 1);
    CursorImage ia = Image(a, 8, 8);
    CursorImage ib = Image(b, 8, 8);
    CHECK(HashCursorImage(ia) == HashCursorImage(ib)); // equal contents, equal hash, whatever the pointers
    b[100] = 2;
    CHECK(HashCursorImage(ia) != HashCursorImage(ib));
    b[100] = 1;
    ib.hotspot_x = 1;
    CHECK(HashCursorImage(ia) != HashCursorImage(ib));
    ib.hotspot_x = 0;
    ib.scale = 2;
    CHECK(HashCursorImage(ia) != HashCursorImage(ib));
    const auto c = Pixels(4, 16, 1); // the same bytes in another shape
    CHECK(HashCursorImage(ia) != HashCursorImage(Image(c, 4, 16)));
}

TEST_CASE("A window without cursor support says so and refuses everything", "[cursor][os]") {
    struct Bare final : os::Window {
        bool Valid() const override { return false; }
        void PumpEvents() override {}
        bool ShouldClose() const override { return false; }
        u32 Width() const override { return 0; }
        u32 Height() const override { return 0; }
        const char* BackendName() const override { return "bare"; }
        NativeWindowInfo GetNativeInfo() const override { return {}; }
        bool IsMinimized() const override { return true; }
        u64 ResizeSerial() const override { return 0; }
    } window;
    const auto px = Pixels(2, 2);
    CHECK_FALSE(window.SupportsCursorShape());
    CHECK_FALSE(window.SetCursor(CursorShape::Hand));
    CHECK_FALSE(window.SupportsCursorImage());
    CHECK_FALSE(window.SetCursorImage(Image(px, 2, 2)));
    window.ClearCursorImage();
    CHECK_FALSE(window.SupportsPointerLock());
    CHECK_FALSE(window.SetPointerLock(true));
    CHECK_FALSE(window.PointerLocked());
}

TEST_CASE("PointerLockState: a request while focused acquires, a release releases, a repeat does nothing", "[cursor][lock][os]") {
    PointerLockState lock;
    CHECK(lock.SetFocused(true) == Action::Nothing);
    CHECK(lock.SetWanted(true) == Action::Acquire);
    CHECK(lock.Pending());
    CHECK(lock.SetWanted(true) == Action::Nothing); // a second request is a no-op
    lock.Granted();
    CHECK(lock.Active());
    CHECK_FALSE(lock.Pending());
    CHECK(lock.SetWanted(true) == Action::Nothing);
    CHECK(lock.SetWanted(false) == Action::Release);
    CHECK_FALSE(lock.Active());
    CHECK(lock.SetWanted(false) == Action::Nothing);
}

TEST_CASE("PointerLockState: a request while unfocused is remembered and applied on focus", "[cursor][lock][os]") {
    PointerLockState lock;
    CHECK(lock.SetWanted(true) == Action::Nothing);
    CHECK_FALSE(lock.Active());
    CHECK(lock.SetFocused(true) == Action::Acquire);
    lock.Granted();
    CHECK(lock.Active());
}

TEST_CASE("PointerLockState: focus loss releases and focus gain asks again while still wanted", "[cursor][lock][os]") {
    PointerLockState lock;
    lock.SetFocused(true);
    lock.SetWanted(true);
    lock.Granted();
    CHECK(lock.SetFocused(false) == Action::Release);
    CHECK_FALSE(lock.Active());
    CHECK(lock.Wanted());
    CHECK(lock.SetFocused(true) == Action::Acquire);
    lock.Granted();
    CHECK(lock.Active());
    // Not wanted any more: focus changes do nothing.
    lock.SetWanted(false);
    CHECK(lock.SetFocused(false) == Action::Nothing);
    CHECK(lock.SetFocused(true) == Action::Nothing);
}

TEST_CASE("PointerLockState: a pending request is cancelled by a release or a focus loss", "[cursor][lock][os]") {
    PointerLockState lock;
    lock.SetFocused(true);
    CHECK(lock.SetWanted(true) == Action::Acquire);
    CHECK(lock.SetFocused(false) == Action::Release); // the object exists but is not yet answered: it must go
    CHECK_FALSE(lock.Pending());
    lock.Granted(); // a late answer for a cancelled request changes nothing
    CHECK_FALSE(lock.Active());
}

TEST_CASE("PointerLockState: a refused request is retried on the next focus change, not in a loop", "[cursor][lock][os]") {
    PointerLockState lock;
    lock.SetFocused(true);
    CHECK(lock.SetWanted(true) == Action::Acquire);
    lock.Failed();
    CHECK_FALSE(lock.Active());
    CHECK(lock.SetWanted(true) == Action::Acquire); // an explicit request asks again
    lock.Failed();
    CHECK(lock.SetFocused(false) == Action::Nothing);
    CHECK(lock.SetFocused(true) == Action::Acquire);
}

TEST_CASE("PointerLockState: a lock the compositor took away stays wanted but is not re-asked until focus changes", "[cursor][lock][os]") {
    PointerLockState lock;
    lock.SetFocused(true);
    lock.SetWanted(true);
    lock.Granted();
    lock.Lost();
    CHECK_FALSE(lock.Active());
    CHECK(lock.Wanted());
    CHECK(lock.SetWanted(true) == Action::Nothing); // not asked again at once
    CHECK(lock.SetFocused(false) == Action::Nothing);
    CHECK(lock.SetFocused(true) == Action::Acquire);
}

TEST_CASE("PointerLockState: destroying the window forgets the lock but not the wish", "[cursor][lock][os]") {
    PointerLockState lock;
    lock.SetFocused(true);
    lock.SetWanted(true);
    lock.Granted();
    lock.Cleared();
    CHECK_FALSE(lock.Active());
    CHECK_FALSE(lock.Pending());
    CHECK(lock.Wanted());
}


TEST_CASE("Every Wayland cursor listener slot has a function (libwayland aborts on a null one)", "[cursor][wayland][os]") {
    CHECK(wayland::WaylandCursor::ListenersComplete());
    CHECK(wayland::WaylandInput::ListenersComplete());
}

namespace {

// Opens a Wayland window, or skips the test when there is no Wayland session.
bool OpenWayland(wayland::WaylandPlatformWindow& window, InputState& input) {
    if (!window.Open("cursor test", 320, 240, &input)) {
        return false;
    }
    return true;
}

} // namespace

TEST_CASE("WaylandPlatformWindow sets every cursor shape and the Hidden shape, repeatedly", "[wayland][cursor][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    wayland::WaylandPlatformWindow window;
    if (!OpenWayland(window, input)) SKIP("no Wayland session");
    if (!window.SupportsCursorShape()) SKIP("the compositor does not offer wp_cursor_shape_v1");
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < static_cast<int>(CursorShape::Count); ++i) {
            INFO("shape " << i);
            CHECK(window.SetCursor(static_cast<CursorShape>(i)));
        }
    }
    CHECK_FALSE(window.SetCursor(CursorShape::Count));
    for (int i = 0; i < 20; ++i) window.PumpEvents(); // the compositor accepts every request: no protocol error
    CHECK(window.Valid());
}

TEST_CASE("WaylandPlatformWindow sets, replaces and clears a custom cursor image without leaking its buffer", "[wayland][cursor][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    wayland::WaylandPlatformWindow window;
    if (!OpenWayland(window, input)) SKIP("no Wayland session");
    REQUIRE(window.SupportsCursorImage());
    CHECK(window.Input().Cursor().BufferCount() == 0);
    for (u32 size : {1u, 32u, 128u}) {
        const auto px = Pixels(size, size, static_cast<byte>(size));
        CursorImage image = Image(px, size, size);
        image.scale = size == 32 ? 2 : 1;
        INFO("size " << size);
        CHECK(window.SetCursorImage(image));
        CHECK(window.Input().Cursor().BufferCount() == 1); // a replacement frees the old buffer
    }
    const auto px = Pixels(128, 128, 128);
    CHECK(window.SetCursorImage(Image(px, 128, 128))); // the same image again
    CHECK(window.Input().Cursor().BufferCount() == 1);
    const auto big = Pixels(129, 8);
    CHECK_FALSE(window.SetCursorImage(Image(big, 129, 8)));
    CHECK(window.Input().Cursor().BufferCount() == 1); // a refused image changes nothing
    CHECK(window.SetCursor(CursorShape::Hand) == window.SupportsCursorShape()); // a shape does not remove the image
    window.ClearCursorImage();
    CHECK(window.Input().Cursor().BufferCount() == 0);
    window.ClearCursorImage();
    for (int i = 0; i < 20; ++i) window.PumpEvents();
    CHECK(window.Valid());
}

TEST_CASE("WaylandPlatformWindow accepts a lock request, an unlock and a close with the lock requested", "[wayland][cursor][lock][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    {
        wayland::WaylandPlatformWindow window;
        if (!OpenWayland(window, input)) SKIP("no Wayland session");
        if (!window.SupportsPointerLock()) {
            CHECK_FALSE(window.SetPointerLock(true)); // a compositor without the protocols refuses
            CHECK_FALSE(window.PointerLocked());
            SKIP("the compositor does not offer the pointer constraint protocols");
        }
        CHECK(window.SetPointerLock(true));
        CHECK(window.SetPointerLock(true)); // a second request is a no-op
        for (int i = 0; i < 20; ++i) window.PumpEvents();
        CHECK(window.Valid());
        CHECK(window.SetPointerLock(false));
        CHECK_FALSE(window.PointerLocked());
        CHECK(window.SetPointerLock(true));
        for (int i = 0; i < 5; ++i) window.PumpEvents();
        // Leaving the scope closes the window with the lock requested (and possibly granted).
    }
    wayland::WaylandPlatformWindow again; // the compositor is still fine
    if (!OpenWayland(again, input)) SKIP("no Wayland session");
    again.PumpEvents();
    CHECK(again.Valid());
}

} // namespace window_cursor_test
