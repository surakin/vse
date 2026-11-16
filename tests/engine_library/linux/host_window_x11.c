/* Phase 24 Day 370: host_window.h on X11 (Xlib), the window code host_scene.c and walk_around.c had of their own from
   Days 359 and 371. */
#include <stdlib.h>
#include <string.h>

#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "host_os.h"
#include "host_window.h"

struct host_window
{
    Window window;
    int input_selected; /* the first poll selects key and pointer input */
    uint32_t width;
    uint32_t height;
};

static Display* g_display = NULL;

int host_windows_begin(const char** why)
{
    g_display = XOpenDisplay(NULL);
    if (g_display == NULL)
    {
        *why = "no X display";
        return 0;
    }
    return 1;
}

void host_windows_end(void)
{
    if (g_display != NULL)
    {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
}

uint32_t host_window_surface_cap(void)
{
    return ENGINE_CAP_SURFACE_X11;
}

host_window* host_window_open(uint32_t width, uint32_t height)
{
    host_window* w = (host_window*)calloc(1, sizeof(host_window));
    if (w == NULL)
    {
        return NULL;
    }
    w->window = XCreateSimpleWindow(g_display, DefaultRootWindow(g_display), 0, 0, width, height, 0, 0, 0);
    w->width = width;
    w->height = height;
    XSelectInput(g_display, w->window, StructureNotifyMask);
    XMapWindow(g_display, w->window);
    {
        /* wait for the server to map it (a reparenting window manager maps it later than XMapWindow returns) */
        int tries;
        for (tries = 0; tries < 2000; ++tries)
        {
            XEvent event;
            if (XCheckTypedWindowEvent(g_display, w->window, MapNotify, &event))
            {
                break;
            }
            XSync(g_display, False);
            host_sleep_ms(1);
        }
    }
    XSelectInput(g_display, w->window, NoEventMask);
    XSync(g_display, False);
    return w;
}

void host_window_close(host_window* window)
{
    if (window == NULL)
    {
        return;
    }
    XDestroyWindow(g_display, window->window);
    XSync(g_display, False);
    free(window);
}

void host_window_describe(const host_window* window, engine_surface_info* info)
{
    if (window == NULL)
    {
        info->kind = 0;
        return;
    }
    info->kind = ENGINE_SURFACE_X11;
    info->width = window->width;
    info->height = window->height;
    info->scale = 1.0f;
    info->display = g_display;
    info->surface = NULL;
    info->window_id = (uint64_t)window->window;
}

int host_window_poll(host_window* window, host_window_event* out)
{
    if (!window->input_selected)
    {
        XSelectInput(g_display, window->window, KeyPressMask | KeyReleaseMask | PointerMotionMask);
        window->input_selected = 1;
    }
    while (XPending(g_display) > 0)
    {
        XEvent event;
        XNextEvent(g_display, &event);
        if (event.xany.window != window->window)
        {
            continue;
        }
        if (event.type == KeyPress || event.type == KeyRelease)
        {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            const int mapped = key == XK_w        ? HOST_KEY_W
                               : key == XK_s      ? HOST_KEY_S
                               : key == XK_a      ? HOST_KEY_A
                               : key == XK_d      ? HOST_KEY_D
                               : key == XK_Escape ? HOST_KEY_ESCAPE
                                                  : 0;
            if (mapped == 0)
            {
                continue;
            }
            out->type = HOST_EVENT_KEY;
            out->down = event.type == KeyPress;
            out->key = mapped;
            return 1;
        }
        if (event.type == MotionNotify)
        {
            out->type = HOST_EVENT_MOTION;
            out->x = event.xmotion.x;
            return 1;
        }
    }
    return 0;
}

int host_window_post_key(host_window* window, int key, int down)
{
    const KeySym sym = key == HOST_KEY_W   ? XK_w
                       : key == HOST_KEY_S ? XK_s
                       : key == HOST_KEY_A ? XK_a
                       : key == HOST_KEY_D ? XK_d
                                           : XK_Escape;
    XEvent event;
    if (!window->input_selected)
    {
        /* XSendEvent delivers only to a client that selected the event on the window */
        XSelectInput(g_display, window->window, KeyPressMask | KeyReleaseMask | PointerMotionMask);
        window->input_selected = 1;
    }
    memset(&event, 0, sizeof(event));
    event.xkey.type = down ? KeyPress : KeyRelease;
    event.xkey.display = g_display;
    event.xkey.window = window->window;
    event.xkey.root = DefaultRootWindow(g_display);
    event.xkey.same_screen = True;
    event.xkey.keycode = XKeysymToKeycode(g_display, sym);
    if (event.xkey.keycode == 0 ||
        XSendEvent(g_display, window->window, True, down ? KeyPressMask : KeyReleaseMask, &event) == 0)
    {
        return 0;
    }
    XSync(g_display, False);
    return 1;
}

#include "unity/x11_disable.h"
