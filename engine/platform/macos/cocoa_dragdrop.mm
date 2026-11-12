// Drag and drop of CocoaWindow (Phase 26 Day 313; platform/macos/cocoa_window.h, BACKEND.md "macos").
//
// Receiving: the view is an NSDraggingDestination registered for the accepted formats' pasteboard types; entered,
// updated, exited and performDragOperation: become DragEnter, DragOver, DragLeave and Drop. The dragging pasteboard is
// the source's to change once the drop is over, so every accepted format is read during performDragOperation: and
// RequestDropData answers from that copy (FinishDrop, or the session timeout, drops it). macOS narrows the source's
// operation mask itself for the keys held (Option: copy; Command: move), so the default answer is Copy where allowed,
// else Move. Sending: beginDraggingSessionWithItems:event:source: from the mouse-down that is still held, the offers
// copied into the items; the session's end is DragEnded with what the target did. AppKit's session cannot be stopped:
// CancelDrag makes the source's mask none from AppKit's next query, so a target that has not accepted yet refuses; one
// that already had may still take the data, and DragEnded then says so.
#include "platform/macos/cocoa_window.h"

#include "cstr/mem_ops.h"
#include "os/os.h"
#include "platform/macos/cocoa_window_native.h"

#import <AppKit/AppKit.h>

using engine_placeholder::u8;
using engine_placeholder::os::DragAction;
using engine_placeholder::os::DragActionBit;
using engine_placeholder::os::cocoa::CocoaDnd;
using engine_placeholder::os::cocoa::CocoaWindow;

namespace
{

NSMutableSet<EngineMetalView*>* g_drag_sources = nil;  // views whose window closed during their own drag (main thread)

// The source's NSDragOperation mask as DragAction bits; Generic (what Finder offers for a move) counts as a move.
u8 ActionsOfMask(NSDragOperation mask)
{
    u8 bits = 0;
    if ((mask & NSDragOperationCopy) != 0)
    {
        bits |= DragActionBit(DragAction::Copy);
    }
    if ((mask & (NSDragOperationMove | NSDragOperationGeneric)) != 0)
    {
        bits |= DragActionBit(DragAction::Move);
    }
    if ((mask & NSDragOperationLink) != 0)
    {
        bits |= DragActionBit(DragAction::Link);
    }
    return bits;
}

NSDragOperation OperationOf(DragAction action, NSDragOperation mask)
{
    switch (action)
    {
    case DragAction::Copy: return NSDragOperationCopy;
    case DragAction::Move: return (mask & NSDragOperationMove) != 0 ? NSDragOperationMove : NSDragOperationGeneric;
    case DragAction::Link: return NSDragOperationLink;
    case DragAction::Nothing: break;
    }
    return NSDragOperationNone;
}

NSDragOperation MaskOfActions(u8 bits)
{
    NSDragOperation mask = NSDragOperationNone;
    if ((bits & DragActionBit(DragAction::Copy)) != 0)
    {
        mask |= NSDragOperationCopy;
    }
    if ((bits & DragActionBit(DragAction::Move)) != 0)
    {
        mask |= NSDragOperationMove | NSDragOperationGeneric;
    }
    if ((bits & DragActionBit(DragAction::Link)) != 0)
    {
        mask |= NSDragOperationLink;
    }
    return mask;
}

DragAction ActionOfOperation(NSDragOperation operation)
{
    if ((operation & (NSDragOperationMove | NSDragOperationGeneric | NSDragOperationDelete)) != 0)
    {
        return DragAction::Move;  // the target took it (Delete: the Trash)
    }
    if ((operation & NSDragOperationCopy) != 0)
    {
        return DragAction::Copy;
    }
    if ((operation & NSDragOperationLink) != 0)
    {
        return DragAction::Link;
    }
    return DragAction::Nothing;
}

}  // namespace

@implementation EngineMetalView (Dragging)

- (NSPoint)pixelPointOfDrag:(id<NSDraggingInfo>)sender
{
    const NSPoint p = [self convertPoint:sender.draggingLocation fromView:nil];
    const CGFloat scale = self.window != nil ? self.window.backingScaleFactor : 1.0;
    return NSMakePoint(p.x * scale, (self.bounds.size.height - p.y) * scale);
}

// The accepted formats the drag offers, from at most kMaxDragTypes of its types.
- (u8)offeredFormats:(id<NSDraggingInfo>)sender dnd:(const CocoaDnd&)dnd
{
    NSArray<NSPasteboardType>* types = sender.draggingPasteboard.types;
    u8 offered = 0;
    const NSUInteger count = types.count < engine_placeholder::os::kMaxDragTypes
                                 ? types.count
                                 : engine_placeholder::os::kMaxDragTypes;
    for (NSUInteger i = 0; i < count; ++i)
    {
        NSString* type = types[i];
        if ([type isEqualToString:NSPasteboardTypeString])
        {
            offered |= engine_placeholder::os::kDropText;
        }
        else if ([type isEqualToString:NSPasteboardTypeFileURL])
        {
            offered |= engine_placeholder::os::kDropFiles;
        }
        else if (dnd.accept_custom != nil && [type isEqualToString:dnd.accept_custom])
        {
            offered |= engine_placeholder::os::kDropCustom;
        }
    }
    return static_cast<u8>(offered & dnd.accept_formats);
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if (self.owner == nullptr)
    {
        return NSDragOperationNone;
    }
    CocoaDnd& dnd = self.owner->NativeState()->dnd;
    dnd.session = false;
    const u8 offered = [self offeredFormats:sender dnd:dnd];
    if (offered == 0)
    {
        return NSDragOperationNone;  // refused, and the program hears nothing of it
    }
    const NSPoint p = [self pixelPointOfDrag:sender];
    self.owner->OnDragEnter(p.x, p.y, offered, ActionsOfMask(sender.draggingSourceOperationMask));
    return OperationOf(self.owner->ChooseDropAction(), sender.draggingSourceOperationMask);
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender
{
    if (self.owner == nullptr || !self.owner->NativeState()->dnd.session)
    {
        return NSDragOperationNone;
    }
    const NSPoint p = [self pixelPointOfDrag:sender];
    self.owner->OnDragOver(p.x, p.y, ActionsOfMask(sender.draggingSourceOperationMask));  // the keys may have changed
    return OperationOf(self.owner->ChooseDropAction(), sender.draggingSourceOperationMask);
}

- (void)draggingExited:(id<NSDraggingInfo>)sender
{
    (void)sender;
    if (self.owner != nullptr)
    {
        self.owner->OnDragLeave();
    }
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender
{
    (void)sender;
    return self.owner != nullptr && self.owner->NativeState()->dnd.session;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    if (self.owner == nullptr || !self.owner->NativeState()->dnd.session)
    {
        return NO;
    }
    CocoaDnd& dnd = self.owner->NativeState()->dnd;
    const u8 allowed = ActionsOfMask(sender.draggingSourceOperationMask);
    dnd.allowed = allowed;
    const DragAction action = self.owner->ChooseDropAction();
    if (action == DragAction::Nothing)
    {
        self.owner->OnDragLeave();  // refused here: the source gets no drop, the program a leave
        return NO;
    }
    // Read every accepted format now: the pasteboard is the source's again once the drop is over.
    NSPasteboard* pasteboard = sender.draggingPasteboard;
    const engine_placeholder::Array<engine_placeholder::os::ClipboardFormat, 3> formats = {
        {engine_placeholder::os::ClipboardFormat::Text, engine_placeholder::os::ClipboardFormat::FileList,
         engine_placeholder::os::ClipboardFormat::Custom}};
    const engine_placeholder::Array<u8, 3> bits = {
        {engine_placeholder::os::kDropText, engine_placeholder::os::kDropFiles, engine_placeholder::os::kDropCustom}};
    for (engine_placeholder::usize i = 0; i < 3; ++i)
    {
        engine_placeholder::os::cocoa::CocoaDroppedFormat& f = dnd.dropped[i];
        f = engine_placeholder::os::cocoa::CocoaDroppedFormat{};
        if ((dnd.offered & bits[i]) == 0)
        {
            continue;
        }
        f.present = true;
        NSString* type = i == 2 ? dnd.accept_custom
                                : engine_placeholder::os::cocoa::PasteboardTypeOf(formats[i],
                                                                                  engine_placeholder::StringView());
        f.status = engine_placeholder::os::cocoa::ReadPasteboard(pasteboard, formats[i], type, &f.bytes, &f.replaced,
                                                                 &f.rejected);
        if (f.status != engine_placeholder::os::ClipboardStatus::Ok)
        {
            f.bytes.Clear();
        }
    }
    const NSPoint p = [self pixelPointOfDrag:sender];
    self.owner->OnDrop(p.x, p.y, allowed, action);
    return YES;
}

// NSDraggingSource: the window's own drag.
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    (void)session;
    (void)context;
    if (self.owner == nullptr)
    {
        return NSDragOperationNone;
    }
    const CocoaDnd& dnd = self.owner->NativeState()->dnd;
    return dnd.cancelled ? NSDragOperationNone : MaskOfActions(dnd.outgoing_allowed);
}

- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)point operation:(NSDragOperation)operation
{
    (void)session;
    (void)point;
    if (self.owner != nullptr)
    {
        // What the target did, even after CancelDrag: a target that had accepted before the cancel may still have
        // taken the data, and a source must not believe it did not.
        self.owner->OnDragEnded(ActionOfOperation(operation));
    }
    [g_drag_sources removeObject:self];  // a closed window's view, kept for this call
}

@end

namespace engine_placeholder::os::cocoa
{

namespace
{

// A drag image from the program's pixels, `scale` pixels to the point; null pixels or a size out of range: none.
NSImage* DragImageOf(const DragImage& image, CGFloat scale)
{
    if (image.rgba == nullptr || image.width == 0 || image.height == 0 || image.width > kMaxCursorImageSize ||
        image.height > kMaxCursorImageSize)
    {
        return nil;
    }
    return ImageOfPixels(image.rgba, image.width, image.height, scale);
}

}  // namespace

void KeepViewForDrag(EngineMetalView* view)
{
    if (g_drag_sources == nil)
    {
        g_drag_sources = [NSMutableSet set];
    }
    [g_drag_sources addObject:view];
}

void CocoaWindow::SetDropAccept(const DropAccept& accept)
{
    if (native_ == nullptr)
    {
        return;
    }
    CocoaDnd& dnd = native_->dnd;
    dnd.accept_formats = accept.formats & (kDropText | kDropFiles | kDropCustom);
    dnd.accept_custom = (accept.formats & kDropCustom) != 0
                            ? PasteboardTypeOf(ClipboardFormat::Custom, accept.custom_name)
                            : nil;
    if (dnd.accept_custom == nil)
    {
        dnd.accept_formats &= static_cast<u8>(~kDropCustom);
    }
    NSMutableArray<NSPasteboardType>* types = [NSMutableArray array];
    if ((dnd.accept_formats & kDropText) != 0)
    {
        [types addObject:NSPasteboardTypeString];
    }
    if ((dnd.accept_formats & kDropFiles) != 0)
    {
        [types addObject:NSPasteboardTypeFileURL];
    }
    if (dnd.accept_custom != nil)
    {
        [types addObject:dnd.accept_custom];
    }
    [native_->view unregisterDraggedTypes];
    if (types.count != 0)
    {
        [native_->view registerForDraggedTypes:types];
    }
}

void CocoaWindow::SetDropAction(DragAction action)
{
    if (native_ == nullptr || !native_->dnd.session)
    {
        return;
    }
    native_->dnd.answer = action;
    native_->dnd.answered = true;
}

DragAction CocoaWindow::ChooseDropAction() const
{
    const CocoaDnd& dnd = native_->dnd;
    DragAction action = DragAction::Nothing;
    if (dnd.answered)
    {
        action = dnd.answer;
    }
    else if ((dnd.allowed & DragActionBit(DragAction::Copy)) != 0)
    {
        action = DragAction::Copy;
    }
    else if ((dnd.allowed & DragActionBit(DragAction::Move)) != 0)
    {
        action = DragAction::Move;  // Command held: macOS left only the move
    }
    if (action != DragAction::Nothing && (dnd.allowed & DragActionBit(action)) == 0)
    {
        action = DragAction::Nothing;
    }
    return action;
}

void CocoaWindow::OnDragEnter(f64 x, f64 y, u8 offered, u8 allowed)
{
    CocoaDnd& dnd = native_->dnd;
    dnd.session = true;
    dnd.offered = offered;
    dnd.allowed = allowed;
    dnd.answered = false;
    InputEvent event;
    event.type = InputEventType::DragEnter;
    event.modifiers = modifiers_;
    event.x = x;
    event.y = y;
    event.drag_formats = offered;
    event.drag_actions = allowed;
    QueueEvent(event);
}

void CocoaWindow::OnDragOver(f64 x, f64 y, u8 allowed)
{
    native_->dnd.allowed = allowed;
    InputEvent event;
    event.type = InputEventType::DragOver;
    event.modifiers = modifiers_;
    event.x = x;
    event.y = y;
    event.drag_actions = allowed;
    QueueEvent(event);
}

void CocoaWindow::OnDragLeave()
{
    CocoaDnd& dnd = native_->dnd;
    if (!dnd.session)
    {
        return;
    }
    dnd.session = false;
    InputEvent event;
    event.type = InputEventType::DragLeave;
    QueueEvent(event);
}

void CocoaWindow::OnDrop(f64 x, f64 y, u8 allowed, DragAction action)
{
    CocoaDnd& dnd = native_->dnd;
    dnd.session = false;
    dnd.drop_waiting = true;
    dnd.drop_time_ns = MonotonicTimeNanos();
    InputEvent event;
    event.type = InputEventType::Drop;
    event.modifiers = modifiers_;
    event.x = x;
    event.y = y;
    event.drag_formats = dnd.offered;
    event.drag_actions = allowed;
    event.drag_action = static_cast<u8>(action);
    QueueEvent(event);
}

u32 CocoaWindow::RequestDropData(ClipboardFormat format, StringView custom_name)
{
    if (native_ == nullptr || !native_->dnd.drop_waiting)
    {
        return 0;
    }
    const u64 now = MonotonicTimeNanos();
    const u32 id = clipboard_requests_.Begin(ClipboardKind::Clipboard, format, now, InputEventType::DropData);
    if (id == 0)
    {
        return 0;
    }
    const CocoaDnd& dnd = native_->dnd;
    const usize index = format == ClipboardFormat::Text ? 0 : format == ClipboardFormat::FileList ? 1 : 2;
    const CocoaDroppedFormat& f = dnd.dropped[index];
    bool name_matches = index != 2;
    if (!name_matches)
    {
        NSString* type = PasteboardTypeOf(ClipboardFormat::Custom, custom_name);
        name_matches = type != nil && dnd.accept_custom != nil && [type isEqualToString:dnd.accept_custom];
    }
    if (!f.present || !name_matches)
    {
        clipboard_requests_.Fail(id, ClipboardStatus::Missing, now);
        return id;
    }
    Vector<byte> copy;
    copy.Resize(f.bytes.Size());
    if (copy.Size() != 0)
    {
        MemCopy(copy.Data(), f.bytes.Data(), copy.Size());
    }
    clipboard_requests_.Complete(id, f.status, static_cast<Vector<byte>&&>(copy), f.replaced, f.rejected, now);
    return id;
}

void CocoaWindow::FinishDrop([[maybe_unused]] DragAction taken)
{
    // AppKit took the answer when performDragOperation: returned; finishing drops the copy of the data.
    if (native_ == nullptr)
    {
        return;
    }
    native_->dnd.drop_waiting = false;
    for (usize i = 0; i < native_->dnd.dropped.Size(); ++i)
    {
        native_->dnd.dropped[i] = CocoaDroppedFormat{};
    }
}

void CocoaWindow::DndTick(u64 now_ns)
{
    if (native_->dnd.drop_waiting && now_ns - native_->dnd.drop_time_ns > kDropSessionTimeoutNs)
    {
        FinishDrop(DragAction::Nothing);
    }
}

bool CocoaWindow::BeginDrag(const DragSource& source)
{
    if (native_ == nullptr || native_->dnd.dragging || source.offers == nullptr || source.offer_count == 0 ||
        source.offer_count > kMaxDragOffers)
    {
        return false;
    }
    EngineMetalView* view = native_->view;
    NSEvent* event = view.lastMouseEvent;
    if (event == nil || [NSEvent pressedMouseButtons] == 0)
    {
        return false;  // a drag starts only from a press, or a move with it held
    }
    @autoreleasepool
    {
        NSMutableArray<NSDraggingItem*>* items = [NSMutableArray array];
        NSPasteboardItem* item = nil;  // text and the custom format; each file is its own item
        for (usize i = 0; i < source.offer_count; ++i)
        {
            const ClipboardOffer& offer = source.offers[i];
            if (offer.data == nullptr || offer.size == 0 || offer.size > kMaxClipboardBytes)
            {
                return false;
            }
            if (offer.format == ClipboardFormat::FileList)
            {
                NSArray<NSURL*>* urls = FileUrlsOf(offer.data, offer.size);
                if (urls == nil)
                {
                    return false;
                }
                for (NSURL* url in urls)
                {
                    [items addObject:[[NSDraggingItem alloc] initWithPasteboardWriter:url]];
                }
                continue;
            }
            if (item == nil)
            {
                item = [[NSPasteboardItem alloc] init];
            }
            if (offer.format == ClipboardFormat::Text)
            {
                Vector<byte> clean;
                SanitizeUtf8(offer.data, offer.size, clean, nullptr);
                [item setString:StringOfBytes(clean.Data(), clean.Size()) forType:NSPasteboardTypeString];
            }
            else
            {
                NSString* type = PasteboardTypeOf(ClipboardFormat::Custom, offer.custom_name);
                if (type == nil)
                {
                    return false;
                }
                [item setData:[NSData dataWithBytes:offer.data length:offer.size] forType:type];
            }
        }
        if (item != nil)
        {
            [items insertObject:[[NSDraggingItem alloc] initWithPasteboardWriter:item] atIndex:0];
        }
        // The image under the pointer, its hotspot there; without one, the system's documents icon.
        const CGFloat scale = native_->window.backingScaleFactor;
        NSImage* image = DragImageOf(source.image, scale);
        NSPoint hotspot = NSMakePoint(source.image.hotspot_x / scale, source.image.hotspot_y / scale);
        if (image == nil)
        {
            image = [NSImage imageNamed:NSImageNameMultipleDocuments];
            image.size = NSMakeSize(32, 32);
            hotspot = NSMakePoint(16, 16);
        }
        const NSPoint at = [view convertPoint:event.locationInWindow fromView:nil];
        const NSRect frame = NSMakeRect(at.x - hotspot.x, at.y - (image.size.height - hotspot.y), image.size.width,
                                        image.size.height);
        for (NSDraggingItem* dragging in items)
        {
            [dragging setDraggingFrame:frame contents:image];
        }
        native_->dnd.dragging = true;
        native_->dnd.cancelled = false;
        native_->dnd.outgoing_allowed = static_cast<u8>(source.allowed_actions & kAllDragActions);
        if (source.started != nullptr)
        {
            source.started(source.started_user);
        }
        NSDraggingSession* session = [view beginDraggingSessionWithItems:items event:event source:view];
        if (session == nil)
        {
            native_->dnd.dragging = false;
            return false;
        }
    }
    return true;
}

void CocoaWindow::CancelDrag()
{
    if (native_ != nullptr && native_->dnd.dragging)
    {
        native_->dnd.cancelled = true;  // the mask is none when AppKit next asks: a target not yet answered refuses
    }
}

bool CocoaWindow::Dragging() const
{
    return native_ != nullptr && native_->dnd.dragging;
}

void CocoaWindow::OnDragEnded(DragAction action)
{
    if (native_ == nullptr)
    {
        return;
    }
    native_->dnd.dragging = false;
    native_->dnd.cancelled = false;
    native_->view.lastMouseEvent = nil;
    // AppKit's drag loop takes the release of the button the drag started with: the window never gets its mouseUp:.
    // Every button no longer held is released here, before the DragEnded (seen by hand: the button stayed down).
    const NSUInteger pressed = [NSEvent pressedMouseButtons];
    const Array<u32, 5> bits = {{kMouseLeft, kMouseRight, kMouseMiddle, kMouseBack, kMouseForward}};
    for (usize i = 0; i < bits.Size(); ++i)
    {
        if ((input_->mouse_buttons & bits[i]) != 0 && (pressed & (NSUInteger{1} << i)) == 0)
        {
            OnPointerButton(bits[i], false, input_->mouse_x, input_->mouse_y, [NSEvent modifierFlags]);
        }
    }
    InputEvent event;
    event.type = InputEventType::DragEnded;
    event.drag_action = static_cast<u8>(action);
    QueueEvent(event);
}

}  // namespace engine_placeholder::os::cocoa
