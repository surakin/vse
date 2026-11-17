/* Phase 24 Day 369: host_window.h on Windows (Win32): a window class of the host's, borderless windows of the exact
   client size (a captioned window has a smallest width, and the golden image is 160 pixels wide), a message loop turned
   by the polls and at each window's opening and closing, and the keys and pointer motion the walk-around reads. The
   engine draws into the HWND with Direct3D 12 or Vulkan: host_window_api_flags (host_window.h) reads HOST_SURFACE_API.
 */
#include <stdlib.h>
#include <string.h>

#include "os/windows/win32.h"
#include "unity/windows_enable.h"
#include <wtsapi32.h>
#include "unity/windows_disable.h"

#include "host_window.h"

#define HOST_WINDOW_EVENTS 64

struct host_window
{
    HWND hwnd;
    uint32_t width;
    uint32_t height;
    host_window_event events[HOST_WINDOW_EVENTS]; /* written by the window procedure, read by host_window_poll */
    unsigned head;
    unsigned tail;
};

static const wchar_t kHostWindowClass[] = L"engine_host_window";
static int g_class_registered = 0;

static void Queue(host_window* window, const host_window_event* e)
{
    if (window != NULL && window->tail - window->head < HOST_WINDOW_EVENTS)
    {
        window->events[window->tail % HOST_WINDOW_EVENTS] = *e;
        ++window->tail;
    }
}

static int KeyOf(WPARAM vk)
{
    return vk == 'W'         ? HOST_KEY_W
           : vk == 'S'       ? HOST_KEY_S
           : vk == 'A'       ? HOST_KEY_A
           : vk == 'D'       ? HOST_KEY_D
           : vk == VK_ESCAPE ? HOST_KEY_ESCAPE
                             : 0;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    host_window* window = (host_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    host_window_event e;
    memset(&e, 0, sizeof(e));
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
        e.type = HOST_EVENT_KEY;
        e.key = KeyOf(wparam);
        e.down = message == WM_KEYDOWN;
        if (e.key != 0)
        {
            Queue(window, &e);
        }
        return 0;
    case WM_MOUSEMOVE:
        e.type = HOST_EVENT_MOTION;
        e.x = (int)(short)LOWORD(lparam);
        Queue(window, &e);
        return 0;
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

/* One turn of the host's message loop: what is queued for this thread's windows. */
static void Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* Whether the session is locked: the desktop shows nothing then (the window tests skip, as host_win32.c does). */
static int SessionLocked(void)
{
    WTSINFOEXW* info = NULL;
    DWORD bytes = 0;
    int locked = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, (LPWSTR*)&info,
                                    &bytes) &&
        info != NULL && info->Level == 1)
    {
        locked = info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
    }
    if (info != NULL)
    {
        WTSFreeMemory(info);
    }
    return locked;
}

int host_windows_begin(const char** why)
{
    WNDCLASSEXW wc;
    if (SessionLocked())
    {
        *why = "the session is locked";
        return 0;
    }
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kHostWindowClass;
    if (RegisterClassExW(&wc) == 0)
    {
        *why = "RegisterClassExW failed";
        return 0;
    }
    g_class_registered = 1;
    return 1;
}

void host_windows_end(void)
{
    if (g_class_registered)
    {
        UnregisterClassW(kHostWindowClass, GetModuleHandleW(NULL));
        g_class_registered = 0;
    }
}

uint32_t host_window_surface_cap(void)
{
    return ENGINE_CAP_SURFACE_WIN32;
}

host_window* host_window_open(uint32_t width, uint32_t height)
{
    host_window* w = (host_window*)calloc(1, sizeof(host_window));
    if (w == NULL)
    {
        return NULL;
    }
    w->width = width;
    w->height = height;
    w->hwnd = CreateWindowExW(0, kHostWindowClass, L"engine scene host", WS_POPUP, 40, 40, (int)width, (int)height,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (w->hwnd == NULL)
    {
        free(w);
        return NULL;
    }
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
    ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
    Pump();
    return w;
}

void host_window_close(host_window* window)
{
    if (window == NULL)
    {
        return;
    }
    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, 0); /* nothing queues into it while it goes */
    DestroyWindow(window->hwnd);
    Pump();
    free(window);
}

void host_window_describe(const host_window* window, engine_surface_info* info)
{
    if (window == NULL)
    {
        info->kind = 0;
        return;
    }
    Pump();
    info->kind = ENGINE_SURFACE_WIN32;
    info->width = window->width;
    info->height = window->height;
    info->scale = 1.0f;
    info->display = GetModuleHandleW(NULL); /* the HINSTANCE of the window class */
    info->surface = window->hwnd;
    info->window_id = 0;
}

int host_window_poll(host_window* window, host_window_event* out)
{
    Pump();
    if (window->head == window->tail)
    {
        return 0;
    }
    *out = window->events[window->head % HOST_WINDOW_EVENTS];
    ++window->head;
    return 1;
}

int host_window_post_key(host_window* window, int key, int down)
{
    const WPARAM vk = key == HOST_KEY_W   ? 'W'
                      : key == HOST_KEY_S ? 'S'
                      : key == HOST_KEY_A ? 'A'
                      : key == HOST_KEY_D ? 'D'
                                          : VK_ESCAPE;
    /* the message a keyboard's key gives the window, through its queue (a posted message, not injected input) */
    const LPARAM flags = down ? 1 : (LPARAM)((1u << 30) | (1u << 31) | 1u);
    return PostMessageW(window->hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, flags) ? 1 : 0;
}

/* Showing pixels in the window is Day 381 (the Windows PC): until then walk_around --offscreen only draws and takes the
   pictures. */
int host_window_can_show(void)
{
    return 0;
}

int host_window_show(host_window* window, const void* rgba, uint32_t width, uint32_t height, uint32_t stride)
{
    (void)window;
    (void)rgba;
    (void)width;
    (void)height;
    (void)stride;
    return 0;
}
