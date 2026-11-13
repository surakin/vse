// The cursor, the pointer lock, the clipboard and drag and drop on macOS (Phase 21 Day 297;
// engine/platform/macos/cocoa_cursor.mm, cocoa_clipboard.mm, cocoa_dragdrop.mm, engine/platform/BACKEND.md "macos").
// The clipboard cases use a private, uniquely named pasteboard (the user's clipboard is never touched); the drop cases
// feed the view a synthesized NSDraggingInfo over a private pasteboard, as AppKit calls a destination. Sending a drag
// needs a real mouse and another program: checked by hand (the day file). Every case opens a real window, so the file
// is in the `display` group and skips while the session is locked.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "display_session.h"
#include "os/os.h"
#include "platform/clipboard.h"
#include "platform/cursor.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "platform/macos/cocoa_window.h"
#include "platform/macos/cocoa_window_native.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

using namespace engine_placeholder;
using engine_placeholder::os::ClipboardFormat;
using engine_placeholder::os::ClipboardKind;
using engine_placeholder::os::ClipboardResult;
using engine_placeholder::os::ClipboardStatus;
using engine_placeholder::os::CursorShape;
using engine_placeholder::os::DragAction;
using engine_placeholder::os::InputEvent;
using engine_placeholder::os::InputEventType;
using engine_placeholder::os::cocoa::CocoaWindow;

// A drag as AppKit describes it to a destination: a pasteboard, the source's operations, a location.
@interface FakeDraggingInfo : NSObject <NSDraggingInfo>
@property(nonatomic, strong) NSPasteboard* pasteboard;
@property(nonatomic, assign) NSDragOperation mask;
@property(nonatomic, assign) NSPoint location;  // in the window
@property(nonatomic, strong) NSWindow* destination;
@end

@implementation FakeDraggingInfo
@synthesize draggingFormation;
@synthesize animatesToDestination;
@synthesize numberOfValidItemsForDrop;
- (NSWindow*)draggingDestinationWindow
{
    return self.destination;
}
- (NSDragOperation)draggingSourceOperationMask
{
    return self.mask;
}
- (NSPoint)draggingLocation
{
    return self.location;
}
- (NSPoint)draggedImageLocation
{
    return self.location;
}
- (NSImage*)draggedImage
{
    return nil;
}
- (NSPasteboard*)draggingPasteboard
{
    return self.pasteboard;
}
- (id)draggingSource
{
    return nil;  // another program
}
- (NSInteger)draggingSequenceNumber
{
    return 1;
}
- (void)slideDraggedImageTo:(NSPoint)screenPoint
{
    (void)screenPoint;
}
- (NSArray<NSString*>*)namesOfPromisedFilesDroppedAtDestination:(NSURL*)dropDestination
{
    (void)dropDestination;
    return @[];
}
- (void)enumerateDraggingItemsWithOptions:(NSDraggingItemEnumerationOptions)enumOpts
                                  forView:(NSView*)view
                                  classes:(NSArray<Class>*)classArray
                            searchOptions:(NSDictionary<NSPasteboardReadingOptionKey, id>*)searchOptions
                               usingBlock:(void (^)(NSDraggingItem*, NSInteger, BOOL*))block
{
    (void)enumOpts;
    (void)view;
    (void)classArray;
    (void)searchOptions;
    (void)block;
}
- (NSSpringLoadingHighlight)springLoadingHighlight
{
    return NSSpringLoadingHighlightNone;
}
- (void)resetSpringLoading
{
}
@end

namespace
{

NSView* ViewOf(const CocoaWindow& window)
{
    return (__bridge NSView*)window.GetNativeInfo().display;
}

NSWindow* WindowOf(const CocoaWindow& window)
{
    return ViewOf(window).window;
}

Vector<InputEvent> Drain(os::Window& window)
{
    Vector<InputEvent> out;
    InputEvent event;
    while (window.PollEvent(&event))
    {
        out.Push(event);
    }
    return out;
}

usize CountOf(const Vector<InputEvent>& events, InputEventType type)
{
    usize n = 0;
    for (usize i = 0; i < events.Size(); ++i)
    {
        n += events[i].type == type ? 1 : 0;
    }
    return n;
}

void OpenQuiet(CocoaWindow& window, const char* title = "cocoa transfer")
{
    REQUIRE(window.Open(title, 320, 200, nullptr));
    window.PumpEvents();
    Drain(window);
}

// A mouse event at (x, y) points from the view's top left.
NSEvent* MouseAt(const CocoaWindow& window, NSEventType type, CGFloat x, CGFloat y)
{
    NSView* view = ViewOf(window);
    const NSPoint in_window = [view convertPoint:NSMakePoint(x, view.bounds.size.height - y) toView:nil];
    return [NSEvent mouseEventWithType:type
                              location:in_window
                         modifierFlags:0
                             timestamp:[[NSProcessInfo processInfo] systemUptime]
                          windowNumber:WindowOf(window).windowNumber
                               context:nil
                           eventNumber:0
                            clickCount:1
                              pressure:1.0f];
}

void PointerIn(const CocoaWindow& window)
{
    [ViewOf(window) mouseMoved:MouseAt(window, NSEventTypeMouseMoved, 20, 20)];
}

void PointerOut(const CocoaWindow& window)
{
    [ViewOf(window) mouseExited:MouseAt(window, NSEventTypeMouseMoved, -5, 20)];
}

NSPasteboard* PrivatePasteboard()
{
    return [NSPasteboard pasteboardWithUniqueName];
}

void Offer(CocoaWindow& window, ClipboardFormat format, const char* bytes, usize size, StringView name = StringView())
{
    os::ClipboardOffer offer;
    offer.format = format;
    offer.custom_name = name;
    offer.data = reinterpret_cast<const byte*>(bytes);
    offer.size = size;
    REQUIRE(window.SetClipboard(ClipboardKind::Clipboard, offer));
}

ClipboardResult Read(CocoaWindow& window, ClipboardFormat format, StringView name = StringView())
{
    const u32 id = window.RequestClipboard(ClipboardKind::Clipboard, format, name);
    REQUIRE(id != 0);
    const Vector<InputEvent> events = Drain(window);
    bool announced = false;
    for (usize i = 0; i < events.Size(); ++i)
    {
        announced = announced || (events[i].type == InputEventType::ClipboardData && events[i].request_id == id);
    }
    CHECK(announced);  // answered at once and announced by its event
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK_FALSE(window.TakeClipboardResult(id, &result));  // once
    return result;
}

bool BytesAre(const Vector<byte>& bytes, const char* text)
{
    usize i = 0;
    for (; text[i] != '\0'; ++i)
    {
        if (i >= bytes.Size() || bytes[i] != static_cast<byte>(text[i]))
        {
            return false;
        }
    }
    return i == bytes.Size();
}

FakeDraggingInfo* Drag(const CocoaWindow& window, NSPasteboard* pasteboard, NSDragOperation mask)
{
    FakeDraggingInfo* info = [[FakeDraggingInfo alloc] init];
    info.pasteboard = pasteboard;
    info.mask = mask;
    info.destination = WindowOf(window);
    NSView* view = ViewOf(window);
    info.location = [view convertPoint:NSMakePoint(30, view.bounds.size.height - 40) toView:nil];  // 30, 40 from top
    return info;
}

id<NSDraggingDestination> Destination(const CocoaWindow& window)
{
    return (id<NSDraggingDestination>)ViewOf(window);
}

}  // namespace

// --- The cursor --------------------------------------------------------------------------------------------------

TEST_CASE("macOS cursor: every shape is an NSCursor, Hidden hides once, and an out-of-range shape is refused",
          "[macos][window][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    CHECK(window.SupportsCursorShape());
    PointerIn(window);
    for (u32 s = 0; s < static_cast<u32>(CursorShape::Count); ++s)
    {
        const CursorShape shape = static_cast<CursorShape>(s);
        INFO("shape " << s);
        REQUIRE(window.SetCursor(shape));
        if (shape == CursorShape::Hidden)
        {
            CHECK(os::cocoa::CursorHideDepth() == 1);
        }
        else
        {
            CHECK(os::cocoa::CursorHideDepth() == 0);
            CHECK([NSCursor currentCursor] == (__bridge NSCursor*)os::cocoa::SystemCursorFor(shape));
        }
    }
    CHECK((__bridge NSCursor*)os::cocoa::SystemCursorFor(CursorShape::IBeam) == [NSCursor IBeamCursor]);
    CHECK((__bridge NSCursor*)os::cocoa::SystemCursorFor(CursorShape::NotAllowed) ==
          [NSCursor operationNotAllowedCursor]);
    CHECK(os::cocoa::CursorHideDepth() == 1);  // Hidden was the last shape
    CHECK_FALSE(window.SetCursor(CursorShape::Count));
    CHECK_FALSE(window.SetCursor(static_cast<CursorShape>(200)));
    CHECK(os::cocoa::CursorHideDepth() == 1);  // a refused shape changes nothing
    REQUIRE(window.SetCursor(CursorShape::Arrow));
    CHECK(os::cocoa::CursorHideDepth() == 0);
}

TEST_CASE("macOS cursor: a hidden cursor is shown again on leave and close and hidden again on enter; the count "
          "always balances",
          "[macos][window][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    {
        CocoaWindow window;
        OpenQuiet(window);
        PointerOut(window);  // the real pointer may have been over the window when it opened
        REQUIRE(window.SetCursor(CursorShape::Hidden));  // the pointer is not inside: nothing hidden
        CHECK(os::cocoa::CursorHideDepth() == 0);
        PointerIn(window);
        CHECK(os::cocoa::CursorHideDepth() == 1);
        REQUIRE(window.SetCursor(CursorShape::Hidden));  // again: still one
        CHECK(os::cocoa::CursorHideDepth() == 1);
        PointerOut(window);
        CHECK(os::cocoa::CursorHideDepth() == 0);
        PointerIn(window);
        CHECK(os::cocoa::CursorHideDepth() == 1);
        window.Close();  // hidden at close
        CHECK(os::cocoa::CursorHideDepth() == 0);
    }
    {
        CocoaWindow window;
        OpenQuiet(window);
        PointerIn(window);
        REQUIRE(window.SetCursor(CursorShape::Hidden));
        CHECK(os::cocoa::CursorHideDepth() == 1);
    }  // destroyed hidden
    CHECK(os::cocoa::CursorHideDepth() == 0);
}

TEST_CASE("macOS cursor: an image is a cursor of its size in points with its hotspot, is not made twice, and clears "
          "back to the shape",
          "[macos][window][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    PointerIn(window);
    REQUIRE(window.SetCursor(CursorShape::Hand));
    Vector<byte> pixels;
    pixels.Resize(64 * 48 * 4);
    for (usize i = 0; i < pixels.Size(); ++i)
    {
        pixels[i] = static_cast<byte>(i % 4 == 3 ? 0xFF : (i * 7) & 0xFF);
    }
    os::CursorImage image;
    image.rgba = pixels.Data();
    image.width = 64;
    image.height = 48;
    image.hotspot_x = 10;
    image.hotspot_y = 6;
    image.scale = 2;
    REQUIRE(window.SetCursorImage(image));
    NSCursor* shown = [NSCursor currentCursor];
    CHECK(shown != [NSCursor pointingHandCursor]);
    CHECK(shown.image.size.width == 32.0);  // 64 pixels at scale 2
    CHECK(shown.image.size.height == 24.0);
    CHECK(shown.hotSpot.x == 5.0);
    CHECK(shown.hotSpot.y == 3.0);
    REQUIRE(window.SetCursorImage(image));  // equal: the same cursor object stays
    CHECK([NSCursor currentCursor] == shown);
    pixels[0] = 0x01;
    REQUIRE(window.SetCursorImage(image));  // different pixels: a new one
    CHECK([NSCursor currentCursor] != shown);
    window.ClearCursorImage();
    CHECK([NSCursor currentCursor] == [NSCursor pointingHandCursor]);
    window.ClearCursorImage();  // no image: nothing
    image.hotspot_x = 64;       // outside
    CHECK_FALSE(window.SetCursorImage(image));
    image.hotspot_x = 0;
    image.scale = 0;
    CHECK_FALSE(window.SetCursorImage(image));
    // An image outranks Hidden: no hide while it shows, the hide comes back when it is cleared.
    image.scale = 1;
    REQUIRE(window.SetCursor(CursorShape::Hidden));
    CHECK(os::cocoa::CursorHideDepth() == 1);
    REQUIRE(window.SetCursorImage(image));
    CHECK(os::cocoa::CursorHideDepth() == 0);
    window.ClearCursorImage();
    CHECK(os::cocoa::CursorHideDepth() == 1);
    window.Close();
    CHECK(os::cocoa::CursorHideDepth() == 0);
}

// --- The pointer lock ----------------------------------------------------------------------------------------------

namespace
{

NSEvent* MoveBy(const CocoaWindow& window, i32 dx, i32 dy)
{
    CGEventRef cg = CGEventCreateCopy([MouseAt(window, NSEventTypeMouseMoved, 50, 50) CGEvent]);
    CGEventSetIntegerValueField(cg, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(cg, kCGMouseEventDeltaY, dy);
    NSEvent* event = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    return event;
}

}  // namespace

TEST_CASE("macOS lock: held only while focused, gives relative motion instead of moves, released by unlock and close",
          "[macos][window][cursor][lock]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    CHECK(window.SupportsPointerLock());
    window.OnFocusChanged(false);  // whatever the desktop did: start unfocused
    Drain(window);
    CGWarpMouseCursorPosition(CGPointMake(5, 5));  // away from the centre, so taking the lock is a real jump
    REQUIRE(window.SetPointerLock(true));
    CHECK_FALSE(window.PointerLocked());  // waits for the focus
    window.OnFocusChanged(true);
    CHECK(window.PointerLocked());  // the detach needs no Accessibility permission: granted at once
    CHECK(os::cocoa::CursorHideDepth() == 1);
    Drain(window);
    [ViewOf(window) mouseMoved:MoveBy(window, 219, -47)];  // the warp's jump, counted into the first delta: dropped
    [ViewOf(window) mouseMoved:MoveBy(window, 7, -3)];
    [ViewOf(window) mouseDragged:MoveBy(window, 0, 5)];
    Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 2);
    CHECK(events[0].type == InputEventType::RelativeMotion);
    CHECK(events[0].x == 7.0);
    CHECK(events[0].y == -3.0);
    CHECK(events[1].type == InputEventType::RelativeMotion);
    CHECK(events[1].y == 5.0);
    window.OnFocusChanged(false);  // Cmd-Tab
    CHECK_FALSE(window.PointerLocked());
    CHECK(os::cocoa::CursorHideDepth() == 0);
    window.OnFocusChanged(true);  // back: asked for again, still wanted
    CHECK(window.PointerLocked());
    REQUIRE(window.SetPointerLock(true));  // a second request: nothing
    CHECK(os::cocoa::CursorHideDepth() == 1);
    REQUIRE(window.SetPointerLock(false));
    CHECK_FALSE(window.PointerLocked());
    CHECK(os::cocoa::CursorHideDepth() == 0);
    // Unlocked, a move is a move again.
    Drain(window);
    [ViewOf(window) mouseMoved:MoveBy(window, 4, 4)];
    events = Drain(window);
    CHECK(CountOf(events, InputEventType::RelativeMotion) == 0);
    CHECK(CountOf(events, InputEventType::PointerMove) == 1);
    REQUIRE(window.SetPointerLock(true));
    REQUIRE(window.PointerLocked());
    window.Close();  // closed locked: the mouse is attached again and the cursor shown
    CHECK_FALSE(window.PointerLocked());
    CHECK(os::cocoa::CursorHideDepth() == 0);
}

// --- The clipboard (a private pasteboard)
// -----------------------------------------------------------------------------

TEST_CASE("macOS clipboard: text round trips, made valid UTF-8; there is no primary selection; bad offers are refused",
          "[macos][window][clipboard]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    NSPasteboard* pasteboard = PrivatePasteboard();
    window.SetPasteboardForTest((__bridge void*)pasteboard);
    CHECK(window.SupportsClipboard(ClipboardKind::Clipboard));
    CHECK_FALSE(window.SupportsClipboard(ClipboardKind::Primary));
    CHECK(window.RequestClipboard(ClipboardKind::Primary, ClipboardFormat::Text, StringView()) == 0);
    const char text[] = "caf\xC3\xA9 \xF0\x9F\x8E\x89 \xFF!";  // a stray byte at the end
    Offer(window, ClipboardFormat::Text, text, sizeof(text) - 1);
    CHECK([[pasteboard stringForType:NSPasteboardTypeString] isEqualToString:@"café 🎉 �!"]);
    CHECK(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Text, StringView()));
    CHECK_FALSE(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
    const ClipboardResult result = Read(window, ClipboardFormat::Text);
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(BytesAre(result.bytes, "caf\xC3\xA9 \xF0\x9F\x8E\x89 \xEF\xBF\xBD!"));
    // (An NSString may hold an unpaired surrogate, but the pasteboard stores such a string as nothing: seen on macOS
    // 15. Typed text can carry one; cocoa_input_macos_test.mm covers it.)
    os::ClipboardOffer empty;
    empty.data = reinterpret_cast<const byte*>(text);
    empty.size = 0;
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, empty));
    empty.size = os::kMaxClipboardBytes + 1;
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, empty));
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Primary, empty));
    [pasteboard releaseGlobally];
}

TEST_CASE("macOS clipboard: a file list round trips as file URLs; a path a reader would refuse is never offered, and "
          "another program's non-local entries are counted",
          "[macos][window][clipboard]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    NSPasteboard* pasteboard = PrivatePasteboard();
    window.SetPasteboardForTest((__bridge void*)pasteboard);
    const char paths[] = "/tmp/a b.txt\n/Users/someone/caf\xC3\xA9.txt";
    Offer(window, ClipboardFormat::FileList, paths, sizeof(paths) - 1);
    NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:@{}];
    REQUIRE(urls.count == 2);
    CHECK(urls[0].fileURL);
    CHECK([urls[0].path isEqualToString:@"/tmp/a b.txt"]);
    CHECK(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::FileList, StringView()));
    const ClipboardResult result = Read(window, ClipboardFormat::FileList);
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(BytesAre(result.bytes, paths));
    CHECK(result.rejected_entries == 0);
    // Refused before anything changes: a relative path, a `..` segment.
    const char relative[] = "notes.txt";
    os::ClipboardOffer bad;
    bad.format = ClipboardFormat::FileList;
    bad.data = reinterpret_cast<const byte*>(relative);
    bad.size = sizeof(relative) - 1;
    const NSInteger before = pasteboard.changeCount;
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, bad));
    const char dots[] = "/tmp/../etc/passwd";
    bad.data = reinterpret_cast<const byte*>(dots);
    bad.size = sizeof(dots) - 1;
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, bad));
    CHECK(pasteboard.changeCount == before);
    // Another program's list: a web link and a `..` path are rejected and counted, the local file kept.
    [pasteboard clearContents];
    [pasteboard writeObjects:@[
        [NSURL fileURLWithPath:@"/tmp/kept.txt"], [NSURL URLWithString:@"https://example.com/x"],
        [NSURL URLWithString:@"file:///tmp/../etc/passwd"]
    ]];
    const ClipboardResult mixed = Read(window, ClipboardFormat::FileList);
    CHECK(mixed.status == ClipboardStatus::Ok);
    CHECK(BytesAre(mixed.bytes, "/tmp/kept.txt"));
    CHECK(mixed.rejected_entries == 2);
    // Only a web link: no file list at all.
    [pasteboard clearContents];
    [pasteboard writeObjects:@[ [NSURL URLWithString:@"https://example.com/y"] ]];
    CHECK(Read(window, ClipboardFormat::FileList).status == ClipboardStatus::Missing);
    [pasteboard releaseGlobally];
}

TEST_CASE("macOS clipboard: a custom format round trips under its name; another name and an empty pasteboard are "
          "Missing",
          "[macos][window][clipboard]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    NSPasteboard* pasteboard = PrivatePasteboard();
    window.SetPasteboardForTest((__bridge void*)pasteboard);
    CHECK(Read(window, ClipboardFormat::Text).status == ClipboardStatus::Missing);
    const char blob[] = {'\x00', '\x01', '\xFE', 'x'};
    const StringView name("com.example.engine-entity");
    Offer(window, ClipboardFormat::Custom, blob, sizeof(blob), name);
    CHECK(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Custom, name));
    CHECK_FALSE(window.ClipboardHasFormat(ClipboardKind::Clipboard, ClipboardFormat::Custom, StringView("com.other")));
    const ClipboardResult result = Read(window, ClipboardFormat::Custom, name);
    REQUIRE(result.status == ClipboardStatus::Ok);
    REQUIRE(result.bytes.Size() == 4);
    CHECK(result.bytes[2] == static_cast<byte>(0xFE));
    CHECK(Read(window, ClipboardFormat::Custom, StringView("com.other")).status == ClipboardStatus::Missing);
    CHECK(Read(window, ClipboardFormat::Text).status == ClipboardStatus::Missing);
    os::ClipboardOffer unnamed;
    unnamed.format = ClipboardFormat::Custom;
    unnamed.data = reinterpret_cast<const byte*>(blob);
    unnamed.size = sizeof(blob);
    CHECK_FALSE(window.SetClipboard(ClipboardKind::Clipboard, unnamed));
    [pasteboard releaseGlobally];
}

TEST_CASE("macOS clipboard: another program's change queues ClipboardChanged at the next pump; the window's own does "
          "not",
          "[macos][window][clipboard]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    NSPasteboard* pasteboard = PrivatePasteboard();
    window.SetPasteboardForTest((__bridge void*)pasteboard);
    window.PumpEvents();
    Drain(window);
    Offer(window, ClipboardFormat::Text, "mine", 4);
    usize own = 0;
    for (int i = 0; i < 40; ++i)
    {
        window.PumpEvents();
        own += CountOf(Drain(window), InputEventType::ClipboardChanged);
        os::SleepNanos(10'000'000);
    }
    CHECK(own == 0);
    [pasteboard clearContents];
    [pasteboard setString:@"theirs" forType:NSPasteboardTypeString];
    // The poll asks the pasteboard server at most every 250 ms: pump for up to a second.
    usize changed = 0;
    for (int i = 0; i < 100 && changed == 0; ++i)
    {
        window.PumpEvents();
        changed = CountOf(Drain(window), InputEventType::ClipboardChanged);
        os::SleepNanos(10'000'000);
    }
    CHECK(changed == 1);
    for (int i = 0; i < 40; ++i)
    {
        window.PumpEvents();
        changed += CountOf(Drain(window), InputEventType::ClipboardChanged);
        os::SleepNanos(10'000'000);
    }
    CHECK(changed == 1);  // once per change
    [pasteboard releaseGlobally];
}

// --- Drag and drop: receiving ----------------------------------------------------------------------------------------

TEST_CASE("macOS drop: text dropped on the window arrives with its events in order, and RequestDropData answers from "
          "the copy",
          "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    CHECK(window.SupportsDragAndDrop());
    os::DropAccept accept;
    accept.formats = os::kDropText | os::kDropFiles;
    window.SetDropAccept(accept);
    CHECK([ViewOf(window).registeredDraggedTypes containsObject:NSPasteboardTypeString]);
    CHECK([ViewOf(window).registeredDraggedTypes containsObject:NSPasteboardTypeFileURL]);
    NSPasteboard* pasteboard = PrivatePasteboard();
    [pasteboard clearContents];
    [pasteboard setString:@"dropped é" forType:NSPasteboardTypeString];
    FakeDraggingInfo* info = Drag(window, pasteboard,
                                  NSDragOperationCopy | NSDragOperationMove | NSDragOperationGeneric);
    id<NSDraggingDestination> view = Destination(window);
    CHECK([view draggingEntered:info] == NSDragOperationCopy);  // the default: Copy, the source allows it
    CHECK([view draggingUpdated:info] == NSDragOperationCopy);
    window.SetDropAction(DragAction::Move);
    CHECK([view draggingUpdated:info] == NSDragOperationMove);
    CHECK([view prepareForDragOperation:info]);
    CHECK([view performDragOperation:info]);
    [pasteboard clearContents];  // the source's pasteboard changes after the drop: the copy is what counts
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 4);
    CHECK(events[0].type == InputEventType::DragEnter);
    CHECK(events[0].drag_formats == os::kDropText);
    CHECK(events[0].drag_actions == (os::DragActionBit(DragAction::Copy) | os::DragActionBit(DragAction::Move)));
    CHECK(events[0].x == 30.0 * window.ContentScale());
    CHECK(events[0].y == 40.0 * window.ContentScale());
    CHECK(events[1].type == InputEventType::DragOver);
    CHECK(events[2].type == InputEventType::DragOver);
    CHECK(events[3].type == InputEventType::Drop);
    CHECK(events[3].drag_action == static_cast<u8>(DragAction::Move));
    const u32 id = window.RequestDropData(ClipboardFormat::Text, StringView());
    REQUIRE(id != 0);
    const Vector<InputEvent> data = Drain(window);
    REQUIRE(data.Size() == 1);
    CHECK(data[0].type == InputEventType::DropData);
    CHECK(data[0].request_id == id);
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(BytesAre(result.bytes, "dropped \xC3\xA9"));
    const u32 files = window.RequestDropData(ClipboardFormat::FileList, StringView());
    REQUIRE(files != 0);
    REQUIRE(window.TakeClipboardResult(files, &result));
    CHECK(result.status == ClipboardStatus::Missing);  // accepted, but not offered
    window.FinishDrop(DragAction::Move);
    CHECK(window.RequestDropData(ClipboardFormat::Text, StringView()) == 0);  // no drop waiting
    [pasteboard releaseGlobally];
}

TEST_CASE("macOS drop: a format the window does not accept is refused unheard; the keys narrow the action as macOS "
          "does; a refusal is a leave",
          "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    id<NSDraggingDestination> view = Destination(window);
    NSPasteboard* pasteboard = PrivatePasteboard();
    [pasteboard clearContents];
    [pasteboard setString:@"text" forType:NSPasteboardTypeString];
    // Nothing accepted (the default), then files only: a text drag is refused and the program hears nothing.
    CHECK([view draggingEntered:Drag(window, pasteboard, NSDragOperationEvery)] == NSDragOperationNone);
    os::DropAccept accept;
    accept.formats = os::kDropFiles;
    window.SetDropAccept(accept);
    CHECK([view draggingEntered:Drag(window, pasteboard, NSDragOperationEvery)] == NSDragOperationNone);
    CHECK([view draggingUpdated:Drag(window, pasteboard, NSDragOperationEvery)] == NSDragOperationNone);
    CHECK_FALSE([view performDragOperation:Drag(window, pasteboard, NSDragOperationEvery)]);
    CHECK(Drain(window).Size() == 0);
    accept.formats = os::kDropText;
    window.SetDropAccept(accept);
    // Option held: the source offers copy only. Command held: Finder offers the generic (move) operation only.
    CHECK([view draggingEntered:Drag(window, pasteboard, NSDragOperationCopy)] == NSDragOperationCopy);
    CHECK([view draggingEntered:Drag(window, pasteboard, NSDragOperationGeneric)] == NSDragOperationGeneric);
    CHECK([view draggingEntered:Drag(window, pasteboard, NSDragOperationLink)] == NSDragOperationNone);
    // The program answers Nothing: the drop is refused and reported as a leave.
    FakeDraggingInfo* info = Drag(window, pasteboard, NSDragOperationCopy);
    [view draggingEntered:info];
    window.SetDropAction(DragAction::Nothing);
    CHECK([view draggingUpdated:info] == NSDragOperationNone);
    CHECK_FALSE([view performDragOperation:info]);
    Vector<InputEvent> events = Drain(window);
    CHECK(events[events.Size() - 1].type == InputEventType::DragLeave);
    CHECK(CountOf(events, InputEventType::Drop) == 0);
    // An answer the source does not allow is no answer.
    info = Drag(window, pasteboard, NSDragOperationCopy);
    [view draggingEntered:info];
    window.SetDropAction(DragAction::Link);
    CHECK([view draggingUpdated:info] == NSDragOperationNone);
    [view draggingExited:info];
    events = Drain(window);
    CHECK(events[events.Size() - 1].type == InputEventType::DragLeave);
    [pasteboard releaseGlobally];
}

TEST_CASE("macOS drop: only the first 64 types are looked at; a dropped file list is checked and cut at 10,000 "
          "entries",
          "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    id<NSDraggingDestination> view = Destination(window);
    os::DropAccept accept;
    accept.formats = os::kDropText | os::kDropFiles;
    window.SetDropAccept(accept);
    // 100 made-up types, then the text: the text is past the first 64, so the drag offers nothing we take.
    NSPasteboard* crowded = PrivatePasteboard();
    [crowded clearContents];
    NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
    for (int i = 0; i < 100; ++i)
    {
        [item setString:@"x" forType:[NSString stringWithFormat:@"com.example.filler.%d", i]];
    }
    [item setString:@"late" forType:NSPasteboardTypeString];
    [crowded writeObjects:@[ item ]];
    REQUIRE(crowded.types.count > 64);
    CHECK([view draggingEntered:Drag(window, crowded, NSDragOperationCopy)] == NSDragOperationNone);
    // 10,050 local files and a web link.
    NSPasteboard* files = PrivatePasteboard();
    [files clearContents];
    NSMutableArray* urls = [NSMutableArray array];
    for (int i = 0; i < 10050; ++i)
    {
        [urls addObject:[NSURL fileURLWithPath:[NSString stringWithFormat:@"/tmp/f%05d", i]]];
    }
    [urls addObject:[NSURL URLWithString:@"https://example.com/z"]];
    [files writeObjects:urls];
    FakeDraggingInfo* info = Drag(window, files, NSDragOperationCopy);
    CHECK([view draggingEntered:info] == NSDragOperationCopy);
    CHECK([view performDragOperation:info]);
    const u32 id = window.RequestDropData(ClipboardFormat::FileList, StringView());
    REQUIRE(id != 0);
    ClipboardResult result;
    REQUIRE(window.TakeClipboardResult(id, &result));
    REQUIRE(result.status == ClipboardStatus::Ok);
    usize lines = result.bytes.Size() != 0 ? 1 : 0;
    for (usize i = 0; i < result.bytes.Size(); ++i)
    {
        lines += result.bytes[i] == '\n' ? 1 : 0;
    }
    CHECK(lines == os::kMaxDropFileEntries);
    CHECK(result.rejected_entries == 51);  // the 50 past the cut and the web link
    window.FinishDrop(DragAction::Copy);
    [crowded releaseGlobally];
    [files releaseGlobally];
}

TEST_CASE("macOS drop: a custom format is accepted by its name only", "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    id<NSDraggingDestination> view = Destination(window);
    os::DropAccept accept;
    accept.formats = os::kDropCustom;
    accept.custom_name = StringView("com.example.engine-entity");
    window.SetDropAccept(accept);
    CHECK([ViewOf(window).registeredDraggedTypes containsObject:@"com.example.engine-entity"]);
    NSPasteboard* pasteboard = PrivatePasteboard();
    [pasteboard clearContents];
    const char blob[] = {'e', 'n', '\x00', 't'};
    [pasteboard setData:[NSData dataWithBytes:blob length:4] forType:@"com.example.engine-entity"];
    FakeDraggingInfo* info = Drag(window, pasteboard, NSDragOperationCopy);
    REQUIRE([view draggingEntered:info] == NSDragOperationCopy);
    REQUIRE([view performDragOperation:info]);
    ClipboardResult result;
    const u32 other = window.RequestDropData(ClipboardFormat::Custom, StringView("com.example.other"));
    REQUIRE(window.TakeClipboardResult(other, &result));
    CHECK(result.status == ClipboardStatus::Missing);
    const u32 id = window.RequestDropData(ClipboardFormat::Custom, StringView("com.example.engine-entity"));
    REQUIRE(window.TakeClipboardResult(id, &result));
    CHECK(result.status == ClipboardStatus::Ok);
    CHECK(result.bytes.Size() == 4);
    window.FinishDrop(DragAction::Copy);
    // A custom accept without a name accepts nothing custom.
    accept.custom_name = StringView();
    window.SetDropAccept(accept);
    CHECK(ViewOf(window).registeredDraggedTypes.count == 0);
    [pasteboard releaseGlobally];
}

// --- Drag and drop: sending ------------------------------------------------------------------------------------------

TEST_CASE("macOS drag: BeginDrag refuses without a button held, without offers and with too many",
          "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    const char text[] = "drag me";
    os::ClipboardOffer offer;
    offer.format = ClipboardFormat::Text;
    offer.data = reinterpret_cast<const byte*>(text);
    offer.size = sizeof(text) - 1;
    os::DragSource source;
    source.offers = &offer;
    source.offer_count = 1;
    CHECK_FALSE(window.BeginDrag(source));  // no button is held
    CHECK_FALSE(window.Dragging());
    os::DragSource none;
    CHECK_FALSE(window.BeginDrag(none));
    const os::ClipboardOffer four[4] = {offer, offer, offer, offer};
    source.offers = four;
    source.offer_count = 4;
    CHECK_FALSE(window.BeginDrag(source));
    window.CancelDrag();  // nothing running: nothing happens
    CHECK(Drain(window).Size() == 0);
}

// Hand check, Day 297: after a drag out of the window, the button the drag started with stayed down: AppKit's drag loop
// takes its release.
TEST_CASE("macOS drag: the end of a drag releases the buttons no longer held, before DragEnded",
          "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState state;
    CocoaWindow window;
    REQUIRE(window.Open("cocoa drag end", 320, 200, &state));
    window.PumpEvents();
    Drain(window);
    [ViewOf(window) mouseDown:MouseAt(window, NSEventTypeLeftMouseDown, 20, 20)];  // the press a drag starts from
    REQUIRE(state.IsMouseDown(os::kMouseLeft));
    Drain(window);
    window.OnDragEnded(DragAction::Copy);  // as draggingSession:endedAtPoint:operation: reports it
    CHECK_FALSE(state.IsMouseDown(os::kMouseLeft));
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 2);
    CHECK(events[0].type == InputEventType::PointerButton);
    CHECK(events[0].button == os::kMouseLeft);
    CHECK_FALSE(events[0].down);
    CHECK(events[1].type == InputEventType::DragEnded);
    CHECK(events[1].drag_action == static_cast<u8>(DragAction::Copy));
}

// Review findings, Day 297.

TEST_CASE("macOS lock: the cursor follows the view's centre when the window changes size, and a focus regained "
          "without a jump keeps the first move",
          "[macos][window][cursor][lock]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    window.OnFocusChanged(true);
    REQUIRE(window.SetPointerLock(true));
    REQUIRE(window.PointerLocked());
    NSWindow* ns = WindowOf(window);
    [ns setContentSize:NSMakeSize(420, 300)];
    window.PumpEvents();
    const NSRect view_on_screen = [ns convertRectToScreen:[ViewOf(window) convertRect:ViewOf(window).bounds
                                                                               toView:nil]];
    const NSPoint at = [NSEvent mouseLocation];
    CHECK(at.x > NSMidX(view_on_screen) - 2.0);
    CHECK(at.x < NSMidX(view_on_screen) + 2.0);
    CHECK(at.y > NSMidY(view_on_screen) - 2.0);
    CHECK(at.y < NSMidY(view_on_screen) + 2.0);
    // Cmd-Tab away and back: the cursor never left the centre, so the first motion is the user's and is kept.
    window.OnFocusChanged(false);
    window.OnFocusChanged(true);
    REQUIRE(window.PointerLocked());
    Drain(window);
    [ViewOf(window) mouseMoved:MoveBy(window, 3, 2)];
    const Vector<InputEvent> events = Drain(window);
    REQUIRE(events.Size() == 1);
    CHECK(events[0].type == InputEventType::RelativeMotion);
    CHECK(events[0].x == 3.0);
    window.Close();
    CHECK(os::cocoa::CursorHideDepth() == 0);
}

TEST_CASE("macOS cursor: leaving sets the arrow only over the window's own cursor, not one another view set first",
          "[macos][window][cursor]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    PointerIn(window);
    REQUIRE(window.SetCursor(CursorShape::Hand));
    PointerOut(window);
    CHECK([NSCursor currentCursor] == [NSCursor arrowCursor]);  // its own hand: back to the arrow
    PointerIn(window);
    [[NSCursor IBeamCursor] set];  // another window's enter, delivered before this window's leave
    PointerOut(window);
    CHECK([NSCursor currentCursor] == [NSCursor IBeamCursor]);
}

TEST_CASE("macOS drag: closing the window during its own drag reports DragEnded", "[macos][window][dragdrop]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    OpenQuiet(window);
    window.NativeState()->dnd.dragging = true;  // as BeginDrag leaves it while AppKit's session runs
    REQUIRE(window.Dragging());
    window.Close();
    CHECK_FALSE(window.Dragging());
    const Vector<InputEvent> events = Drain(window);
    usize ended = 0;
    for (usize i = 0; i < events.Size(); ++i)
    {
        if (events[i].type == InputEventType::DragEnded)
        {
            ++ended;
            CHECK(events[i].drag_action == static_cast<u8>(DragAction::Nothing));
        }
    }
    CHECK(ended == 1);
}
