#pragma once

// The Objective-C side of CocoaWindow (Phase 21 Days 295-297), shared by the files that implement it:
// cocoa_window.mm (the window, the view, input), cocoa_cursor.mm (cursor and pointer lock), cocoa_clipboard.mm and
// cocoa_dragdrop.mm. Objective-C++ only; never included by C++ code (cocoa_window.h is the C++ interface).
#include "containers/array.h"
#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/drag_drop.h"
#include "platform/macos/cocoa_window.h"

#import <AppKit/AppKit.h>

@class EngineWindowDelegate;

// The content view: input (cocoa_window.mm), the cursor (cocoa_cursor.mm), the drag destination and source
// (cocoa_dragdrop.mm). `owner` is cleared by Close before anything is released.
@interface EngineMetalView : NSView <NSTextInputClient>
@property(nonatomic, assign) vse::os::cocoa::CocoaWindow* owner;
// The last mouse-down or drag event while a button is held (a drag starts from it); nil when no button is down.
@property(nonatomic, strong) NSEvent* lastMouseEvent;
@end

// The cursor (cocoa_cursor.mm): cursorUpdate: from the tracking area.
@interface EngineMetalView (Cursor)
- (void)cursorUpdate:(NSEvent*)event;
@end

// Drag and drop (cocoa_dragdrop.mm): the view is both the destination of drags from other programs and the source of
// the window's own.
@interface EngineMetalView (Dragging) <NSDraggingSource>
@end

namespace vse::os::cocoa
{

// One format of a drop, read while performDragOperation: ran (the dragging pasteboard is valid only then).
struct CocoaDroppedFormat
{
    bool present = false;
    ClipboardStatus status = ClipboardStatus::Missing;
    Vector<byte> bytes;
    u32 replaced = 0;
    u32 rejected = 0;
};

// Drag and drop state (cocoa_dragdrop.mm).
struct CocoaDnd
{
    u8 accept_formats = 0;    // kDropText | kDropFiles | kDropCustom
    NSString* accept_custom;  // the custom type, or nil
    bool session = false;     // a drag from another program is over the window with a format we accept
    u8 offered = 0;           // the accepted formats it offers
    u8 allowed = 0;           // DragAction bits the source allows
    bool answered = false;    // SetDropAction was called in this session
    DragAction answer = DragAction::Nothing;
    bool drop_waiting = false;  // a Drop was queued and FinishDrop has not come
    u64 drop_time_ns = 0;
    Array<CocoaDroppedFormat, 3> dropped;  // text, files, custom
    bool dragging = false;                 // a drag this window started is running
    bool cancelled = false;                // CancelDrag was called during it
    u8 outgoing_allowed = 0;               // its allowed actions
};

struct CocoaWindow::Native
{
    NSWindow* window = nil;
    EngineMetalView* view = nil;
    EngineWindowDelegate* delegate = nil;
    NSCursor* image_cursor = nil;    // the custom image's cursor, or nil
    NSPasteboard* pasteboard = nil;  // the general pasteboard, or a test's private one
    CocoaDnd dnd;
};

// The pasteboard types of the formats (cocoa_clipboard.mm).
NSString* PasteboardTypeOf(ClipboardFormat format, StringView custom_name);

// Reads one format from a pasteboard with the shared rules (SanitizeUtf8, ParseUriList, the size limit, the file-list
// cut); used by the clipboard and by a drop (cocoa_clipboard.mm).
ClipboardStatus ReadPasteboard(NSPasteboard* pasteboard, ClipboardFormat format, NSString* type, Vector<byte>* out,
                               u32* replaced, u32* rejected);

// An NSString as valid UTF-8: unpaired surrogates (an NSString may hold them; UTF8String then gives nothing) become
// U+FFFD and are counted (cocoa_clipboard.mm).
void Utf8Of(NSString* text, Vector<byte>* out, u32* replaced);

// An NSString of UTF-8 bytes; invalid bytes give the empty string (cocoa_clipboard.mm).
NSString* StringOfBytes(const byte* data, usize size);

// An image of premultiplied RGBA8 pixels, `scale` pixels to the point, or nil (cocoa_cursor.mm; the cursor image and
// the drag image).
NSImage* ImageOfPixels(const byte* rgba, u32 width, u32 height, CGFloat scale);

// Keeps a view alive until the drag session it is the source of ends (a window closed during its own drag), and lets
// it go at the end (cocoa_dragdrop.mm).
void KeepViewForDrag(EngineMetalView* view);

// The file URLs of `\n`-separated absolute paths, or nil if one is refused by the shared URI rules
// (cocoa_clipboard.mm).
NSArray<NSURL*>* FileUrlsOf(const byte* paths, usize size);

}  // namespace vse::os::cocoa
