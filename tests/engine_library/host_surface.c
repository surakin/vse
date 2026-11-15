/* Phase 23 Day 352: a C program that owns an X11 window (Xlib) embeds the engine and has it render into that window,
   the way an application with its own toolkit would. It checks the capability bits, the arguments, a frame of a known
   color read back through the function table and compared with the expected pixels, the host's resize (the image
   follows the window), a hidden window (size 0: nothing is drawn, the read-back stays pending), the window destroyed
   under the engine (the surface is reported lost and read-backs fail), and the recovery on a new window through
   surface_rebind. The engine never touches the window itself: the host maps, resizes and destroys it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <X11/Xlib.h>

#include "engine_c/engine_c.h"

static int g_failures = 0;
static const engine_api* g_api = NULL;
static int g_x_errors = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_surface: line %d: CHECK failed: %s\n", __LINE__, #condition);                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void ReportStatus(int line, const char* call, engine_status got, int expected)
{
    const engine_string name = g_api->status_name(got);
    printf("host_surface: line %d: %s returned %d (%.*s), expected %d\n", line, call, (int)got, (int)name.length,
           name.data, expected);
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

/* The host destroys a window the engine still renders into: the X server answers the engine's requests with errors,
   which the host's own handler counts (the default handler would exit the program). */
static int CountXError(Display* display, XErrorEvent* event)
{
    (void)display;
    (void)event;
    ++g_x_errors;
    return 0;
}

static Window MakeWindow(Display* display, unsigned width, unsigned height)
{
    const int screen = DefaultScreen(display);
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);
    return window;
}

/* Waits until the server reports the window's new size (what a toolkit does on ConfigureNotify, before it tells the
 * engine). */
static void WaitForSize(Display* display, Window window, unsigned width, unsigned height)
{
    int tries;
    for (tries = 0; tries < 200; ++tries)
    {
        XWindowAttributes attributes;
        XSync(display, False);
        if (XGetWindowAttributes(display, window, &attributes) != 0 && (unsigned)attributes.width == width &&
            (unsigned)attributes.height == height)
        {
            return;
        }
        usleep(10000);
    }
}

static engine_surface_info InfoOf(Display* display, Window window, unsigned width, unsigned height)
{
    engine_surface_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_X11;
    info.width = width;
    info.height = height;
    info.scale = 1.0f;
    info.display = display;
    info.window_id = (uint64_t)window;
    return info;
}

/* One frame, then the read-back of what it drew. */
static engine_status FrameAndRead(engine_handle engine, engine_handle surface, engine_pixels* pixels)
{
    engine_handle ticket;
    engine_frame_info frame;
    engine_status status;
    memset(&ticket, 0, sizeof(ticket));
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    memset(pixels, 0, sizeof(*pixels));
    pixels->struct_size = sizeof(*pixels);
    status = g_api->surface_read_pixels(surface, &ticket);
    if (status != ENGINE_OK)
    {
        return status;
    }
    CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
    status = g_api->pixels_finish(ticket, pixels);
    CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
    return status;
}

/* Whether every pixel is within `tolerance` of (r, g, b, 255). */
static int AllPixels(const engine_pixels* pixels, int r, int g, int b, int tolerance)
{
    const unsigned char* data = (const unsigned char*)pixels->data.data;
    unsigned x;
    unsigned y;
    if (data == NULL || pixels->stride < pixels->width * 4 ||
        pixels->data.size < (size_t)pixels->stride * pixels->height)
    {
        return 0;
    }
    for (y = 0; y < pixels->height; ++y)
    {
        for (x = 0; x < pixels->width; ++x)
        {
            const unsigned char* p = data + (size_t)y * pixels->stride + (size_t)x * 4;
            if (abs(p[0] - r) > tolerance || abs(p[1] - g) > tolerance || abs(p[2] - b) > tolerance || p[3] != 255)
            {
                return 0;
            }
        }
    }
    return 1;
}

static engine_handle NewEngine(void)
{
    engine_create_info info;
    engine_handle engine;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&info, &engine), ENGINE_OK);
    return engine;
}

static void TestArguments(engine_handle engine, Display* display, Window window)
{
    engine_surface_info info = InfoOf(display, window, 200, 120);
    engine_surface_info bad;
    engine_handle surface;
    engine_handle zero;
    engine_surface_state state;
    memset(&surface, 0, sizeof(surface));
    memset(&zero, 0, sizeof(zero));
    CHECK_STATUS(g_api->surface_create(engine, NULL, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->surface_create(engine, &info, NULL), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.struct_size = 8;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_STRUCT_SIZE);
    bad = info;
    bad.flags = 1;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.kind = 99;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_UNSUPPORTED);
    bad = info;
    bad.display = NULL;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.window_id = 0;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.scale = -1.0f;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.width = 1u << 20;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(surface.value == 0);
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(zero, &state), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->surface_destroy(zero), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->surface_resized(zero, 1, 1, 1.0f), ENGINE_ERROR_INVALID_ARGUMENT);
}

static void TestRendering(engine_handle engine, Display* display, Window window)
{
    engine_surface_info info = InfoOf(display, window, 200, 120);
    engine_surface_state state;
    engine_handle surface;
    engine_handle world;
    engine_pixels pixels;
    float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float half[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    float bad[4] = {2.0f, 0.0f, 0.0f, 1.0f};
    memset(&surface, 0, sizeof(surface));
    memset(&world, 0, sizeof(world));
    CHECK_STATUS(g_api->surface_create(engine, &info, &surface), ENGINE_OK);
    CHECK(surface.value != 0);
    /* An engine with an outstanding surface refuses to be destroyed (checked at the end of main). */
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);
    CHECK_STATUS(g_api->surface_set_background(surface, bad), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK_STATUS(g_api->surface_set_background(surface, NULL), ENGINE_ERROR_INVALID_ARGUMENT);

    /* A world next to the surface: the frame still steps it. */
    CHECK_STATUS(g_api->world_create(engine, &world), ENGINE_OK);

    /* The image the engine drew is the host window's size, and its pixels are the clear color: the golden image. */
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 200 && pixels.height == 120 && pixels.stride == 200 * 4);
    CHECK(AllPixels(&pixels, 255, 0, 0, 0));
    engine_buffer_release(&pixels.data);

    /* A second take of the same ticket is NOT_FOUND; a ticket made before any frame is NOT_READY. */
    {
        engine_handle ticket;
        engine_frame_info frame;
        memset(&frame, 0, sizeof(frame));
        frame.struct_size = sizeof(frame);
        memset(&pixels, 0, sizeof(pixels));
        pixels.struct_size = sizeof(pixels);
        CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_NOT_READY);
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_OK);
        engine_buffer_release(&pixels.data);
        CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_NOT_FOUND);
        CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_ERROR_STALE_HANDLE);
    }

    /* A ticket destroyed before its frame is simply gone: the frame does not serve it, and its slot is reused by the
     * next ticket. */
    {
        engine_handle gone;
        engine_handle next;
        engine_frame_info frame;
        memset(&frame, 0, sizeof(frame));
        frame.struct_size = sizeof(frame);
        CHECK_STATUS(g_api->surface_read_pixels(surface, &gone), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_destroy(gone), ENGINE_OK);
        CHECK_STATUS(g_api->surface_read_pixels(surface, &next), ENGINE_OK);
        CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_finish(gone, &pixels), ENGINE_ERROR_STALE_HANDLE);
        CHECK_STATUS(g_api->pixels_finish(next, &pixels), ENGINE_OK);
        engine_buffer_release(&pixels.data);
        CHECK_STATUS(g_api->pixels_destroy(next), ENGINE_OK);
    }

    /* Another background: sRGB mid grey (0.5 linear is 188 in the sRGB image). */
    CHECK_STATUS(g_api->surface_set_background(surface, half), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(AllPixels(&pixels, 188, 188, 188, 2));
    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);

    /* The host resizes its window and tells the engine: the image follows. */
    XResizeWindow(display, window, 320, 200);
    WaitForSize(display, window, 320, 200);
    CHECK_STATUS(g_api->surface_resized(surface, 320, 200, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 320 && pixels.height == 200);
    CHECK(AllPixels(&pixels, 255, 0, 0, 0));
    engine_buffer_release(&pixels.data);

    /* The host hides its window (size 0): the engine draws nothing and the read-back stays pending; with a real size
     * again it resumes. */
    CHECK_STATUS(g_api->surface_resized(surface, 0, 0, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_ERROR_NOT_READY);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.visible == 0 && state.frames_skipped >= 1 && state.lost == 0);
    CHECK_STATUS(g_api->surface_resized(surface, 320, 200, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 320 && pixels.height == 200);
    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.visible == 1 && state.image_width == 320 && state.image_height == 200 && state.frames_presented >= 5);

    /* Unmapping the window (the host's own business) does not stop the engine from rendering to it. */
    XUnmapWindow(display, window);
    XSync(display, False);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    engine_buffer_release(&pixels.data);

    /* The host destroys its window under the engine: the frames go on without a crash, and a new window recovers
     * through surface_rebind. */
    {
        int frames_after;
        Window fresh;
        engine_surface_info info2;
        engine_frame_info frame;
        memset(&frame, 0, sizeof(frame));
        frame.struct_size = sizeof(frame);
        XDestroyWindow(display, window);
        XSync(display, False);
        for (frames_after = 0; frames_after < 5; ++frames_after)
        {
            CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        }
        XSync(display, False);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        printf("host_surface: after the window was destroyed: lost=%u visible=%u skipped=%llu, %d X errors\n",
               state.lost, state.visible, (unsigned long long)state.frames_skipped, g_x_errors);
        fresh = MakeWindow(display, 160, 100);
        info2 = InfoOf(display, fresh, 160, 100);
        CHECK_STATUS(g_api->surface_rebind(surface, &info2), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.lost == 0 && state.recoveries == 1);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(pixels.width == 160 && pixels.height == 100);
        CHECK(AllPixels(&pixels, 255, 0, 0, 0));
        engine_buffer_release(&pixels.data);

        /* A read-back ticket whose surface is destroyed fails with CANCELLED, and the engine refuses to go while the
         * surface exists. */
        {
            engine_handle ticket;
            memset(&pixels, 0, sizeof(pixels));
            pixels.struct_size = sizeof(pixels);
            CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
            CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE);
            CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
            CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_CANCELLED);
            CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
            CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_ERROR_STALE_HANDLE);
        }
        CHECK_STATUS(g_api->world_destroy(world), ENGINE_OK);
        XDestroyWindow(display, fresh);
        XSync(display, False);
    }
}

int main(void)
{
    Display* display;
    Window window;
    engine_handle engine;
    engine_info info;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_surface: engine_get_api failed\n");
        return 2;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("host_surface: skipped: no X display\n");
        return 0;
    }
    XSetErrorHandler(&CountXError);
    engine = NewEngine();
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    if ((info.caps & ENGINE_CAP_SURFACE_X11) == 0 || (info.caps & ENGINE_CAP_READBACK) == 0)
    {
        printf("host_surface: skipped: the library has no X11 surface support\n");
        CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
        XCloseDisplay(display);
        return 0;
    }
    window = MakeWindow(display, 200, 120);
    TestArguments(engine, display, window);
    TestRendering(engine, display, window);
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    XCloseDisplay(display);
    if (g_failures != 0)
    {
        printf("host_surface: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_surface: ok\n");
    return 0;
}
