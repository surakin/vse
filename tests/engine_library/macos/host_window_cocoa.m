/* Phase 24 Day 370: host_window.h on macOS (AppKit): an NSWindow whose plain content NSView the engine draws into with
   Metal (the engine adds its CAMetalLayer as a sublayer, Day 355). Every call is on the main thread, as a Cocoa surface
   requires; the host's AppKit work is inside autorelease pools of its own, the engine's calls are not (Day 355). */
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <stdlib.h>

#include "host_window.h"

struct host_window
{
    void* window; /* the NSWindow, retained (+1) */
    uint32_t width;
    uint32_t height;
    float scale;
};

/* Runs the run loop for a moment, so the window server shows and lays out what was just ordered front. */
static void Spin(double seconds)
{
    @autoreleasepool
    {
        NSDate* until = [NSDate dateWithTimeIntervalSinceNow:seconds];
        for (;;)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:until
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil)
            {
                break;
            }
            [NSApp sendEvent:event];
        }
    }
}

int host_windows_begin(const char** why)
{
    @autoreleasepool
    {
        NSDictionary* session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
        NSNumber* locked = session[@"CGSSessionScreenIsLocked"];
        if (session == nil)
        {
            *why = "not in a window-server session (ssh, a daemon)";
            return 0;
        }
        if ([locked isKindOfClass:[NSNumber class]] && locked.boolValue)
        {
            *why = "the console is locked (unlock the screen)";
            return 0;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
    }
    return 1;
}

void host_windows_end(void)
{
    Spin(0.02); /* what closing the windows queued */
}

uint32_t host_window_surface_cap(void)
{
    return ENGINE_CAP_SURFACE_COCOA;
}

host_window* host_window_open(uint32_t width, uint32_t height)
{
    host_window* w = (host_window*)calloc(1, sizeof(host_window));
    if (w == NULL)
    {
        return NULL;
    }
    @autoreleasepool
    {
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(60, 60, width, height)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        window.title = @"engine scene host";
        /* the window's own scale (its screen's, which on a Mac with displays of two densities need not be the main
           one's): the content is width x height pixels, so width / scale points */
        const CGFloat factor = window.backingScaleFactor > 0.0 ? window.backingScaleFactor : 1.0;
        const NSSize points = NSMakeSize(width / factor, height / factor);
        [window setContentSize:points];
        window.contentView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, points.width, points.height)];
        [window setAcceptsMouseMovedEvents:YES];
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        w->window = (__bridge_retained void*)window;
        w->width = width;
        w->height = height;
        w->scale = (float)factor;
    }
    {
        /* until the window server shows it (two seconds at most), not a fixed wait */
        int tries;
        for (tries = 0; tries < 200; ++tries)
        {
            int visible;
            Spin(0.01);
            @autoreleasepool
            {
                NSWindow* window = (__bridge NSWindow*)w->window;
                visible = window.isVisible && (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
            }
            if (visible)
            {
                break;
            }
        }
    }
    return w;
}

void host_window_close(host_window* window)
{
    if (window == NULL)
    {
        return;
    }
    @autoreleasepool
    {
        NSWindow* w = (__bridge_transfer NSWindow*)window->window;
        [w close];
        w = nil;
    }
    free(window);
}

void host_window_describe(const host_window* window, engine_surface_info* info)
{
    if (window == NULL)
    {
        info->kind = 0;
        return;
    }
    @autoreleasepool
    {
        NSWindow* w = (__bridge NSWindow*)window->window;
        info->kind = ENGINE_SURFACE_COCOA;
        info->width = window->width;
        info->height = window->height;
        info->scale = window->scale;
        info->display = (__bridge void*)w.contentView;
        info->surface = NULL; /* the engine adds its CAMetalLayer to the plain view */
        info->window_id = 0;
    }
}

int host_window_poll(host_window* window, host_window_event* out)
{
    @autoreleasepool
    {
        NSWindow* w = (__bridge NSWindow*)window->window;
        for (;;)
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil)
            {
                return 0;
            }
            if (event.window != w || (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp &&
                                      event.type != NSEventTypeMouseMoved))
            {
                [NSApp sendEvent:event];
                continue;
            }
            if (event.type == NSEventTypeMouseMoved)
            {
                out->type = HOST_EVENT_MOTION;
                out->x = (int)([w.contentView convertPoint:event.locationInWindow fromView:nil].x * window->scale);
                return 1;
            }
            NSString* chars = event.charactersIgnoringModifiers;
            const unichar c = chars.length > 0 ? [chars characterAtIndex:0] : 0;
            const int plain = (event.modifierFlags & NSEventModifierFlagCommand) == 0;
            const int key = !plain     ? 0
                            : c == 'w' ? HOST_KEY_W
                            : c == 's' ? HOST_KEY_S
                            : c == 'a' ? HOST_KEY_A
                            : c == 'd' ? HOST_KEY_D
                            : c == 27  ? HOST_KEY_ESCAPE
                                       : 0;
            if (key == 0)
            {
                [NSApp sendEvent:event]; /* Command-Q and the other key equivalents are AppKit's */
                continue;
            }
            out->type = HOST_EVENT_KEY;
            out->down = event.type == NSEventTypeKeyDown;
            out->key = key;
            return 1;
        }
    }
}

int host_window_post_key(host_window* window, int key, int down)
{
    @autoreleasepool
    {
        NSWindow* w = (__bridge NSWindow*)window->window;
        NSString* chars = key == HOST_KEY_W   ? @"w"
                          : key == HOST_KEY_S ? @"s"
                          : key == HOST_KEY_A ? @"a"
                          : key == HOST_KEY_D ? @"d"
                                              : @"\x1b";
        NSEvent* event = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                          location:NSZeroPoint
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:w.windowNumber
                                           context:nil
                                        characters:chars
                       charactersIgnoringModifiers:chars
                                         isARepeat:NO
                                           keyCode:0];
        if (event == nil)
        {
            return 0;
        }
        [NSApp postEvent:event atStart:NO];
        return 1;
    }
}
