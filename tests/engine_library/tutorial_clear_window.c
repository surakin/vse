/* The program of docs/tutorials/loading-the-engine.md: a C host with its own Xlib window embeds the engine, clears the
   window to a color for about two seconds, and checks what the engine drew by reading the pixels back. It uses the
   engine through `engine_get_api` only. Exit 0 also when there is no display or the build cannot render into a window
   (it says so). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>

#include "engine/engine_c.h"

#define WIDTH 320u
#define HEIGHT 200u

int main(void)
{
    const engine_api* api = NULL;
    engine_handle engine;
    engine_handle surface;
    engine_create_info create;
    engine_info about;
    engine_surface_info target;
    engine_frame_info frame;
    engine_pixels pixels;
    engine_handle ticket;
    const float color[4] = {0.0f, 0.5f, 1.0f, 1.0f};
    struct timespec pause = {0, 16 * 1000 * 1000};
    int i;
    int ok;

    /* 1. The host's own window: the engine never creates, moves or destroys it. */
    Display* display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("tutorial: skipped (no display)\n");
        return 0;
    }
    const Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, WIDTH, HEIGHT, 0, 0, 0);
    XMapWindow(display, window);
    XSync(display, False);

    /* 2. Ask for the function table, then make an engine. */
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &api) != ENGINE_OK)
    {
        printf("tutorial: no function table\n");
        return 1;
    }
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    memset(&engine, 0, sizeof(engine));
    if (api->create(&create, &engine) != ENGINE_OK)
    {
        printf("tutorial: create failed\n");
        return 1;
    }

    /* 3. Does this build draw into a window? */
    memset(&about, 0, sizeof(about));
    about.struct_size = sizeof(about);
    api->get_info(engine, &about);
    if ((about.caps & ENGINE_CAP_SURFACE_X11) == 0)
    {
        printf("tutorial: skipped (this build has no X11 surfaces)\n");
        api->destroy(engine);
        return 0;
    }

    /* 4. Hand the window to the engine: the host's Display and Window, its size in pixels and its scale. */
    memset(&target, 0, sizeof(target));
    target.struct_size = sizeof(target);
    target.kind = ENGINE_SURFACE_X11;
    target.width = WIDTH;
    target.height = HEIGHT;
    target.scale = 1.0f;
    target.display = display;
    target.window_id = (uint64_t)window;
    memset(&surface, 0, sizeof(surface));
    if (api->surface_create(engine, &target, &surface) != ENGINE_OK)
    {
        printf("tutorial: skipped (no device can present to this window)\n");
        api->destroy(engine);
        return 0;
    }
    api->surface_set_background(surface, color);

    /* 5. The host's own loop: it keeps its event handling (the engine opens no connection of its own) and calls `frame`
     * when it wants one drawn. */
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = sizeof(frame);
    frame.delta_seconds = 0.016;
    for (i = 0; i < 120; ++i)
    {
        while (XPending(display) > 0)
        {
            XEvent event;
            XNextEvent(display, &event);
        }
        api->frame(engine, &frame, NULL);
        nanosleep(&pause, NULL);
    }

    /* 6. A screenshot: a ticket now, the pixels after the next frame that draws the surface. */
    memset(&pixels, 0, sizeof(pixels));
    pixels.struct_size = sizeof(pixels);
    ok = api->surface_read_pixels(surface, &ticket) == ENGINE_OK;
    ok = ok && api->frame(engine, &frame, NULL) == ENGINE_OK;
    ok = ok && api->pixels_finish(ticket, &pixels) == ENGINE_OK;
    if (ok)
    {
        const unsigned char* p = (const unsigned char*)pixels.data.data + (size_t)(HEIGHT / 2) * pixels.stride +
                                 (size_t)(WIDTH / 2) * 4;
        printf("tutorial: the middle pixel is %d %d %d\n", p[0], p[1], p[2]);
        ok = p[0] == 0 && p[2] == 255 && p[1] > 150 &&
             p[1] < 230; /* 0.5 reads about 188 in the sRGB encoding of the swapchain */
        engine_buffer_release(&pixels.data);
    }
    api->pixels_destroy(ticket);

    /* 7. Clean up in this order: the surface before the window, everything before the engine. */
    api->surface_destroy(surface);
    api->destroy(engine);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    printf(ok ? "tutorial: ok\n" : "tutorial: the pixels are not the clear color\n");
    return ok ? 0 : 1;
}
