// The macOS window backend (Phase 26 Day 311; platform/macos/cocoa_window.h, engine/platform/BACKEND.md "macos",
// platform/macos/FULLSCREEN.md). Objective-C++ with ARC: the window, its view and its delegate are held strongly by
// CocoaWindow::Native; the delegate reaches the C++ window through a plain pointer that Close clears before anything is
// released, so no AppKit callback reaches a destroyed window.
#include "platform/macos/cocoa_window.h"
#include "platform/macos/cocoa_window_native.h"

#include "log/log.h"
#include "os/os.h"
#include "platform/macos/cocoa_keys.h"
#include "string/utf8_codec.h"

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

// The content view (declared in cocoa_window_native.h): backed by a CAMetalLayer the RHI renders into
// (makeBackingLayer), so AppKit never draws it. It is the window's first responder and turns the NSEvents it gets into
// the C++ window's input (cocoa_keys.h): keys, text through a minimal NSTextInputClient (interpretKeyEvents: gives dead
// keys and input methods their say; marked text is not shown), the pointer in pixels from the top left, the wheel in
// notches. The cursor and drag and drop are its categories in cocoa_cursor.mm and cocoa_dragdrop.mm.

namespace
{

engine_placeholder::u32 FirstScalar(NSString* text)
{
    if (text.length == 0)
    {
        return 0;
    }
    const char* utf8 = text.UTF8String;
    const engine_placeholder::usize length = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    return utf8 != nullptr ? engine_placeholder::DecodeUtf8At(utf8, length, 0).codepoint : 0;
}

}  // namespace

@implementation EngineMetalView
{
    BOOL has_marked_text_;
    NSTrackingArea* tracking_;
}
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
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;  // the click that activates the window reaches the program, as on the other platforms
}
- (void)updateTrackingAreas
{
    if (tracking_ == nil)
    {
        // The visible rectangle, kept current by AppKit: entered, exited and moved over whether or not the window is
        // key (Linux and Windows report motion to an unfocused window), and during a drag too.
        tracking_ = [[NSTrackingArea alloc]
            initWithRect:NSZeroRect
                 options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                         NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingEnabledDuringMouseDrag
                   owner:self
                userInfo:nil];
        [self addTrackingArea:tracking_];
    }
    [super updateTrackingAreas];
}

// The event's position in window pixels from the top left, and whether it is inside the view.
- (NSPoint)pixelPointOf:(NSEvent*)event inside:(BOOL*)inside
{
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const NSRect bounds = self.bounds;
    if (inside != nullptr)
    {
        *inside = NSPointInRect(p, bounds);
    }
    const CGFloat scale = self.window != nil ? self.window.backingScaleFactor : 1.0;
    return NSMakePoint(p.x * scale, (bounds.size.height - p.y) * scale);
}

- (void)keyDown:(NSEvent*)event
{
    if (self.owner == nullptr)
    {
        return;
    }
    if (self.owner->OnKeyDown(event.keyCode, FirstScalar(event.charactersIgnoringModifiers), event.modifierFlags,
                              event.isARepeat))
    {
        [self interpretKeyEvents:@[ event ]];  // insertText: (the Text after this KeyDown) or doCommandBySelector:
    }
}
- (void)keyUp:(NSEvent*)event
{
    if (self.owner != nullptr)
    {
        self.owner->OnKeyUp(event.keyCode, event.modifierFlags);
    }
}
- (void)flagsChanged:(NSEvent*)event
{
    if (self.owner != nullptr)
    {
        self.owner->OnFlagsChanged(event.keyCode, event.modifierFlags);
    }
}

// NSTextInputClient, minimally: committed text becomes Text events; marked text (a dead key waiting, an input method
// composing) is remembered so the input system knows, but not shown.
- (void)insertText:(id)string replacementRange:(NSRange)range
{
    (void)range;
    has_marked_text_ = NO;
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : string;
    if (self.owner != nullptr && text.length != 0)
    {
        engine_placeholder::Vector<engine_placeholder::byte> utf8;
        engine_placeholder::os::cocoa::Utf8Of(text, &utf8, nullptr);  // UTF8String gives nothing for a lone surrogate
        self.owner->OnText(reinterpret_cast<const char*>(utf8.Data()), utf8.Size());
    }
}
- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;  // Enter, Tab, Backspace, the arrows: the KeyDown said it; no beep, no text
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)range
{
    (void)selected;
    (void)range;
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : string;
    has_marked_text_ = text.length != 0;
}
- (void)unmarkText
{
    has_marked_text_ = NO;
}
- (BOOL)hasMarkedText
{
    return has_marked_text_;
}
- (NSRange)markedRange
{
    return has_marked_text_ ? NSMakeRange(0, 1) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange
{
    return NSMakeRange(0, 0);
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    (void)range;
    (void)actual;
    return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return NSNotFound;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    (void)range;
    (void)actual;
    // An input method's candidate window opens at the window's top left corner.
    const NSRect bounds = self.bounds;
    const NSRect corner = NSMakeRect(0, bounds.size.height, 0, 0);
    return self.window != nil ? [self.window convertRectToScreen:[self convertRect:corner toView:nil]] : NSZeroRect;
}

- (void)reportMove:(NSEvent*)event dragging:(BOOL)dragging
{
    if (dragging)
    {
        self.lastMouseEvent = event;  // a drag the program starts now starts from here
    }
    if (self.owner != nullptr && self.owner->PointerLocked())
    {
        // The cursor is detached and parked: the motion is the event's delta (points, accelerated as the system
        // moves the cursor; AppKit gives no unaccelerated deltas), positive right and down.
        self.owner->OnRelativeMotion(event.deltaX, event.deltaY, event.modifierFlags);
        return;
    }
    if (self.owner != nullptr)
    {
        BOOL inside = NO;
        const NSPoint p = [self pixelPointOf:event inside:&inside];
        if (!inside && !dragging)
        {
            return;  // a move outside with no button held is another window's (or nobody's), as on X11 and Windows
        }
        self.owner->OnPointerMove(p.x, p.y, inside, event.modifierFlags);
    }
}
- (void)reportButton:(NSEvent*)event bit:(engine_placeholder::u32)bit down:(bool)down
{
    if (down)
    {
        self.lastMouseEvent = event;
    }
    else if ([NSEvent pressedMouseButtons] == 0)
    {
        self.lastMouseEvent = nil;  // no button held: no drag can start
    }
    if (self.owner != nullptr && bit != 0)
    {
        const NSPoint p = [self pixelPointOf:event inside:nullptr];
        self.owner->OnPointerButton(bit, down, p.x, p.y, event.modifierFlags);
    }
}
- (engine_placeholder::u32)otherButtonBit:(NSEvent*)event
{
    switch (event.buttonNumber)
    {
    case 2: return engine_placeholder::os::kMouseMiddle;
    case 3: return engine_placeholder::os::kMouseBack;
    case 4: return engine_placeholder::os::kMouseForward;
    default: return 0;  // a sixth button and up: not in the engine's mask
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    [self reportMove:event dragging:NO];
}
- (void)mouseDragged:(NSEvent*)event
{
    [self reportMove:event dragging:YES];
}
- (void)rightMouseDragged:(NSEvent*)event
{
    [self reportMove:event dragging:YES];
}
- (void)otherMouseDragged:(NSEvent*)event
{
    [self reportMove:event dragging:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    [self reportButton:event bit:engine_placeholder::os::kMouseLeft down:true];
}
- (void)mouseUp:(NSEvent*)event
{
    [self reportButton:event bit:engine_placeholder::os::kMouseLeft down:false];
}
- (void)rightMouseDown:(NSEvent*)event
{
    [self reportButton:event bit:engine_placeholder::os::kMouseRight down:true];
}
- (void)rightMouseUp:(NSEvent*)event
{
    [self reportButton:event bit:engine_placeholder::os::kMouseRight down:false];
}
- (void)otherMouseDown:(NSEvent*)event
{
    [self reportButton:event bit:[self otherButtonBit:event] down:true];
}
- (void)otherMouseUp:(NSEvent*)event
{
    [self reportButton:event bit:[self otherButtonBit:event] down:false];
}
- (void)mouseEntered:(NSEvent*)event
{
    if (self.owner != nullptr)
    {
        const NSPoint p = [self pixelPointOf:event inside:nullptr];
        self.owner->OnPointerEnter(p.x, p.y, event.modifierFlags);
    }
}
- (void)mouseExited:(NSEvent*)event
{
    if (self.owner != nullptr)
    {
        self.owner->OnPointerLeave(event.modifierFlags);
    }
}
- (void)scrollWheel:(NSEvent*)event
{
    if (self.owner != nullptr)
    {
        const engine_placeholder::os::cocoa::CocoaWheel wheel = engine_placeholder::os::cocoa::CocoaWheelNotches(
            event.scrollingDeltaX, event.scrollingDeltaY, event.hasPreciseScrollingDeltas);
        if (wheel.dx != 0.0 || wheel.dy != 0.0)
        {
            self.owner->OnWheel(wheel.dx, wheel.dy, event.modifierFlags);
        }
    }
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
- (void)windowDidMove:(NSNotification*)notification
{
    (void)notification;
    if (self.owner != nullptr)
    {
        self.owner->OnGeometryChanged(false);  // a locked cursor follows the view's centre
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
        // Holding a key repeats it, as on the other platforms: press-and-hold (the accent menu a held letter opens in a
        // text field) is turned off for this program. Only the registration domain, the fallback below every other
        // one: the user's own settings are not touched, and a user who sets it for this program still wins.
        [[NSUserDefaults standardUserDefaults] registerDefaults:@{@"ApplePressAndHoldEnabled" : @NO}];
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
    held_ = {};
    modifiers_ = 0;
    cursor_shape_ = CursorShape::Arrow;
    image_hash_ = 0;
    lock_ = PointerLockState();
    clipboard_requests_.Init(&events_);
    input_ = input != nullptr ? input : &own_input_;
    // A new window starts with the pointer outside: one reopened on the state its last window closed with (the pointer
    // over it) would otherwise never report the enter (the end-of-phase review).
    input_->pointer_inside = false;
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
        native_->view.owner = this;
        native_->window.contentView = native_->view;
        [native_->window makeFirstResponder:native_->view];
        native_->pasteboard = [NSPasteboard generalPasteboard];
        pasteboard_count_ = native_->pasteboard.changeCount;
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
        ReleaseCursor();  // before the view lets go: the hide is balanced and the mouse attached again
        if (native_->dnd.dragging)
        {
            // A drag this window started outlives it in AppKit; the window says it ended (BACKEND.md), and the view is
            // kept alive for the session's remaining callbacks, which find no owner.
            KeepViewForDrag(native_->view);
            OnDragEnded(DragAction::Nothing);
        }
        clipboard_requests_.FailAll(ClipboardStatus::Failed, MonotonicTimeNanos());
        native_->view.owner = nullptr;
        [native_->view unregisterDraggedTypes];
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
    // The delegate is detached, so windowDidResignKey never comes: the stream still says the keys and the focus went.
    if (!focused_)
    {
        ReleaseKeys();  // keys given to an unfocused window (OnFocusChanged releases a focused one's)
    }
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
            // NSApplication does not pass a key-up with Command held on to the window (it keeps it for key
            // equivalents), which would leave the key down: give it to the window directly.
            if (event.type == NSEventTypeKeyUp && (event.modifierFlags & NSEventModifierFlagCommand) != 0)
            {
                NSWindow* target = event.window != nil ? event.window : NSApp.keyWindow;
                if (target != nil)
                {
                    [target sendEvent:event];
                }
                continue;
            }
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
        const u64 now = MonotonicTimeNanos();
        if (native_ != nullptr)
        {
            if (now >= next_pasteboard_poll_ns_)
            {
                next_pasteboard_poll_ns_ = now + 250'000'000ull;  // changeCount is a call to the pasteboard server
                PollPasteboard();
            }
            DndTick(now);
        }
        clipboard_requests_.Expire(now);
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
    if (locked_)
    {
        WarpToCentre();  // moved, resized or made fullscreen while locked: the parked cursor stays inside the view
    }
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
    if (focused)
    {
        // Modifiers that changed while another program had the keyboard (Cmd-Tab back with Command still held).
        SetModifiers(ModifiersFromCocoa([NSEvent modifierFlags]));
    }
    else
    {
        ReleaseKeys();  // their key-ups go to whichever program has the keyboard now
    }
    if (native_ != nullptr)
    {
        ApplyLock(lock_.SetFocused(focused));  // held only while focused; asked for again on focus while wanted
    }
    InputEvent event;
    event.type = focused ? InputEventType::FocusGained : InputEventType::FocusLost;
    event.modifiers = modifiers_;
    QueueEvent(event);
}

void CocoaWindow::ReleaseKeys()
{
    for (u32 code = 0; code < kCocoaKeyCodeCount; ++code)
    {
        const Key key = held_[code];
        if (key != Key::Unknown)
        {
            held_[code] = Key::Unknown;
            QueueKey(InputEventType::KeyUp, key, false, modifiers_);
        }
    }
    input_->keyboard.ReleaseAll();
    SetModifiers(0);
}

void CocoaWindow::SetModifiers(u32 modifiers)
{
    modifiers_ = modifiers;
    input_->keyboard.modifiers = modifiers;
}

void CocoaWindow::QueueKey(InputEventType type, Key key, bool repeat, u32 modifiers)
{
    InputEvent event;
    event.type = type;
    event.key = key;
    event.repeat = repeat;
    event.modifiers = modifiers;
    QueueEvent(event);
}

bool CocoaWindow::OnKeyDown(u32 key_code, u32 character, u64 flags, bool is_repeat)
{
    const u32 modifiers = ModifiersFromCocoa(flags);
    const bool may_type = (modifiers & (kModCtrl | kModSuper)) == 0;
    if (key_code >= kCocoaKeyCodeCount || IsCocoaModifierKey(key_code))
    {
        return may_type;  // modifier keys come through flagsChanged:
    }
    SetModifiers(modifiers);
    const Key held = held_[key_code];
    // A press of a key already down is a repeat; it keeps the Key it was pressed as (the layout may have changed).
    const bool repeat = is_repeat || held != Key::Unknown;
    const Key key = held != Key::Unknown ? held : KeyFromCocoa(key_code, character);
    if (key != Key::Unknown)
    {
        // A key without an engine name still types its text, but is neither down nor up: nothing could release it.
        held_[key_code] = key;
        input_->keyboard.SetDown(key, true);
        QueueKey(InputEventType::KeyDown, key, repeat, modifiers);
    }
    return may_type;
}

void CocoaWindow::OnKeyUp(u32 key_code, u64 flags)
{
    SetModifiers(ModifiersFromCocoa(flags));
    if (key_code >= kCocoaKeyCodeCount)
    {
        return;
    }
    const Key key = held_[key_code];
    if (key == Key::Unknown)
    {
        return;  // a key whose press this window did not see (Cmd-Q's Q, taken by the menu), or one without a name
    }
    held_[key_code] = Key::Unknown;
    input_->keyboard.SetDown(key, StillHeld(key));
    QueueKey(InputEventType::KeyUp, key, false, modifiers_);
}

bool CocoaWindow::StillHeld(Key key) const
{
    // Two keys can have one name (the digit row's 1 and the number pad's, Return and the pad's Enter): the Key stays
    // down in InputState while either is held. The stream reports each key's own press and release.
    for (u32 code = 0; code < kCocoaKeyCodeCount; ++code)
    {
        if (held_[code] == key)
        {
            return true;
        }
    }
    return false;
}

void CocoaWindow::OnFlagsChanged(u32 key_code, u64 flags)
{
    const u32 before = modifiers_;
    const u32 after = ModifiersFromCocoa(flags);
    if (key_code < kCocoaKeyCodeCount && IsCocoaModifierKey(key_code))
    {
        const Key key = KeyFromCocoa(key_code, 0);
        if (key == Key::CapsLock)
        {
            // macOS reports Caps Lock once per press, as the lock's new state, never the key's release: a tap.
            QueueKey(InputEventType::KeyDown, key, false, before);
            QueueKey(InputEventType::KeyUp, key, false, after);
        }
        else if (key != Key::Unknown)
        {
            const bool down = CocoaModifierKeyDown(key_code, flags);
            if (down && held_[key_code] == Key::Unknown)
            {
                held_[key_code] = key;
                input_->keyboard.SetDown(key, true);
                QueueKey(InputEventType::KeyDown, key, false, before);  // the modifiers held before the key, as on
                                                                        // Linux and Windows
            }
            else if (!down && held_[key_code] != Key::Unknown)
            {
                held_[key_code] = Key::Unknown;
                input_->keyboard.SetDown(key, false);
                QueueKey(InputEventType::KeyUp, key, false, before);
            }
        }
    }
    SetModifiers(after);
}

void CocoaWindow::OnText(const char* utf8, usize length)
{
    // One event per scalar (an input method commits whole words; a dead key with a letter it does not combine with
    // gives both), without the control characters.
    usize offset = 0;
    while (offset < length)
    {
        const DecodedCodepoint decoded = DecodeUtf8At(utf8, length, offset);
        offset += decoded.byte_length;
        if (!IsTypedScalar(decoded.codepoint))
        {
            continue;
        }
        InputEvent event;
        event.type = InputEventType::Text;
        event.text_length = static_cast<u8>(EncodeUtf8(decoded.codepoint, event.text));
        event.modifiers = modifiers_;
        QueueEvent(event);
    }
}

void CocoaWindow::OnPointerEnter(f64 x, f64 y, u64 flags)
{
    if (input_->pointer_inside)
    {
        return;  // a move inside reported it already
    }
    input_->pointer_inside = true;
    input_->mouse_x = x;
    input_->mouse_y = y;
    InputEvent event;
    event.type = InputEventType::PointerEnter;
    event.x = x;
    event.y = y;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
    ApplyCursor();  // the shape or image, or the hide, over the window
}

void CocoaWindow::OnPointerLeave(u64 flags)
{
    if (!input_->pointer_inside)
    {
        return;
    }
    input_->pointer_inside = false;
    InputEvent event;
    event.type = InputEventType::PointerLeave;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
    if (!lock_.Active())
    {
        ApplyCursor();  // a hidden cursor shows again outside the window
        // The window's shape does not follow the pointer out; but a cursor another view or window has set since (its
        // enter can come before this leave) is left alone.
        NSCursor* mine = native_->image_cursor != nil ? native_->image_cursor
                                                      : (__bridge NSCursor*)SystemCursorFor(cursor_shape_);
        if ([NSCursor currentCursor] == mine)
        {
            [[NSCursor arrowCursor] set];
        }
    }
}

void CocoaWindow::OnPointerMove(f64 x, f64 y, bool inside, u64 flags)
{
    // The tracking area's mouseEntered: does not come for a pointer that was already inside when the window opened:
    // the first move inside is the enter then (as on Windows).
    if (inside && !input_->pointer_inside)
    {
        OnPointerEnter(x, y, flags);
    }
    input_->mouse_x = x;
    input_->mouse_y = y;
    InputEvent event;
    event.type = InputEventType::PointerMove;
    event.x = x;
    event.y = y;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
}

void CocoaWindow::OnPointerButton(u32 button, bool down, f64 x, f64 y, u64 flags)
{
    if (down)
    {
        input_->mouse_buttons |= button;
    }
    else
    {
        input_->mouse_buttons &= ~button;
    }
    input_->mouse_x = x;
    input_->mouse_y = y;
    InputEvent event;
    event.type = InputEventType::PointerButton;
    event.button = button;
    event.down = down;
    event.x = x;
    event.y = y;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
}

void CocoaWindow::OnWheel(f64 dx_notches, f64 dy_notches, u64 flags)
{
    // InputState keeps the Linux backends' units, 10 per notch (for a precise scroll: points).
    input_->scroll_dx += dx_notches * kCocoaPointsPerNotch;
    input_->scroll_dy += dy_notches * kCocoaPointsPerNotch;
    InputEvent event;
    event.type = InputEventType::Wheel;
    event.x = dx_notches;
    event.y = dy_notches;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
}

void CocoaWindow::QueueEvent(InputEvent event)
{
    event.time_ns = MonotonicTimeNanos();
    events_.Push(event);
}

}  // namespace engine_placeholder::os::cocoa
