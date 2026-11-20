// The Cocoa window (Phase 21 Day 295; engine/platform/macos/cocoa_window.h): Retina size and scale, the CAMetalLayer
// the RHI gets, resize through AppKit, the close button and Quit as requests the program answers, minimizing, a window
// refused off the main thread, and a window destroyed while AppKit still has events for it. Every case opens a real
// window, so the file is in the `display` group and skips while the session is locked.
#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "display_session.h"
#include "os/os.h"
#include "platform/macos/cocoa_window.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

using namespace vse;
using vse::os::cocoa::CocoaWindow;

namespace
{

template <typename Pred> bool PumpUntil(os::Window& window, Pred done, int ms = 3000)
{
    for (int waited = 0; waited < ms; waited += 10)
    {
        window.PumpEvents();
        if (done())
        {
            return true;
        }
        os::SleepNanos(10'000'000);
    }
    return false;
}

NSWindow* WindowOf(const CocoaWindow& window)
{
    NSView* view = (__bridge NSView*)window.GetNativeInfo().display;
    return view.window;
}

}  // namespace

TEST_CASE("macOS: a Cocoa window is sized in pixels at the backing scale and backed by a CAMetalLayer",
          "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa size", 400, 300, nullptr));
    REQUIRE(window.Valid());
    NSWindow* ns = WindowOf(window);
    REQUIRE(ns != nil);
    const f32 scale = static_cast<f32>(ns.backingScaleFactor);
    CHECK(window.ContentScale() == scale);
    CHECK(window.Width() == static_cast<u32>(400 * scale));
    CHECK(window.Height() == static_cast<u32>(300 * scale));
    CHECK(StringView(window.BackendName()) == StringView("cocoa"));
    const os::NativeWindowInfo info = window.GetNativeInfo();
    CHECK(info.kind == os::NativeWindowKind::Cocoa);
    CHECK(info.window_id == static_cast<u64>(ns.windowNumber));
    id layer = (__bridge id)info.surface;
    REQUIRE(layer != nil);
    CHECK([layer isKindOfClass:[CAMetalLayer class]]);
    CHECK(static_cast<f32>([(CAMetalLayer*)layer contentsScale]) == scale);
    CHECK(ns.collectionBehavior & NSWindowCollectionBehaviorFullScreenPrimary);
    CHECK_FALSE(window.IsMinimized());
}

TEST_CASE("macOS: a resize through AppKit bumps the serial and queues a Resize in pixels", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa resize", 320, 200, nullptr));
    os::InputEvent event;
    while (window.PollEvent(&event))
    {
    }
    const u64 serial = window.ResizeSerial();
    NSWindow* ns = WindowOf(window);
    [ns setContentSize:NSMakeSize(500, 360)];
    REQUIRE(PumpUntil(window, [&] { return window.ResizeSerial() != serial; }));
    const f32 scale = window.ContentScale();
    CHECK(window.Width() == static_cast<u32>(500 * scale));
    CHECK(window.Height() == static_cast<u32>(360 * scale));
    bool resized = false;
    while (window.PollEvent(&event))
    {
        if (event.type == os::InputEventType::Resize)
        {
            resized = true;
            CHECK(event.width == window.Width());
            CHECK(event.height == window.Height());
            CHECK(event.time_ns != 0);
        }
    }
    CHECK(resized);
}

TEST_CASE("macOS: the close button is a request the program answers; the window stays until it closes it",
          "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa close", 320, 200, nullptr));
    CHECK_FALSE(window.ShouldClose());
    NSWindow* ns = WindowOf(window);
    [ns performClose:nil];  // what the close button does
    window.PumpEvents();
    CHECK(window.ShouldClose());
    CHECK(window.Valid());
    CHECK(ns.visible);  // windowShouldClose: said no
    window.Close();
    CHECK_FALSE(window.Valid());
    CHECK_FALSE(ns.visible);
    window.Close();  // twice is harmless
}

TEST_CASE("macOS: Quit (Cmd-Q, the Dock) asks every window to close, and a program can take it back", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow first;
    CocoaWindow second;
    REQUIRE(first.Open("cocoa quit 1", 200, 150, nullptr));
    REQUIRE(second.Open("cocoa quit 2", 200, 150, nullptr));
    [NSApp terminate:nil];  // the Quit menu item's action (Cmd-Q); the process must go on
    first.PumpEvents();
    CHECK(os::cocoa::QuitRequested());
    CHECK(first.ShouldClose());
    CHECK(second.ShouldClose());
    CHECK(first.Valid());
    os::cocoa::ClearQuitRequest();
    CHECK_FALSE(first.ShouldClose());
    CHECK_FALSE(second.ShouldClose());
    // The menu has the Quit item with Cmd-Q.
    NSMenuItem* quit = [[[NSApp.mainMenu itemAtIndex:0] submenu] itemAtIndex:0];
    REQUIRE(quit != nil);
    CHECK([quit.keyEquivalent isEqualToString:@"q"]);
    CHECK(quit.action == @selector(terminate:));
    // And the key itself: a Cmd-Q key-down given to the application goes through its key-equivalent handling to the
    // menu, as a press at the keyboard does (checked by hand would need the screen; this is the same AppKit path).
    NSEvent* cmd_q = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                      location:NSZeroPoint
                                 modifierFlags:NSEventModifierFlagCommand
                                     timestamp:0
                                  windowNumber:WindowOf(first).windowNumber
                                       context:nil
                                    characters:@"q"
                   charactersIgnoringModifiers:@"q"
                                     isARepeat:NO
                                       keyCode:12];  // kVK_ANSI_Q
    [NSApp sendEvent:cmd_q];
    first.PumpEvents();
    CHECK(os::cocoa::QuitRequested());
    CHECK(first.ShouldClose());
    os::cocoa::ClearQuitRequest();
}

TEST_CASE("macOS: a minimized window reports it, and restores", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa minimize", 320, 200, nullptr));
    NSWindow* ns = WindowOf(window);
    [ns miniaturize:nil];
    if (!PumpUntil(window, [&] { return window.IsMinimized(); }))
    {
        SKIP("the Dock did not take the window in 3 s");
    }
    [ns deminiaturize:nil];
    CHECK(PumpUntil(window, [&] { return !window.IsMinimized(); }));
}

TEST_CASE("macOS: a window opened off the main thread is refused, not a crash", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    Atomic<int> opened{-1};
    Thread worker(
        [&opened]
        {
            CocoaWindow window;
            opened.Store(window.Open("off the main thread", 200, 150, nullptr) ? 1 : 0);
        });
    worker.Join();
    CHECK(opened.Load() == 0);
}

TEST_CASE("macOS: a window destroyed with AppKit events still pending, and a closed one, take no callback",
          "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    for (int i = 0; i < 5; ++i)
    {
        UniquePtr<CocoaWindow> window = MakeUnique<CocoaWindow>();
        REQUIRE(window->Open("cocoa churn", 200, 150, nullptr));
        NSWindow* ns = WindowOf(*window);
        [ns setContentSize:NSMakeSize(260, 180)];  // notifications on their way
        [ns performClose:nil];
        window.Reset();  // destroyed with them pending
        // AppKit may still deliver what it queued for that window: nothing may reach the deleted object.
        CocoaWindow probe;
        REQUIRE(probe.Open("cocoa probe", 120, 90, nullptr));
        probe.PumpEvents();
        CHECK_FALSE(probe.ShouldClose());
    }
    CocoaWindow closed;
    CHECK_FALSE(closed.Valid());
    closed.PumpEvents();  // nothing open: nothing to do
    CHECK(closed.GetNativeInfo().kind == os::NativeWindowKind::Invalid);
    CHECK_FALSE(closed.Open("zero", 0, 100, nullptr));
}

// Review findings, Day 295.

TEST_CASE("macOS: a logout, restart or shutdown is a session end; a plain Quit is not", "[macos][window]")
{
    const OSType ends[] = {kAELogOut,  kAEReallyLogOut,       kAEShowRestartDialog,
                           kAERestart, kAEShowShutdownDialog, kAEShutDown};
    for (const OSType reason : ends)
    {
        CHECK(os::cocoa::QuitReasonEndsSession(reason));
    }
    CHECK_FALSE(os::cocoa::QuitReasonEndsSession(0));  // Cmd-Q, the menu, the Dock: no reason
    CHECK_FALSE(os::cocoa::QuitReasonEndsSession('quit'));
}

TEST_CASE("macOS: a window reopened after Close starts afresh, and Close says the focus went", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa reopen", 240, 160, nullptr));
    window.PumpEvents();
    [WindowOf(window) performClose:nil];
    REQUIRE(window.ShouldClose());
    window.OnFocusChanged(true);  // as windowDidBecomeKey would
    window.Close();
    bool focus_lost = false;
    os::InputEvent event;
    while (window.PollEvent(&event))
    {
        focus_lost = focus_lost || event.type == os::InputEventType::FocusLost;
    }
    CHECK(focus_lost);
    REQUIRE(window.Open("cocoa reopened", 240, 160, nullptr));
    CHECK_FALSE(window.ShouldClose());  // the old close request does not carry over
    CHECK_FALSE(window.IsFullscreen());
    bool stale = false;
    while (window.PollEvent(&event))
    {
        stale = stale || event.type == os::InputEventType::FocusLost;
    }
    CHECK_FALSE(stale);  // the old window's events are gone
}

TEST_CASE("macOS: a closed window's PumpEvents still serves the program's other windows", "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow first;
    CocoaWindow second;
    REQUIRE(first.Open("cocoa pump 1", 200, 150, nullptr));
    REQUIRE(second.Open("cocoa pump 2", 200, 150, nullptr));
    first.Close();
    const u64 serial = second.ResizeSerial();
    [WindowOf(second) setContentSize:NSMakeSize(260, 190)];
    CHECK(PumpUntil(first, [&] { return second.ResizeSerial() != serial; }));
}

TEST_CASE("macOS: fullscreen asked for and taken back during the animation ends where the last request said",
          "[macos][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("cocoa fullscreen twice", 320, 240, nullptr));
    REQUIRE(window.SetFullscreen(true));
    REQUIRE(window.SetFullscreen(false));  // at once: the animation is running
    // It may go fullscreen first (AppKit does not stop an animation); it must settle out of fullscreen.
    os::SleepNanos(200'000'000);
    CHECK(PumpUntil(
        window,
        [&] { return !window.IsFullscreen() && !((WindowOf(window).styleMask & NSWindowStyleMaskFullScreen) != 0); },
        8000));
}
