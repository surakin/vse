// The macOS window backend (Phase 26 Day 311; platform/macos/cocoa_window.h, engine/platform/BACKEND.md "macos",
// platform/macos/FULLSCREEN.md). Objective-C++ with ARC: the window, its view and its delegate are held strongly by
// CocoaWindow::Native; the delegate reaches the C++ window through a plain pointer that Close clears before anything is
// released, so no AppKit callback reaches a destroyed window.
#include "platform/macos/cocoa_window.h"

#include "log/log.h"
#include "os/os.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>

using engine_placeholder::os::cocoa::CocoaWindow;

namespace
{

bool g_quit_requested = false;     // main thread only
bool g_terminate_pending = false;  // a session end is waiting for the program (NSTerminateLater was answered)
bool g_in_modal_callback = false;  // the live-resize callback is running: PumpEvents must not re-enter AppKit's loop

}  // namespace

// The application's delegate: Quit (Cmd-Q, the Dock, the menu) becomes every window's close request; the process is
// never terminated behind the program's back. A logout, restart or shutdown asks the same way but is not cancelled: it
// is answered "later", and the system waits for the program to end (or for ClearQuitRequest, which answers no).
@interface EngineApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation EngineApplicationDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    g_quit_requested = true;
    NSAppleEventDescriptor* event = [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];
    const OSType reason = event != nil ? [[event attributeDescriptorForKeyword:kAEQuitReason] enumCodeValue] : 0;
    if (engine_placeholder::os::cocoa::QuitReasonEndsSession(reason))
    {
        g_terminate_pending = true;
        return NSTerminateLater;
    }
    return NSTerminateCancel;
}
@end

// The content view: backed by a CAMetalLayer the RHI renders into (makeBackingLayer), so AppKit never draws it.
@interface EngineMetalView : NSView
@end

@implementation EngineMetalView
- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer
{
    return YES;
}
- (BOOL)acceptsFirstResponder
{
    return YES;
}
@end

// The window's delegate: reports to the C++ window, which Close detaches first.
@interface EngineWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) CocoaWindow* owner;
@end

@implementation EngineWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)window
{
    (void)window;
    if (self.owner != nullptr)
    {
        self.owner->OnCloseRequested();
    }
    return NO;  // the program decides when the window closes
}
- (void)windowDidResize:(NSNotification*)notification
{
    if (self.owner != nullptr)
    {
        self.owner->OnGeometryChanged([static_cast<NSWindow*>(notification.object) inLiveResize]);
    }
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnGeometryChanged(false);
    }
}
- (void)windowDidBecomeKey:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFocusChanged(true);
    }
}
- (void)windowDidResignKey:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFocusChanged(false);
    }
}
- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenTransition(true);
    }
}
- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenTransition(true);
    }
}
- (void)windowDidFailToEnterFullScreen:(NSWindow*)window
{
    (void)window;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenChanged(false);
    }
}
- (void)windowDidFailToExitFullScreen:(NSWindow*)window
{
    (void)window;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenChanged(true);
    }
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenChanged(true);
        self.owner->OnGeometryChanged(false);
    }
}
- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnFullscreenChanged(false);
        self.owner->OnGeometryChanged(false);
    }
}
@end

namespace engine_placeholder::os::cocoa
{

struct CocoaWindow::Native
{
    NSWindow* window = nil;
    EngineMetalView* view = nil;
    EngineWindowDelegate* delegate = nil;
};

namespace
{

EngineApplicationDelegate* g_app_delegate = nil;  // the application's delegate is a weak property: kept alive here

void InstallMenu(NSApplication* app)
{
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    NSMenu* app_menu = [[NSMenu alloc] init];
    NSString* name = [[NSProcessInfo processInfo] processName];
    [app_menu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
    app_item.submenu = app_menu;
    app.mainMenu = bar;
}

}  // namespace

bool EnsureApplication()
{
    // The thread first, every time: once the application exists, a caller on another thread must still be refused
    // (AppKit aborts the process for a window made off the main thread).
    if (![NSThread isMainThread])
    {
        Log(LogPlatform, LogVerbosity::Error,
            "Window: AppKit runs on the main thread only; no window from another thread");
        return false;
    }
    static bool ready = false;
    if (ready)
    {
        return true;
    }
    // Without a window server (an ssh login with nobody at the console) AppKit cannot connect; say so instead of
    // letting NSApplication fail.
    const CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (session == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error,
            "Window: no window-server session (an ssh login without the console?); macOS opens no window here");
        return false;
    }
    CFRelease(session);
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        g_app_delegate = [[EngineApplicationDelegate alloc] init];
        app.delegate = g_app_delegate;
        InstallMenu(app);
        [app finishLaunching];
    }
    ready = true;
    return true;
}

bool QuitRequested()
{
    return g_quit_requested;
}

void ClearQuitRequest()
{
    g_quit_requested = false;
    if (g_terminate_pending)
    {
        g_terminate_pending = false;
        [NSApp replyToApplicationShouldTerminate:NO];  // the program stays: the logout or shutdown is cancelled
    }
}

bool QuitReasonEndsSession(u32 reason)
{
    return reason == kAELogOut || reason == kAEReallyLogOut || reason == kAEShowRestartDialog || reason == kAERestart ||
           reason == kAEShowShutdownDialog || reason == kAEShutDown;
}

CocoaWindow::CocoaWindow() = default;

CocoaWindow::~CocoaWindow()
{
    Close();
}

bool CocoaWindow::Open(StringView title, u32 width, u32 height, InputState* input)
{
    if (native_ != nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "Window: CocoaWindow::Open called on an open window");
        return false;
    }
    if (width == 0 || height == 0)
    {
        Log(LogPlatform, LogVerbosity::Error, "Window: a window needs a size");
        return false;
    }
    if (!EnsureApplication())
    {
        return false;  // logged
    }
    // A window reopened after Close starts afresh (the Win32 window does the same).
    close_requested_ = false;
    focused_ = false;
    fullscreen_ = false;
    fullscreen_wanted_ = false;
    fullscreen_transition_ = false;
    events_.Clear();
    input_ = input != nullptr ? input : &own_input_;
    @autoreleasepool
    {
        native_ = MakeUnique<Native>();
        const NSRect content = NSMakeRect(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height));
        const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        native_->window = [[NSWindow alloc] initWithContentRect:content
                                                      styleMask:style
                                                        backing:NSBackingStoreBuffered
                                                          defer:NO];
        native_->window.releasedWhenClosed = NO;  // ARC owns it (Native::window)
        native_->window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        NSString* text = [[NSString alloc] initWithBytes:title.Data()
                                                  length:title.Length()
                                                encoding:NSUTF8StringEncoding];
        native_->window.title = text != nil ? text : @"";
        native_->view = [[EngineMetalView alloc] initWithFrame:content];
        native_->view.wantsLayer = YES;
        native_->window.contentView = native_->view;
        native_->delegate = [[EngineWindowDelegate alloc] init];
        native_->delegate.owner = this;
        native_->window.delegate = native_->delegate;
        [native_->window center];
        [native_->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
    OnGeometryChanged(false);
    return true;
}

void CocoaWindow::Close()
{
    if (native_ == nullptr)
    {
        return;
    }
    @autoreleasepool
    {
        native_->delegate.owner = nullptr;  // first: nothing AppKit sends from here on reaches this object
        native_->window.delegate = nil;
        [native_->window orderOut:nil];
        [native_->window close];
        native_.Reset();
    }
    width_ = 0;
    height_ = 0;
    fullscreen_ = false;  // a fullscreen window's Space goes with it
    fullscreen_wanted_ = false;
    fullscreen_transition_ = false;
    // The delegate is detached, so windowDidResignKey never comes: the stream still says the focus went.
    OnFocusChanged(false);
}

bool CocoaWindow::Valid() const
{
    return native_ != nullptr;
}

void CocoaWindow::PumpEvents()
{
    // Every window's events, whichever window pumps (open or not); never from inside AppKit's live-resize loop, whose
    // events a nested pump would take from it.
    if (![NSThread isMainThread] || g_in_modal_callback || NSApp == nil)
    {
        return;
    }
    @autoreleasepool
    {
        for (;;)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil)
            {
                break;
            }
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
        if (native_ != nullptr && !fullscreen_transition_ && fullscreen_wanted_ != fullscreen_)
        {
            fullscreen_transition_ = true;  // the request that arrived during the last animation
            [native_->window toggleFullScreen:nil];
        }
    }
}

bool CocoaWindow::ShouldClose() const
{
    return close_requested_ || QuitRequested();
}

NativeWindowInfo CocoaWindow::GetNativeInfo() const
{
    NativeWindowInfo info;
    if (native_ == nullptr)
    {
        return info;
    }
    info.kind = NativeWindowKind::Cocoa;
    info.display = (__bridge void*)native_->view;
    info.surface = (__bridge void*)native_->view.layer;
    info.window_id = static_cast<u64>(native_->window.windowNumber);
    return info;
}

bool CocoaWindow::IsMinimized() const
{
    return native_ == nullptr || native_->window.miniaturized || width_ == 0 || height_ == 0;
}

bool CocoaWindow::SupportsFullscreen() const
{
    return native_ != nullptr && (native_->window.styleMask & NSWindowStyleMaskTitled) != 0;
}

bool CocoaWindow::SetFullscreen(bool fullscreen)
{
    if (!SupportsFullscreen())
    {
        return false;
    }
    // The last request wins: during the animation (which AppKit does not interrupt) it waits for the transition to end.
    fullscreen_wanted_ = fullscreen;
    if (!fullscreen_transition_ && fullscreen != fullscreen_)
    {
        fullscreen_transition_ = true;
        [native_->window toggleFullScreen:nil];  // animated into or out of its own Space; the delegate reports the end
    }
    return true;
}

void CocoaWindow::SetModalLoopCallback(ModalLoopCallback callback, void* user)
{
    modal_callback_ = callback;
    modal_user_ = user;
}

void CocoaWindow::OnGeometryChanged(bool in_live_resize)
{
    if (native_ == nullptr)
    {
        return;
    }
    const NSRect points = native_->view.bounds;
    const NSRect pixels = [native_->view convertRectToBacking:points];
    const f32 scale = static_cast<f32>(native_->window.backingScaleFactor);
    const u32 width = static_cast<u32>(pixels.size.width);
    const u32 height = static_cast<u32>(pixels.size.height);
    // The layer's own scale follows the window's, so a drawable sized in pixels is shown sharp.
    native_->view.layer.contentsScale = native_->window.backingScaleFactor;
    if (width != width_ || height != height_ || scale != scale_)
    {
        width_ = width;
        height_ = height;
        scale_ = scale;
        resize_serial_ = detail::NextResizeSerial();
        InputEvent event;
        event.type = InputEventType::Resize;
        event.width = width;
        event.height = height;
        QueueEvent(event);
    }
    // During a live resize AppKit runs its own event loop and the program's is not running: let it draw from here.
    if (in_live_resize && modal_callback_ != nullptr && !g_in_modal_callback)
    {
        g_in_modal_callback = true;
        modal_callback_(modal_user_);
        g_in_modal_callback = false;
    }
}

void CocoaWindow::OnFullscreenTransition(bool started)
{
    fullscreen_transition_ = started;
}

void CocoaWindow::OnFullscreenChanged(bool fullscreen)
{
    fullscreen_ = fullscreen;
    fullscreen_transition_ = false;
    // A request made during the animation is applied by the next PumpEvents: AppKit ignores a toggle sent from inside
    // windowDidEnterFullScreen / windowDidExitFullScreen (seen on Day 311).
}

void CocoaWindow::OnFocusChanged(bool focused)
{
    if (focused == focused_)
    {
        return;
    }
    focused_ = focused;
    InputEvent event;
    event.type = focused ? InputEventType::FocusGained : InputEventType::FocusLost;
    QueueEvent(event);
}

void CocoaWindow::QueueEvent(InputEvent event)
{
    event.time_ns = MonotonicTimeNanos();
    events_.Push(event);
}

}  // namespace engine_placeholder::os::cocoa
