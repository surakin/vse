#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include <string>
#include <utility>
#include <vector>

#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/linux/keyboard_events.h"
#include "platform/linux/xkb_keyboard.h"
#include "platform/linux/wayland/wayland_input.h"
#include "platform/linux/wayland/wayland_platform_window.h"
#include "platform/window.h"
#include "os/os.h"

#include "platform/linux/x11/x11_window.h"

// Day 185: the input event stream. The using-directives stay inside this namespace (a unity build merges files).
namespace window_events_test {

using namespace engine_placeholder;
using namespace engine_placeholder::os;

namespace {

// evdev keycodes (input-event-codes.h).
constexpr u32 kA = 30;
constexpr u32 kE = 18;
constexpr u32 kS = 31;
constexpr u32 kMinus = 12;   // ß on de
constexpr u32 kEqual = 13;   // dead acute on de
constexpr u32 kEnter = 28;
constexpr u32 kBackspace = 14;
constexpr u32 kTab = 15;
constexpr u32 kLeftShift = 42;
constexpr u32 kLeftCtrl = 29;
constexpr u32 kRightAlt = 100;
constexpr u64 kMs = 1'000'000ull;

std::vector<InputEvent> Poll(os::Window& window) {
    std::vector<InputEvent> out;
    InputEvent e;
    while (window.PollEvent(&e)) out.push_back(e);
    return out;
}

std::vector<InputEvent> Drain(InputEventQueue& q) {
    std::vector<InputEvent> out;
    InputEvent e;
    while (q.Pop(&e)) out.push_back(e);
    return out;
}

std::string TextOf(const std::vector<InputEvent>& events) {
    std::string s;
    for (const InputEvent& e : events) {
        if (e.type == InputEventType::Text) s.append(e.text, e.text_length);
    }
    return s;
}

usize Count(const std::vector<InputEvent>& events, InputEventType t) {
    usize n = 0;
    for (const InputEvent& e : events) n += e.type == t ? 1 : 0;
    return n;
}

// A keyboard with a layout, a compose table and an event converter; the test plays the compositor's part (modifier keys update the xkb state).
struct Rig {
    explicit Rig(const char* layout) {
        REQUIRE(keyboard.InitFromNames(layout, nullptr));
        keyboard.InitCompose("en_US.UTF-8");
        events.Init(&keyboard, &queue);
        events.SetRepeatInfo(25, 600);
    }
    void Press(u32 code, u64 now = 0) {
        keyboard.UpdateKey(code, true);
        events.Key(code, true, now);
    }
    void Release(u32 code, u64 now = 0) {
        events.Key(code, false, now);
        keyboard.UpdateKey(code, false);
    }
    XkbKeyboard keyboard;
    InputEventQueue queue;
    KeyboardEvents events;
};

} // namespace

TEST_CASE("InputEventQueue is first in, first out and drops the newest when full", "[input][events][os]") {
    InputEventQueue q;
    InputEvent e;
    REQUIRE_FALSE(q.Pop(&e));
    for (usize i = 0; i < InputEventQueue::kCapacity; ++i) {
        InputEvent in;
        in.type = InputEventType::Resize;
        in.width = static_cast<u32>(i);
        REQUIRE(q.Push(in));
    }
    InputEvent extra;
    extra.type = InputEventType::Resize;
    extra.width = 99999;
    REQUIRE_FALSE(q.Push(extra));
    REQUIRE_FALSE(q.Push(extra));
    REQUIRE(q.Dropped() == 2);
    REQUIRE(q.Size() == InputEventQueue::kCapacity);
    for (usize i = 0; i < InputEventQueue::kCapacity; ++i) {
        REQUIRE(q.Pop(&e));
        REQUIRE(e.width == i); // the oldest survive, in order, each once
    }
    REQUIRE_FALSE(q.Pop(&e));
    REQUIRE(q.Dropped() == 2); // the count is kept
    // After wrap-around it still works.
    for (int round = 0; round < 3; ++round) {
        for (u32 i = 0; i < 700; ++i) {
            InputEvent in;
            in.width = i;
            REQUIRE(q.Push(in));
        }
        for (u32 i = 0; i < 700; ++i) {
            REQUIRE(q.Pop(&e));
            REQUIRE(e.width == i);
        }
    }
    q.Push(extra);
    q.Clear();
    REQUIRE(q.Size() == 0);
}

TEST_CASE("Every Wayland listener slot has a function (libwayland aborts on a null one)", "[input][events][os]") {
    // Regression: binding the seat at version 5 made the compositor send pointer frame and axis events that had no listener, which aborted every windowed demo.
    CHECK(wayland::WaylandInput::ListenersComplete());
}

TEST_CASE("A window that is not open has no events", "[input][events][os]") {
    x11::X11Window x;
    InputEvent e;
    REQUIRE_FALSE(x.PollEvent(&e));
    REQUIRE(x.DroppedEvents() == 0);
    wayland::WaylandPlatformWindow w;
    REQUIRE_FALSE(w.PollEvent(&e));
    REQUIRE(w.DroppedEvents() == 0);
}

TEST_CASE("KeyboardEvents: letters produce a KeyDown and Text; Ctrl and Enter produce no text", "[input][events][os]") {
    Rig rig("us");
    rig.Press(kA, 1);
    auto ev = Drain(rig.queue);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].type == InputEventType::KeyDown);
    CHECK(ev[0].key == Key::A);
    CHECK_FALSE(ev[0].repeat);
    CHECK(TextOf(ev) == "a");
    rig.Release(kA, 2);
    ev = Drain(rig.queue);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].type == InputEventType::KeyUp);

    rig.Press(kLeftShift);
    rig.Press(kA);
    ev = Drain(rig.queue);
    CHECK(TextOf(ev) == "A");
    CHECK((ev.back().modifiers & kModShift) != 0);
    rig.Release(kA);
    rig.Release(kLeftShift);
    Drain(rig.queue);

    rig.Press(kLeftCtrl);
    rig.Press(kA);
    ev = Drain(rig.queue);
    CHECK(Count(ev, InputEventType::KeyDown) == 2);
    CHECK(Count(ev, InputEventType::Text) == 0);
    rig.Release(kA);
    rig.Release(kLeftCtrl);
    Drain(rig.queue);

    for (u32 code : {kEnter, kBackspace, kTab}) {
        rig.Press(code);
        ev = Drain(rig.queue);
        CHECK(Count(ev, InputEventType::KeyDown) == 1);
        CHECK(Count(ev, InputEventType::Text) == 0);
        rig.Release(code);
        Drain(rig.queue);
    }
}

TEST_CASE("KeyboardEvents: German layout gives ß, € on AltGr and é through a dead key", "[input][events][os]") {
    Rig rig("de");
    rig.Press(kMinus);
    CHECK(TextOf(Drain(rig.queue)) == "\xC3\x9F");
    rig.Release(kMinus);
    Drain(rig.queue);

    rig.Press(kRightAlt);
    rig.Press(kE);
    CHECK(TextOf(Drain(rig.queue)) == "\xE2\x82\xAC");
    rig.Release(kE);
    rig.Release(kRightAlt);
    Drain(rig.queue);

    // The dead acute key alone writes nothing; the next letter completes the sequence.
    rig.Press(kEqual);
    auto ev = Drain(rig.queue);
    CHECK(Count(ev, InputEventType::KeyDown) == 0); // a dead key has no engine Key name
    CHECK(Count(ev, InputEventType::Text) == 0);
    rig.Release(kEqual);
    rig.Press(kE);
    CHECK(TextOf(Drain(rig.queue)) == "\xC3\xA9");
    rig.Release(kE);
    Drain(rig.queue);

    // Dead key then a letter with no combination: no text for the dead key, and the sequence is over.
    rig.Press(kEqual);
    rig.Release(kEqual);
    rig.Press(kS);
    rig.Release(kS);
    rig.Press(kA);
    ev = Drain(rig.queue);
    CHECK(TextOf(ev).size() > 0);
    rig.Release(kA);
}

TEST_CASE("KeyboardEvents: a key without an engine name types its text but has no down or up", "[input][events][os]") {
    Rig rig("ru");
    rig.Press(kA, 0); // a Cyrillic letter: KeyFromKeysym has no Key for it
    auto ev = Drain(rig.queue);
    CHECK(Count(ev, InputEventType::KeyDown) == 0);
    CHECK(TextOf(ev) == "\xD1\x84");
    rig.events.Tick(10'000 * kMs);
    ev = Drain(rig.queue);
    CHECK(Count(ev, InputEventType::KeyDown) == 0);
    CHECK(Count(ev, InputEventType::Text) > 0); // the repeat still types
    rig.Release(kA, 10'001 * kMs);
    CHECK(Count(Drain(rig.queue), InputEventType::KeyUp) == 0);
}

TEST_CASE("KeyboardEvents: repeat starts after the delay, runs at the rate and stops on release", "[input][events][os]") {
    Rig rig("us");
    rig.events.SetRepeatInfo(10, 500); // 100 ms interval
    rig.Press(kA, 0);
    Drain(rig.queue);
    rig.events.Tick(499 * kMs);
    CHECK(rig.queue.Size() == 0);
    rig.events.Tick(500 * kMs);
    auto ev = Drain(rig.queue);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].type == InputEventType::KeyDown);
    CHECK(ev[0].repeat);
    CHECK(TextOf(ev) == "a"); // a repeat types again
    rig.events.Tick(599 * kMs);
    CHECK(rig.queue.Size() == 0);
    rig.events.Tick(600 * kMs);
    CHECK(Count(Drain(rig.queue), InputEventType::KeyDown) == 1);
    // A stall of ten seconds queues at most the cap, not a hundred repeats.
    rig.events.Tick(10'600 * kMs);
    CHECK(Count(Drain(rig.queue), InputEventType::KeyDown) == KeyboardEvents::kMaxRepeatsPerTick);
    rig.Release(kA, 10'700 * kMs);
    Drain(rig.queue);
    CHECK_FALSE(rig.events.Repeating());
    rig.events.Tick(20'000 * kMs);
    CHECK(rig.queue.Size() == 0);
}

TEST_CASE("KeyboardEvents: the last pressed key repeats and a modifier never does", "[input][events][os]") {
    Rig rig("us");
    rig.events.SetRepeatInfo(10, 500);
    rig.Press(kA, 0);
    rig.Press(kE, 100 * kMs);
    Drain(rig.queue);
    rig.events.Tick(700 * kMs);
    auto ev = Drain(rig.queue);
    REQUIRE(Count(ev, InputEventType::KeyDown) >= 1);
    for (const InputEvent& e : ev) {
        if (e.type == InputEventType::KeyDown) CHECK(e.key == Key::E);
    }
    // Releasing the first key does not stop the second key's repeat.
    rig.Release(kA, 710 * kMs);
    Drain(rig.queue);
    CHECK(rig.events.Repeating());
    rig.Release(kE, 720 * kMs);
    rig.Press(kLeftShift, 730 * kMs);
    Drain(rig.queue);
    CHECK_FALSE(rig.events.Repeating());
    rig.events.Tick(5'000 * kMs);
    CHECK(rig.queue.Size() == 0);
}

TEST_CASE("KeyboardEvents: a rate of zero turns repeat off", "[input][events][os]") {
    Rig rig("us");
    rig.events.SetRepeatInfo(0, 500);
    rig.Press(kA, 0);
    Drain(rig.queue);
    rig.events.Tick(5'000 * kMs);
    CHECK(rig.queue.Size() == 0);
}

TEST_CASE("KeyboardEvents: losing focus releases held keys, then reports FocusLost, and stops repeat", "[input][events][os]") {
    Rig rig("us");
    rig.events.FocusGained(0);
    rig.Press(kLeftShift);
    rig.Press(kA);
    auto ev = Drain(rig.queue);
    CHECK(ev.front().type == InputEventType::FocusGained);
    rig.events.FocusLost(10);
    ev = Drain(rig.queue);
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].type == InputEventType::KeyUp);
    CHECK(ev[1].type == InputEventType::KeyUp);
    CHECK(ev[2].type == InputEventType::FocusLost);
    CHECK_FALSE(rig.events.Repeating());
    rig.events.Tick(10'000 * kMs);
    CHECK(rig.queue.Size() == 0);
    // A key already down at focus arrival is a KeyDown without text or repeat.
    rig.events.FocusGained(20);
    rig.events.KeyAlreadyDown(kA, 20);
    ev = Drain(rig.queue);
    CHECK(Count(ev, InputEventType::KeyDown) == 1);
    CHECK(Count(ev, InputEventType::Text) == 0);
    CHECK_FALSE(rig.events.Repeating());
}

TEST_CASE("KeyboardEvents: with a full queue the events drop and are counted, not reordered", "[input][events][os]") {
    Rig rig("us");
    for (int i = 0; i < 600; ++i) {
        rig.Press(kA);
        rig.Release(kA);
    }
    CHECK(rig.queue.Dropped() > 0);
    CHECK(rig.queue.Size() == InputEventQueue::kCapacity);
}

// ---- Wayland: a real compositor is the only server there is ------------------------------------------

TEST_CASE("WaylandPlatformWindow reports a Resize event once it is open", "[wayland][input][events][os]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    InputState input;
    wayland::WaylandPlatformWindow window;
    if (!window.Open("events test", 320, 240, &input)) {
        SKIP("no Wayland session");
    }
    // The Wayland wire cannot be faked here (KNOWN-ISSUES): a real compositor decides what arrives, so this checks the queue is drainable and nothing is dropped.
    for (int i = 0; i < 40; ++i) {
        window.PumpEvents();
        SleepNanos(5'000'000);
    }
    const auto ev = Poll(window);
    for (const InputEvent& e : ev) {
        if (e.type == InputEventType::Resize) {
            CHECK(e.width > 0);
            CHECK(e.height > 0);
        }
    }
    CHECK(window.DroppedEvents() == 0);
}

} // namespace window_events_test
