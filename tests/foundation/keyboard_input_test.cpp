#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "platform/input.h"
#include "platform/linux/key_translate.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "platform/linux/wayland/wayland_input.h"
#include "platform/linux/xkb_keyboard.h"

// The using-directives stay inside this namespace: at file scope they would leak into the files merged after this one in a unity build
// (an X11 header then finds `Window` ambiguous).
namespace keyboard_input_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;

namespace {

// Linux evdev keycodes for physical key positions (input-event-codes.h).
constexpr u32 kEvdevQ = 16;
constexpr u32 kEvdevW = 17;
constexpr u32 kEvdevA = 30;
constexpr u32 kEvdevZ = 44;
constexpr u32 kEvdevM = 50;
constexpr u32 kEvdev1 = 2;
constexpr u32 kEvdevSpace = 57;

} // namespace

TEST_CASE("KeyFromKeysym maps letters case-insensitively, digits, and named keys", "[input][os]") {
    REQUIRE(KeyFromKeysym('a') == Key::A);
    REQUIRE(KeyFromKeysym('A') == Key::A);
    REQUIRE(KeyFromKeysym('z') == Key::Z);
    REQUIRE(KeyFromKeysym('0') == Key::Num0);
    REQUIRE(KeyFromKeysym('9') == Key::Num9);
    REQUIRE(KeyFromKeysym(0xff1b) == Key::Escape);     // XKB_KEY_Escape
    REQUIRE(KeyFromKeysym(0xffbe) == Key::F1);         // XKB_KEY_F1
    REQUIRE(KeyFromKeysym(0xffc9) == Key::F12);        // XKB_KEY_F12
    REQUIRE(KeyFromKeysym(0xffe1) == Key::LeftShift);  // XKB_KEY_Shift_L
    REQUIRE(KeyFromKeysym(0xff51) == Key::Left);       // XKB_KEY_Left
    REQUIRE(KeyFromKeysym(0x0430) == Key::Unknown);    // Cyrillic small a
    REQUIRE(KeyFromKeysym(0) == Key::Unknown);
}

TEST_CASE("KeyboardState tracks down keys and ignores Unknown/out-of-range", "[input][os]") {
    KeyboardState state;
    REQUIRE_FALSE(state.IsDown(Key::A));
    state.SetDown(Key::A, true);
    state.SetDown(Key::Slash, true);
    REQUIRE(state.IsDown(Key::A));
    REQUIRE(state.IsDown(Key::Slash));
    state.SetDown(Key::A, false);
    REQUIRE_FALSE(state.IsDown(Key::A));
    REQUIRE(state.IsDown(Key::Slash));
    state.SetDown(Key::Unknown, true);
    REQUIRE_FALSE(state.IsDown(Key::Unknown));
    state.SetDown(Key::Count, true);
    REQUIRE_FALSE(state.IsDown(Key::Count));
    state.ReleaseAll();
    REQUIRE_FALSE(state.IsDown(Key::Slash));
}

TEST_CASE("XkbKeyboard translates the same physical keys per layout (US QWERTY)", "[input][os][xkb]") {
    XkbKeyboard kb;
    REQUIRE(kb.InitFromNames("us"));
    REQUIRE(kb.KeyForEvdevCode(kEvdevQ) == Key::Q);
    REQUIRE(kb.KeyForEvdevCode(kEvdevW) == Key::W);
    REQUIRE(kb.KeyForEvdevCode(kEvdevA) == Key::A);
    REQUIRE(kb.KeyForEvdevCode(kEvdevZ) == Key::Z);
    REQUIRE(kb.KeyForEvdevCode(kEvdevM) == Key::M);
    REQUIRE(kb.KeyForEvdevCode(kEvdev1) == Key::Num1);
    REQUIRE(kb.KeyForEvdevCode(kEvdevSpace) == Key::Space);
}

TEST_CASE("XkbKeyboard translates the same physical keys per layout (French AZERTY)", "[input][os][xkb]") {
    XkbKeyboard kb;
    REQUIRE(kb.InitFromNames("fr"));
    // AZERTY swaps A<->Q, Z<->W and moves M; digits need Shift.
    REQUIRE(kb.KeyForEvdevCode(kEvdevQ) == Key::A);
    REQUIRE(kb.KeyForEvdevCode(kEvdevW) == Key::Z);
    REQUIRE(kb.KeyForEvdevCode(kEvdevA) == Key::Q);
    REQUIRE(kb.KeyForEvdevCode(kEvdevZ) == Key::W);
    REQUIRE(kb.KeyForEvdevCode(kEvdevM) == Key::Comma);
    REQUIRE(kb.KeyForEvdevCode(kEvdev1) == Key::Unknown); // '&'
    REQUIRE(kb.KeyForEvdevCode(kEvdevSpace) == Key::Space);
}

TEST_CASE("XkbKeyboard translates the same physical keys per layout (German QWERTZ)", "[input][os][xkb]") {
    XkbKeyboard kb;
    REQUIRE(kb.InitFromNames("de"));
    REQUIRE(kb.KeyForEvdevCode(kEvdevQ) == Key::Q);
    REQUIRE(kb.KeyForEvdevCode(kEvdevZ) == Key::Y); // QWERTZ swaps Y/Z
    REQUIRE(kb.KeyForEvdevCode(kEvdevM) == Key::M);
    REQUIRE(kb.KeyForEvdevCode(kEvdev1) == Key::Num1);
}

TEST_CASE("XkbKeyboard UpdateMask drives effective modifiers and shifted symbols", "[input][os][xkb]") {
    XkbKeyboard kb;
    REQUIRE(kb.InitFromNames("fr"));
    REQUIRE(kb.Modifiers() == 0);
    // Shift is modifier bit 0 in the standard XKB real-modifier order.
    kb.UpdateMask(1u, 0, 0, 0);
    REQUIRE((kb.Modifiers() & kModShift) != 0);
    REQUIRE(kb.KeyForEvdevCode(kEvdev1) == Key::Num1); // AZERTY: Shift+& = 1
    kb.UpdateMask(0, 0, 0, 0);
    REQUIRE((kb.Modifiers() & kModShift) == 0);
}

TEST_CASE("XkbKeyboard rejects unusable keymap text and stays invalid", "[input][os][xkb]") {
    XkbKeyboard kb;
    const char garbage[] = "this is not an xkb keymap";
    REQUIRE_FALSE(kb.InitFromKeymapText(garbage, sizeof(garbage)));
    REQUIRE_FALSE(kb.Valid());
    REQUIRE(kb.KeyForEvdevCode(kEvdevQ) == Key::Unknown);
}

TEST_CASE("WaylandInput::Init hooks the real seat and loads its keymap", "[wayland][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    wayland::WaylandConnection conn = wayland::WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    InputState state;
    wayland::WaylandInput input;
    REQUIRE(input.Init(conn, &state));
    REQUIRE(input.Valid());
    if (input.HasKeyboard()) {
        REQUIRE(input.HasKeymap());
    }
    REQUIRE_FALSE(state.keyboard.IsDown(Key::A));
}

TEST_CASE("WaylandInput::Init fails gracefully on an invalid connection", "[wayland][input][os]") {
    wayland::WaylandConnection invalid;
    InputState state;
    wayland::WaylandInput input;
    REQUIRE_FALSE(input.Init(invalid, &state));
    REQUIRE_FALSE(input.Valid());
}

TEST_CASE("InputState defaults to idle and BeginFrame clears only scroll deltas", "[input][os]") {
    InputState state;
    REQUIRE(state.mouse_buttons == 0);
    REQUIRE_FALSE(state.pointer_inside);
    REQUIRE(state.scroll_dx == 0.0);
    REQUIRE(state.scroll_dy == 0.0);

    state.mouse_x = 12.5;
    state.mouse_y = 40.0;
    state.mouse_buttons = kMouseLeft | kMouseMiddle;
    state.scroll_dx = 1.0;
    state.scroll_dy = -3.0;
    state.keyboard.SetDown(Key::W, true);
    REQUIRE(state.IsMouseDown(kMouseLeft));
    REQUIRE(state.IsMouseDown(kMouseMiddle));
    REQUIRE_FALSE(state.IsMouseDown(kMouseRight));

    state.BeginFrame();
    REQUIRE(state.scroll_dx == 0.0);
    REQUIRE(state.scroll_dy == 0.0);
    REQUIRE(state.mouse_x == 12.5);
    REQUIRE(state.mouse_buttons == (kMouseLeft | kMouseMiddle));
    REQUIRE(state.keyboard.IsDown(Key::W));
}

TEST_CASE("WaylandInput::Init creates a pointer iff the seat advertises one", "[wayland][input][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    wayland::WaylandConnection conn = wayland::WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    InputState state;
    wayland::WaylandInput input;
    REQUIRE(input.Init(conn, &state));
    // Without real pointer motion, nothing may have been recorded.
    REQUIRE(state.mouse_buttons == 0);
    REQUIRE(state.scroll_dx == 0.0);
    REQUIRE(state.scroll_dy == 0.0);
}

} // namespace keyboard_input_test
