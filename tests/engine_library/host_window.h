/* Phase 24 Day 370: the window of a C host that draws a scene (host_scene.c, walk_around.c), the same on every platform
   that has one: open the platform's window system, open and close windows of a size in pixels, describe a window to
   the engine (engine_surface_info), and read the few events the walk-around steers with. A foreign host has its own
   toolkit; this stands for it. linux/host_window_x11.c is Xlib (the hosts' own code until Day 370),
   macos/host_window_cocoa.m is AppKit (an NSWindow whose content NSView the engine draws into with Metal),
   windows/host_window_win32.c is Win32 (Day 369: an HWND drawn with Direct3D 12 or Vulkan). Plain C. */
#ifndef HOST_WINDOW_H
#define HOST_WINDOW_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine_c.h"
#include "host_surface_api.h"

typedef struct host_window host_window;

/* Opens the window system (the X display; the shared NSApplication). Returns 1, or 0 with `*why` set (a static
   sentence) when no window can appear here: the host then prints "skipped" and ends. */
int host_windows_begin(const char** why);

/* Closes what host_windows_begin opened; every window is closed first. */
void host_windows_end(void);

/* The engine_info.caps bit the library must have to draw into this platform's windows (ENGINE_CAP_SURFACE_X11 or
   ENGINE_CAP_SURFACE_COCOA). */
uint32_t host_window_surface_cap(void);

/* Opens and shows a window whose drawable area is `width` x `height` pixels (on a Retina display, half as many points),
   and waits (up to two seconds) until the window system has mapped it (X11: MapNotify; Cocoa: the window is visible).
   NULL on failure. */
host_window* host_window_open(uint32_t width, uint32_t height);

/* Closes a window. The engine's surface on it is destroyed or rebound first. */
void host_window_close(host_window* window);

/* Writes the window into `info`: kind, the native handles (display and window, or the NSView), the size in pixels and
   the scale. Every other field (struct_size, flags, present_mode, callbacks) is left as the caller set it. A NULL
   window (a host_window_open that failed) writes kind 0, so the engine refuses the call that uses it (a failed check,
   not a crash). */
void host_window_describe(const host_window* window, engine_surface_info* info);

/* The events the walk-around reads. */
#define HOST_KEY_W 1
#define HOST_KEY_S 2
#define HOST_KEY_A 3
#define HOST_KEY_D 4
#define HOST_KEY_ESCAPE 5

#define HOST_EVENT_KEY 1    /* `key` (HOST_KEY_*), `down` */
#define HOST_EVENT_MOTION 2 /* `x`, in window pixels */

typedef struct host_window_event
{
    int type;
    int key;
    int down;
    int x;
} host_window_event;

/* The next pending event of the window's system for `window`, without waiting: 1 and `*out` filled, or 0. Only the five
   keys and pointer motion are returned; other events are handled by the window system (Cocoa: Command-Q and the other
   key equivalents reach AppKit) or dropped. The first poll of a window starts its input (X11 selects it then, so a
   window nobody polls queues no events). */
int host_window_poll(host_window* window, host_window_event* out);

/* Puts a key press (`down` 1) or release of `key` (HOST_KEY_*) on the window system's queue for `window`, as a
   keyboard would (XSendEvent; an NSEvent posted to the application), for a test to read back with host_window_poll.
   Returns 1, or 0 when it could not be queued. */
int host_window_post_key(host_window* window, int key, int down);

/* 1 when this platform's window code shows pixels (host_window_show): X11 (Day 383), Cocoa (Day 382) and Win32 (Day
   381, a GDI blit). A test that shows pictures checks that every one was shown where this is 1. */
int host_window_can_show(void);

/* Shows RGBA8 pixels (`height` rows of `width` pixels, `stride` bytes apart, top row first) in the window, unscaled
   from the top-left corner: what a toolkit host does with a picture taken from an off-screen surface (walk_around
   --offscreen, Phase 24 Day 383). Returns 1, or 0 when it was not shown: host_window_can_show is 0, the arguments are
   bad (a NULL window or pixels, a zero size, `stride` under `width` * 4) or the window system refused the image. */
int host_window_show(host_window* window, const void* rgba, uint32_t width, uint32_t height, uint32_t stride);

#endif
