// The cursor and the pointer lock of CocoaWindow (Phase 21 Day 297; platform/macos/cocoa_window.h, BACKEND.md "macos").
//
// Shapes are NSCursors, set from cursorUpdate: (the tracking area's) and whenever the state changes with the pointer
// inside. `Hidden` and the lock use [NSCursor hide], which AppKit counts: every hide is matched by exactly one unhide,
// through ApplyCursor, which compares what should be with what is (cursor_hidden_), and ReleaseCursor when the window
// closes. The lock warps the cursor to the view's centre and detaches it from the mouse
// (CGAssociateMouseAndMouseCursorPosition); neither call needs the Accessibility permission. The first motion after the
// warp is dropped: its delta includes the warp's distance (seen by hand: 219 points in one event). The cursor is warped
// back to the centre whenever the window moves or changes size while locked.
#include "platform/macos/cocoa_window.h"

#include "cstr/mem_ops.h"
#include "log/log.h"
#include "platform/macos/cocoa_window_native.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

using engine_placeholder::os::cocoa::CocoaWindow;

namespace
{

engine_placeholder::i32 g_cursor_hide_depth = 0;  // main thread only: hides not yet unhidden, every window together

}  // namespace

@implementation EngineMetalView (Cursor)
- (void)cursorUpdate:(NSEvent*)event
{
    (void)event;
    if (self.owner != nullptr)
    {
        self.owner->ApplyCursor();  // not super's, which would set the arrow
    }
}
@end

namespace engine_placeholder::os::cocoa
{

NSImage* ImageOfPixels(const byte* rgba, u32 width, u32 height, CGFloat scale)
{
    // NSBitmapImageRep's default format is RGBA with premultiplied alpha last, the layout of CursorImage and DragImage.
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:width * 4
                                                                  bitsPerPixel:32];
    if (rep == nil || rep.bitmapData == nullptr)
    {
        return nil;
    }
    MemCopy(rep.bitmapData, rgba, static_cast<usize>(width) * height * 4);
    rep.size = NSMakeSize(width / scale, height / scale);  // points: shown width / scale wide
    NSImage* picture = [[NSImage alloc] initWithSize:rep.size];
    [picture addRepresentation:rep];
    return picture;
}

i32 CursorHideDepth()
{
    return g_cursor_hide_depth;
}

void* SystemCursorFor(CursorShape shape)
{
    NSCursor* cursor = [NSCursor arrowCursor];
    switch (shape)
    {
    case CursorShape::IBeam: cursor = [NSCursor IBeamCursor]; break;
    case CursorShape::Hand: cursor = [NSCursor pointingHandCursor]; break;
    case CursorShape::ResizeHorizontal: cursor = [NSCursor resizeLeftRightCursor]; break;
    case CursorShape::ResizeVertical: cursor = [NSCursor resizeUpDownCursor]; break;
    case CursorShape::ResizeDiagonalNWSE:
        if (@available(macOS 15.0, *))
        {
            cursor = [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                                inDirections:NSCursorFrameResizeDirectionsAll];
        }
        break;  // before macOS 15 there is none: the arrow
    case CursorShape::ResizeDiagonalNESW:
        if (@available(macOS 15.0, *))
        {
            cursor = [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                                inDirections:NSCursorFrameResizeDirectionsAll];
        }
        break;
    case CursorShape::ResizeAll:
        cursor = [NSCursor openHandCursor];
        break;  // macOS has no four-way arrow: the hand
                // that moves things
    case CursorShape::NotAllowed: cursor = [NSCursor operationNotAllowedCursor]; break;
    case CursorShape::Arrow:
    case CursorShape::Hidden:
    case CursorShape::Count: break;
    }
    return (__bridge void*)cursor;
}

void CocoaWindow::ApplyCursor()
{
    if (native_ == nullptr)
    {
        return;
    }
    const bool inside = input_->pointer_inside;
    const bool hide = lock_.Active() ||
                      (inside && native_->image_cursor == nil && cursor_shape_ == CursorShape::Hidden);
    if (hide != cursor_hidden_)
    {
        if (hide)
        {
            [NSCursor hide];
            ++g_cursor_hide_depth;
        }
        else
        {
            [NSCursor unhide];
            --g_cursor_hide_depth;
        }
        cursor_hidden_ = hide;
    }
    if (!hide && inside)
    {
        NSCursor* cursor = native_->image_cursor != nil ? native_->image_cursor
                                                        : (__bridge NSCursor*)SystemCursorFor(cursor_shape_);
        [cursor set];
    }
}

void CocoaWindow::ReleaseCursor()
{
    if (locked_)
    {
        CGAssociateMouseAndMouseCursorPosition(true);
        locked_ = false;
    }
    lock_.Cleared();
    if (cursor_hidden_)
    {
        [NSCursor unhide];
        --g_cursor_hide_depth;
        cursor_hidden_ = false;
    }
    if (native_ != nullptr)
    {
        native_->image_cursor = nil;
    }
    image_hash_ = 0;
}

bool CocoaWindow::SetCursor(CursorShape shape)
{
    if (native_ == nullptr || static_cast<u32>(shape) >= static_cast<u32>(CursorShape::Count))
    {
        return false;
    }
    cursor_shape_ = shape;
    ApplyCursor();  // with the pointer outside, cursorUpdate: applies it when it comes in
    return true;
}

bool CocoaWindow::SetCursorImage(const CursorImage& image)
{
    if (native_ == nullptr || ValidateCursorImage(image) != CursorImageError::Ok)
    {
        return false;
    }
    const u64 hash = HashCursorImage(image);
    if (native_->image_cursor != nil && hash == image_hash_)
    {
        return true;  // the image showing: not made again
    }
    @autoreleasepool
    {
        const CGFloat scale = static_cast<CGFloat>(image.scale);
        NSImage* picture = ImageOfPixels(image.rgba, image.width, image.height, scale);
        if (picture == nil)
        {
            Log(LogPlatform, LogVerbosity::Error, "Window: the cursor image's bitmap could not be made");
            return false;
        }
        // The hotspot is from the image's top left, in points.
        native_->image_cursor = [[NSCursor alloc]
            initWithImage:picture
                  hotSpot:NSMakePoint(image.hotspot_x / scale, image.hotspot_y / scale)];
    }
    image_hash_ = hash;
    ApplyCursor();
    return true;
}

void CocoaWindow::ClearCursorImage()
{
    if (native_ == nullptr || native_->image_cursor == nil)
    {
        return;
    }
    native_->image_cursor = nil;
    image_hash_ = 0;
    ApplyCursor();
}

bool CocoaWindow::SetPointerLock(bool locked)
{
    if (native_ == nullptr)
    {
        return false;
    }
    ApplyLock(lock_.SetWanted(locked));
    return true;
}

void CocoaWindow::ApplyLock(PointerLockState::Action action)
{
    if (native_ == nullptr)
    {
        return;
    }
    if (action == PointerLockState::Action::Acquire)
    {
        WarpToCentre();
        if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess)
        {
            Log(LogPlatform, LogVerbosity::Error, "Window: the mouse could not be detached from the cursor (lock)");
            lock_.Failed();
            return;
        }
        locked_ = true;
        lock_.Granted();
        ApplyCursor();  // hidden while locked
    }
    else if (action == PointerLockState::Action::Release)
    {
        if (locked_)
        {
            CGAssociateMouseAndMouseCursorPosition(true);
            locked_ = false;
        }
        skip_motion_ = false;  // a warp's jump not yet seen goes with the lock: the next lock decides afresh
        ApplyCursor();
    }
}

void CocoaWindow::WarpToCentre()
{
    // The view's centre in global display coordinates (Quartz: from the top left of the main display).
    const NSRect in_window = [native_->view convertRect:native_->view.bounds toView:nil];
    const NSRect on_screen = [native_->window convertRectToScreen:in_window];
    const CGFloat main_height = NSScreen.screens.count > 0 ? NSScreen.screens[0].frame.size.height : 0;
    const CGPoint centre = CGPointMake(NSMidX(on_screen), main_height - NSMidY(on_screen));
    // The window server counts the warp into the next event's delta (seen by hand: about 200 points in one event):
    // that motion is dropped, but only when the cursor really jumped (a focus regained with the cursor still parked
    // there does not lose the user's first move).
    const NSPoint now = [NSEvent mouseLocation];  // from the bottom left
    const CGFloat jump_x = now.x - NSMidX(on_screen);
    const CGFloat jump_y = now.y - NSMidY(on_screen);
    if (jump_x * jump_x + jump_y * jump_y > 1.0)
    {
        skip_motion_ = true;
    }
    CGWarpMouseCursorPosition(centre);
}

void CocoaWindow::OnRelativeMotion(f64 dx, f64 dy, u64 flags)
{
    if (dx == 0.0 && dy == 0.0)
    {
        return;
    }
    if (skip_motion_)
    {
        skip_motion_ = false;  // the window server counts the warp to the centre into the first delta after it
        return;
    }
    InputEvent event;
    event.type = InputEventType::RelativeMotion;
    event.x = dx;
    event.y = dy;
    event.modifiers = ModifiersFromCocoa(flags);
    QueueEvent(event);
}

}  // namespace engine_placeholder::os::cocoa
