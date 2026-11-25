/* Phase 23 Day 355: an Objective-C program that owns a Cocoa window embeds the engine and has it render with Metal into
   the window's NSView, the way a macOS application with its own AppKit code would. The C API is reached through the
   function table: linked statically (engine_c), or with HOST_COCOA_DYNAMIC from libengine.dylib through
   engine_loader.h.

   It checks the capability bits and the arguments (no view, an object that is not a view, a layer that is not a
   CAMetalLayer, a surface made off the main thread), a frame of a known color read back and compared with the expected
   pixels (and an sRGB-encoded grey), the engine's own sublayer in a plain view and its removal, a host's own
   CAMetalLayer, the host's resize and a change of scale (the image follows), a hidden window (size 0), a display link
   that drives `frame` from the main thread, the window closed under the engine and the recovery on a new window through
   surface_rebind, the host's NSEvents converted and forwarded with surface_input, the host's cursor and clipboard
   callbacks (NSCursor and a private pasteboard; called by the host here: the engine's UI calls them in
   surface_input_test.cpp), a frame and a destroy off the main thread, and destroy refused while the surface exists.

   The engine's calls are made outside any autorelease pool of the host's (main has none around them; the host's own
   AppKit work is inside its pools): the test runs with OBJC_DEBUG_MISSING_POOLS=YES and run_cocoa_host.cmake fails on
   a missing pool on the main thread, so every entry that touches AppKit or Metal must drain its own pool. The window
   needs an unlocked console (the session check skips otherwise). */
#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "engine/engine_c.h"
#if HOST_COCOA_DYNAMIC
#include "engine/engine_loader.h"
#endif

static int g_failures = 0;
static const engine_api* g_api = NULL;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_cocoa: line %d: CHECK failed: %s\n", __LINE__, #condition);                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

/* Prints the text of the calling thread's last failure. */
static void PrintLastError(void)
{
    engine_buffer why;
    memset(&why, 0, sizeof(why));
    if (g_api->last_error(&why) == ENGINE_OK)
    {
        printf("host_cocoa:   last error: %.*s\n", (int)why.size, (const char*)why.data);
        engine_buffer_release(&why);
    }
}

static void ReportStatus(int line, const char* call, engine_status got, int expected)
{
    const engine_string name = g_api->status_name(got);
    printf("host_cocoa: line %d: %s returned %d (%.*s), expected %d\n", line, call, (int)got, (int)name.length,
           name.data, expected);
    PrintLastError();
    ++g_failures;
}

#define CHECK_STATUS(call, expected)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        const engine_status got_ = (call);                                                                             \
        if (got_ != (expected))                                                                                        \
        {                                                                                                              \
            ReportStatus(__LINE__, #call, got_, (int)(expected));                                                      \
        }                                                                                                              \
    } while (0)

/* ---- the host's side: what an AppKit application keeps for the engine */

typedef struct Host
{
    int cursor_calls;
    uint32_t cursor;
    char clipboard_text[256];
    size_t clipboard_length;
    int focus_requests;
} Host;

static Host g_host;
static NSPasteboard* g_pasteboard; /* a pasteboard of the test's own: the user's clipboard is left alone */

static int HostSetCursor(void* user, uint32_t shape)
{
    Host* host = (Host*)user;
    @autoreleasepool
    {
        NSCursor* cursor = NSCursor.arrowCursor;
        if (shape == ENGINE_CURSOR_I_BEAM)
        {
            cursor = NSCursor.IBeamCursor;
        }
        else if (shape == ENGINE_CURSOR_HAND)
        {
            cursor = NSCursor.pointingHandCursor;
        }
        else if (shape == ENGINE_CURSOR_NOT_ALLOWED)
        {
            cursor = NSCursor.operationNotAllowedCursor;
        }
        [cursor set];
    }
    host->cursor = shape;
    ++host->cursor_calls;
    return 1;
}

static int HostClipboardSet(void* user, engine_string text)
{
    (void)user;
    @autoreleasepool
    {
        NSString* string = [[NSString alloc] initWithBytes:text.data length:text.length encoding:NSUTF8StringEncoding];
        if (string == nil)
        {
            return 0;
        }
        [g_pasteboard clearContents];
        return [g_pasteboard setString:string forType:NSPasteboardTypeString] ? 1 : 0;
    }
}

static int HostClipboardGet(void* user, engine_string* text)
{
    Host* host = (Host*)user;
    @autoreleasepool
    {
        NSString* string = [g_pasteboard stringForType:NSPasteboardTypeString];
        if (string == nil)
        {
            return 0;
        }
        const char* utf8 = string.UTF8String;
        const size_t length = strlen(utf8);
        if (length >= sizeof(host->clipboard_text))
        {
            return 0;
        }
        memcpy(host->clipboard_text, utf8, length);
        host->clipboard_length = length;
    }
    text->data = host->clipboard_text; /* valid until the next callback (engine_c.h) */
    text->length = host->clipboard_length;
    return 1;
}

static void HostFocus(void* user, int focused)
{
    (void)focused;
    ++((Host*)user)->focus_requests;
}

static engine_surface_callbacks g_callbacks;

/* A window of the host's own. A plain view (no layer of its own) is where the engine adds its sublayer; `metal_view`
   makes the view's own layer a CAMetalLayer, the way an application that draws with Metal sets it up. */
static NSWindow* MakeWindow(int width_points, int height_points, int metal_view)
{
    NSWindow* window = nil;
    @autoreleasepool
    {
        window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, width_points, height_points)
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        window.releasedWhenClosed = NO;
        window.title = @"engine host_cocoa";
        NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width_points, height_points)];
        if (metal_view)
        {
            view.wantsLayer = YES;
            view.layer = [CAMetalLayer layer];
        }
        window.contentView = view;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
    return window;
}

/* Runs the host's run loop for a moment, as an application's own loop does between frames. */
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

/* The size of the window's content in pixels, and its scale. */
static void PixelSize(NSWindow* window, uint32_t* width, uint32_t* height, float* scale)
{
    @autoreleasepool
    {
        const NSRect points = window.contentView.bounds;
        const CGFloat factor = window.backingScaleFactor;
        *width = (uint32_t)(points.size.width * factor);
        *height = (uint32_t)(points.size.height * factor);
        *scale = (float)factor;
    }
}

static engine_surface_info InfoOf(NSWindow* window, void* layer)
{
    engine_surface_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_COCOA;
    PixelSize(window, &info.width, &info.height, &info.scale);
    info.display = (__bridge void*)window.contentView;
    info.surface = layer;
    info.callbacks = &g_callbacks;
    return info;
}

static void Frame(engine_handle engine)
{
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 1.0 / 60.0;
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
}

static engine_surface_state StateOf(engine_handle surface)
{
    engine_surface_state state;
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    return state;
}

/* One frame, then the pixels it drew (the caller releases them). */
static engine_status FrameAndRead(engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    engine_handle ticket = {0};
    memset(pixels, 0, sizeof(*pixels));
    pixels->struct_size = sizeof(*pixels);
    const engine_status asked = g_api->surface_read_pixels(surface, &ticket);
    if (asked != ENGINE_OK)
    {
        return asked;
    }
    engine_status status = ENGINE_ERROR_NOT_READY;
    for (int attempt = 0; attempt < 30 && status == ENGINE_ERROR_NOT_READY; ++attempt)
    { /* a drawable may not be free at once after a resize: the ticket waits for the frame that draws */
        Frame(engine);
        status = g_api->pixels_finish(ticket, pixels);
        if (status == ENGINE_ERROR_NOT_READY)
        {
            Spin(0.02);
        }
    }
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    return status;
}

static int AllPixels(const engine_pixels* pixels, uint8_t r, uint8_t g, uint8_t b)
{
    const uint8_t* data = (const uint8_t*)pixels->data.data;
    if (data == NULL || pixels->width == 0 || pixels->data.size < (size_t)pixels->stride * pixels->height)
    {
        return 0;
    }
    for (uint32_t y = 0; y < pixels->height; ++y)
    {
        for (uint32_t x = 0; x < pixels->width; ++x)
        {
            const uint8_t* p = data + (size_t)y * pixels->stride + (size_t)x * 4;
            if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255)
            {
                printf("host_cocoa: pixel (%u, %u) is %u %u %u %u, expected %u %u %u 255\n", x, y, p[0], p[1], p[2],
                       p[3], r, g, b);
                return 0;
            }
        }
    }
    return 1;
}

/* Reads one frame and checks its size and color. */
static void ExpectFrame(int line, engine_handle engine, engine_handle surface, uint32_t width, uint32_t height,
                        uint8_t r, uint8_t g, uint8_t b)
{
    engine_pixels pixels;
    const engine_status status = FrameAndRead(engine, surface, &pixels);
    if (status != ENGINE_OK)
    {
        ReportStatus(line, "FrameAndRead", status, ENGINE_OK);
        return;
    }
    if (pixels.width != width || pixels.height != height)
    {
        printf("host_cocoa: line %d: the image is %ux%u, expected %ux%u\n", line, pixels.width, pixels.height, width,
               height);
        ++g_failures;
    }
    else if (!AllPixels(&pixels, r, g, b))
    {
        printf("host_cocoa: line %d: the frame is not the expected color\n", line);
        ++g_failures;
    }
    engine_buffer_release(&pixels.data);
}

/* ---- input: the host converts its NSEvents into engine_input_event, as an application's view would */

static engine_status Forward(engine_handle surface, NSEvent* event, NSView* view)
{
    engine_input_event e;
    memset(&e, 0, sizeof(e));
    e.struct_size = sizeof(e);
    @autoreleasepool
    {
        const CGFloat factor = view.window.backingScaleFactor;
        const NSPoint in_view = [view convertPoint:event.locationInWindow fromView:nil];
        e.x = in_view.x * factor;
        e.y = (view.bounds.size.height - in_view.y) *
              factor; /* AppKit counts from the bottom, the engine from the top */
        switch (event.type)
        {
        case NSEventTypeMouseMoved: e.type = ENGINE_INPUT_POINTER_MOVE; break;
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
            e.type = ENGINE_INPUT_POINTER_BUTTON;
            e.button = ENGINE_BUTTON_LEFT;
            e.down = event.type == NSEventTypeLeftMouseDown;
            break;
        case NSEventTypeScrollWheel:
            e.type = ENGINE_INPUT_SCROLL;
            e.x = event.scrollingDeltaX;
            e.y = event.scrollingDeltaY;
            break;
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
            e.type = ENGINE_INPUT_KEY;
            e.down = event.type == NSEventTypeKeyDown;
            e.repeat = e.down && event.ARepeat;
            e.key = ENGINE_KEY_A; /* the test sends only "a"; a real host maps every key once */
            if ((event.modifierFlags & NSEventModifierFlagShift) != 0)
            {
                e.modifiers |= ENGINE_MOD_SHIFT;
            }
            break;
        default: return ENGINE_ERROR_UNSUPPORTED;
        }
    }
    return g_api->surface_input(surface, &e);
}

static NSEvent* MouseEvent(NSEventType type, NSWindow* window, double x_points, double y_points_from_bottom)
{
    return [NSEvent mouseEventWithType:type
                              location:NSMakePoint(x_points, y_points_from_bottom)
                         modifierFlags:0
                             timestamp:0
                          windowNumber:window.windowNumber
                               context:nil
                           eventNumber:0
                            clickCount:1
                              pressure:type == NSEventTypeLeftMouseDown ? 1.0f : 0.0f];
}

static NSEvent* KeyEvent(NSEventType type, NSWindow* window)
{
    return [NSEvent keyEventWithType:type
                            location:NSZeroPoint
                       modifierFlags:0
                           timestamp:0
                        windowNumber:window.windowNumber
                             context:nil
                          characters:@"a"
         charactersIgnoringModifiers:@"a"
                           isARepeat:NO
                             keyCode:0];
}

/* ---- the display link: the host's frame clock. Its callback runs on a thread of CoreVideo's; the frame is run on
   the main thread, where a Cocoa surface is drawn (engine_c.h, ENGINE_SURFACE_COCOA). */

typedef struct Clock
{
    engine_handle engine;
    int ticks;    /* display-link callbacks */
    int frames;   /* frames run on the main thread */
    int off_main; /* frames that were not on the main thread (must stay 0) */
} Clock;

static Clock g_clock;

static CVReturn OnDisplayLink(CVDisplayLinkRef link, const CVTimeStamp* now, const CVTimeStamp* output,
                              CVOptionFlags flags_in, CVOptionFlags* flags_out, void* user)
{
    (void)link;
    (void)now;
    (void)output;
    (void)flags_in;
    (void)flags_out;
    Clock* clock = (Clock*)user;
    __atomic_add_fetch(&clock->ticks, 1, __ATOMIC_RELAXED);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!NSThread.isMainThread)
      {
          ++clock->off_main;
      }
      Frame(clock->engine);
      ++clock->frames;
    });
    return kCVReturnSuccess;
}

/* Runs frames from the display link for up to `seconds`; returns the frames run (0 when the system gave no link). */
static int RunDisplayLink(engine_handle engine, double seconds)
{
    CVDisplayLinkRef link = NULL;
    g_clock.engine = engine;
    g_clock.frames = 0;
    if (CVDisplayLinkCreateWithActiveCGDisplays(&link) != kCVReturnSuccess || link == NULL)
    {
        printf("host_cocoa: no display link (no active display)\n");
        return 0;
    }
    CVDisplayLinkSetOutputCallback(link, &OnDisplayLink, &g_clock);
    CVDisplayLinkStart(link);
    @autoreleasepool
    {
        NSDate* until = [NSDate dateWithTimeIntervalSinceNow:seconds];
        while (g_clock.frames < 30 && [until timeIntervalSinceNow] > 0)
        {
            [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
    CVDisplayLinkStop(link);
    CVDisplayLinkRelease(link);
    Spin(0.05); /* the blocks already queued run before the link's clock goes */
    return g_clock.frames;
}

/* ---- the checks */

typedef struct OffMain
{
    engine_handle engine;
    engine_surface_info info;
    engine_status status;
} OffMain;

static void* CreateOffMain(void* user)
{
    OffMain* job = (OffMain*)user;
    engine_handle surface = {0};
    job->status = g_api->surface_create(job->engine, &job->info, &surface);
    if (surface.value != 0)
    {
        g_api->surface_destroy(surface);
    }
    g_api->thread_detach();
    return NULL;
}

static void CheckArguments(engine_handle engine, NSWindow* window)
{
    engine_handle surface = {0};
    engine_surface_info info = InfoOf(window, NULL);
    info.display = NULL;
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    @autoreleasepool
    {
        NSObject* not_a_view = [[NSObject alloc] init];
        info = InfoOf(window, NULL);
        info.display = (__bridge void*)not_a_view;
        CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
        CALayer* not_metal = [CALayer layer];
        info = InfoOf(window, (__bridge void*)not_metal);
        CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    }
    CHECK(surface.value == 0);
    /* A Cocoa surface is made on the main thread. */
    OffMain job;
    job.engine = engine;
    job.info = InfoOf(window, NULL);
    job.status = ENGINE_OK;
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, &CreateOffMain, &job) == 0);
    pthread_join(thread, NULL);
    CHECK_STATUS(job.status, ENGINE_ERROR_INVALID_ARGUMENT);
    /* The other kinds are not this platform's. */
    info = InfoOf(window, NULL);
    info.kind = ENGINE_SURFACE_X11;
    info.window_id = 1;
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_ERROR_UNSUPPORTED);
}

/* Review of Day 355: a frame and a surface_destroy on another thread than the main one. */
typedef struct OffMainCall
{
    engine_handle engine;
    engine_handle surface;
    engine_status frame_status;
    engine_status destroy_status;
} OffMainCall;

static void* FrameAndDestroyOffMain(void* user)
{
    OffMainCall* call = (OffMainCall*)user;
    engine_frame_info frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    call->frame_status = g_api->frame(call->engine, &frame, NULL);
    call->destroy_status = g_api->surface_destroy(call->surface);
    g_api->thread_detach();
    return NULL;
}

static int WantsLayer(NSWindow* window)
{
    @autoreleasepool
    {
        return window.contentView.wantsLayer ? 1 : 0;
    }
}

static size_t SublayerCount(NSWindow* window)
{
    @autoreleasepool
    {
        return window.contentView.layer.sublayers.count;
    }
}

static int SessionUsable(void)
{
    @autoreleasepool
    {
        NSDictionary* session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
        if (session == nil)
        {
            return 0; /* not in a window-server session (ssh, a daemon) */
        }
        NSNumber* locked = session[@"CGSSessionScreenIsLocked"];
        NSNumber* on_console = session[(__bridge NSString*)kCGSessionOnConsoleKey];
        if ([locked isKindOfClass:[NSNumber class]] && locked.boolValue)
        {
            return 0;
        }
        return ![on_console isKindOfClass:[NSNumber class]] || on_console.boolValue;
    }
}

int main(void)
{
#if HOST_COCOA_DYNAMIC
    engine_loader loader;
    if (engine_load(ENGINE_LIB_PATH, ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, &loader) != ENGINE_OK)
    {
        printf("host_cocoa: %s\n", loader.message);
        return 2;
    }
    g_api = loader.api;
#else
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_cocoa: no function table\n");
        return 2;
    }
#endif
    printf("host_cocoa: main thread %p\n",
           (void*)pthread_self()); /* what run_cocoa_host.cmake counts the missing pools of */
    fflush(stdout);
    if (!SessionUsable())
    {
        printf("host_cocoa: skipped: the console is locked or there is no window-server session (unlock the screen)\n");
        return 0;
    }
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        g_pasteboard = [NSPasteboard pasteboardWithUniqueName];
    }
    memset(&g_callbacks, 0, sizeof(g_callbacks));
    g_callbacks.struct_size = sizeof(g_callbacks);
    g_callbacks.user = &g_host;
    g_callbacks.set_cursor = &HostSetCursor;
    g_callbacks.clipboard_set = &HostClipboardSet;
    g_callbacks.clipboard_get = &HostClipboardGet;
    g_callbacks.request_focus = &HostFocus;

    engine_create_info create;
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    engine_handle engine = {0};
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    engine_info about;
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    CHECK_STATUS(g_api->get_info(engine, &about), ENGINE_OK);
    CHECK((about.caps & ENGINE_CAP_SURFACE_COCOA) != 0);
    CHECK((about.caps & ENGINE_CAP_READBACK) != 0);
    CHECK((about.caps & (ENGINE_CAP_SURFACE_X11 | ENGINE_CAP_SURFACE_WAYLAND)) == 0);

    NSWindow* window = MakeWindow(160, 100, 0);
    Spin(0.2);
    CheckArguments(engine, window);

    /* A plain view: the engine adds a CAMetalLayer of its own on top of the view's content. */
    const size_t sublayers_before = SublayerCount(window);
    const int wants_layer_before = WantsLayer(window);
    engine_surface_info info = InfoOf(window, NULL);
    engine_handle surface = {0};
    const engine_status created = g_api->surface_create(engine, &info, &surface);
    if (created == ENGINE_ERROR_UNSUPPORTED)
    {
        /* No Metal 3 GPU of the Mac2 family (a virtual machine, an older Mac): nothing to present with. */
        printf("host_cocoa: skipped: no Metal device can present to this view\n");
        return 0;
    }
    CHECK_STATUS(created, ENGINE_OK);
    if (surface.value == 0)
    {
        printf("host_cocoa: no surface\n");
        return 1;
    }
    CHECK(SublayerCount(window) == sublayers_before + 1);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE); /* the surface is the engine's object */
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float grey[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, info.width, info.height, 255, 0, 0);
    CHECK_STATUS(g_api->surface_set_background(surface, grey), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, info.width, info.height, 188, 188, 188); /* 0.5 in sRGB */
    CHECK(StateOf(surface).visible == 1);

    /* The host resizes its window and tells the engine: the image follows. */
    @autoreleasepool
    {
        [window setContentSize:NSMakeSize(200, 120)];
    }
    Spin(0.1);
    uint32_t width = 0;
    uint32_t height = 0;
    float scale = 1.0f;
    PixelSize(window, &width, &height, &scale);
    CHECK_STATUS(g_api->surface_resized(surface, width, height, scale), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, width, height, 255, 0, 0);
    /* A change of scale (the window moved to a display of another density): twice the pixels for the same points. */
    CHECK_STATUS(g_api->surface_resized(surface, width * 2, height * 2, scale * 2.0f), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, width * 2, height * 2, 255, 0, 0);
    CHECK_STATUS(g_api->surface_resized(surface, width, height, scale), ENGINE_OK);

    /* Hidden (size 0): nothing is drawn, frames are counted as skipped; the next real size resumes. */
    CHECK_STATUS(g_api->surface_resized(surface, 0, 0, scale), ENGINE_OK);
    const uint64_t skipped_before = StateOf(surface).frames_skipped;
    Frame(engine);
    Frame(engine);
    CHECK(StateOf(surface).frames_skipped >= skipped_before + 2);
    CHECK(StateOf(surface).visible == 0);
    CHECK_STATUS(g_api->surface_resized(surface, width, height, scale), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, width, height, 255, 0, 0);

    /* The display link drives the frames, on the main thread. */
    const uint64_t presented_before = StateOf(surface).frames_presented;
    const int frames = RunDisplayLink(engine, 5.0);
    if (frames > 0)
    {
        CHECK(frames >= 10);
        CHECK(g_clock.off_main == 0);
        CHECK(StateOf(surface).frames_presented >= presented_before + 10);
    }
    else
    {
        printf("host_cocoa: the display-link part was not run (no active display)\n");
    }

    /* Input: the host converts its events and forwards them; the requests come back through the callbacks. */
    @autoreleasepool
    {
        NSView* view = window.contentView;
        CHECK_STATUS(Forward(surface, MouseEvent(NSEventTypeMouseMoved, window, 20, 30), view), ENGINE_OK);
        CHECK_STATUS(Forward(surface, MouseEvent(NSEventTypeLeftMouseDown, window, 20, 30), view), ENGINE_OK);
        CHECK_STATUS(Forward(surface, MouseEvent(NSEventTypeLeftMouseUp, window, 20, 30), view), ENGINE_OK);
        CHECK_STATUS(Forward(surface, KeyEvent(NSEventTypeKeyDown, window), view), ENGINE_OK);
        CHECK_STATUS(Forward(surface, KeyEvent(NSEventTypeKeyUp, window), view), ENGINE_OK);
    }
    Frame(engine);
    CHECK(StateOf(surface).input_dropped == 0);
    /* The host's callbacks themselves (the engine calls them from its UI: surface_input_test.cpp drives that on a Cocoa
     * window). */
    CHECK(HostSetCursor(&g_host, ENGINE_CURSOR_I_BEAM) == 1 && g_host.cursor == ENGINE_CURSOR_I_BEAM);
    {
        const engine_string text = {"copied from the engine", sizeof("copied from the engine") - 1};
        engine_string back = {NULL, 0};
        CHECK(HostClipboardSet(&g_host, text) == 1);
        CHECK(HostClipboardGet(&g_host, &back) == 1 && back.length == text.length &&
              memcmp(back.data, text.data, text.length) == 0);
    }

    /* Rebind to a second window whose view has a CAMetalLayer of the host's: the engine's sublayer leaves the first. */
    NSWindow* second = MakeWindow(180, 110, 1);
    Spin(0.2);
    engine_surface_info second_info = InfoOf(second, NULL);
    CHECK_STATUS(g_api->surface_rebind(surface, &second_info), ENGINE_OK);
    CHECK(SublayerCount(window) == sublayers_before);
    CHECK(WantsLayer(window) == wants_layer_before); /* the engine gave the view back as it found it */
    CHECK(StateOf(surface).recoveries == 1);
    ExpectFrame(__LINE__, engine, surface, second_info.width, second_info.height, 255, 0, 0);
    /* A rebind to an object that is not a view changes nothing: the surface keeps drawing where it was. */
    @autoreleasepool
    {
        NSObject* not_a_view = [[NSObject alloc] init];
        engine_surface_info bad = InfoOf(second, NULL);
        bad.display = (__bridge void*)not_a_view;
        CHECK_STATUS(g_api->surface_rebind(surface, &bad), ENGINE_ERROR_INVALID_ARGUMENT);
    }
    ExpectFrame(__LINE__, engine, surface, second_info.width, second_info.height, 255, 0, 0);
    /* The host's own layer passed explicitly. */
    @autoreleasepool
    {
        engine_surface_info explicit_layer = InfoOf(second, (__bridge void*)second.contentView.layer);
        CHECK_STATUS(g_api->surface_rebind(surface, &explicit_layer), ENGINE_OK);
    }
    ExpectFrame(__LINE__, engine, surface, second_info.width, second_info.height, 255, 0, 0);

    /* The host closes the window under the engine: no crash; frames go on (the layer the engine holds takes them, or
       the surface reads as not visible), and a rebind to a new window recovers. */
    @autoreleasepool
    {
        [second close];
        second = nil;
    }
    Spin(0.1);
    Frame(engine);
    Frame(engine);
    NSWindow* third = MakeWindow(150, 90, 0);
    Spin(0.2);
    engine_surface_info third_info = InfoOf(third, NULL);
    CHECK_STATUS(g_api->surface_rebind(surface, &third_info), ENGINE_OK);
    ExpectFrame(__LINE__, engine, surface, third_info.width, third_info.height, 255, 0, 0);

    /* Off the main thread: a frame skips the Cocoa surface and fails its pending read-back; surface_destroy is refused.
     */
    {
        engine_handle waiting = {0};
        CHECK_STATUS(g_api->surface_read_pixels(surface, &waiting), ENGINE_OK);
        OffMainCall call;
        call.engine = engine;
        call.surface = surface;
        call.frame_status = ENGINE_ERROR_INVALID_ARGUMENT;
        call.destroy_status = ENGINE_OK;
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, &FrameAndDestroyOffMain, &call) == 0);
        pthread_join(thread, NULL);
        CHECK_STATUS(call.frame_status, ENGINE_OK);
        CHECK_STATUS(call.destroy_status, ENGINE_ERROR_INVALID_ARGUMENT);
        engine_pixels pixels;
        memset(&pixels, 0, sizeof(pixels));
        pixels.struct_size = sizeof(pixels);
        CHECK_STATUS(g_api->pixels_finish(waiting, &pixels), ENGINE_ERROR_INVALID_ARGUMENT);
        CHECK_STATUS(g_api->pixels_destroy(waiting), ENGINE_OK);
        ExpectFrame(__LINE__, engine, surface, third_info.width, third_info.height, 255, 0,
                    0); /* still alive and drawn */
    }

    /* A read-back pending when the surface goes is cancelled. */
    engine_handle ticket = {0};
    CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
    CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
    CHECK(SublayerCount(third) == 0); /* the engine's sublayer left the plain view */
    {
        engine_pixels pixels;
        memset(&pixels, 0, sizeof(pixels));
        pixels.struct_size = sizeof(pixels);
        CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_CANCELLED);
        CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    @autoreleasepool
    { /* the host's windows go inside a pool of its own: their deallocation autoreleases (the host's part, not the
         engine's) */
        [window close];
        [third close];
        window = nil;
        third = nil;
        g_pasteboard = nil;
    }
#if HOST_COCOA_DYNAMIC
    CHECK(engine_unload(&loader) == ENGINE_OK);
#endif
    if (g_failures != 0)
    {
        printf("host_cocoa: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_cocoa: ok\n");
    return 0;
}
