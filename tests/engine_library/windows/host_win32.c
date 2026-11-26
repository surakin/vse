/* Phase 23 Day 354: a C program that owns a Win32 window (its own window class, window procedure and message loop)
   embeds the engine and has it render into that window, with Direct3D 12 and with Vulkan, the way an application with
   its own toolkit would. The Windows counterpart of host_surface.c (X11): the capability bits, the arguments, a frame
   of a known color read back through the function table, the host's resize (the image follows the window), a hidden
   window (size 0: nothing is drawn, the read-back stays pending), the window destroyed under the engine (frames go on
   without a crash) and the recovery on a new window through surface_rebind. The window procedure forwards what the
   window receives (pointer, buttons, keys, characters, focus) through surface_input, and the host answers the engine's
   cursor and clipboard requests with Win32 (SetCursor, the clipboard). The engine never touches the window itself. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/windows/win32.h"
#include "unity/windows_enable.h"
#include <wtsapi32.h>
#include "unity/windows_disable.h"

#include "engine/engine_c.h"

static int g_failures = 0;
static const engine_api* g_api = NULL;
static engine_handle g_surface; /* the surface the window procedure forwards to, or 0 */
static int g_forwarded = 0;     /* events surface_input accepted */
static int g_forward_failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("host_win32: line %d: CHECK failed: %s\n", __LINE__, #condition);                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static void ReportStatus(int line, const char* call, engine_status got, int expected)
{
    const engine_string name = g_api->status_name(got);
    printf("host_win32: line %d: %s returned %d (%.*s), expected %d\n", line, call, (int)got, (int)name.length,
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

/* ---- the host's window ---- */

static engine_input_event InputOf(uint32_t type)
{
    engine_input_event e;
    memset(&e, 0, sizeof(e));
    e.struct_size = sizeof(e);
    e.type = type;
    return e;
}

static void Forward(const engine_input_event* e)
{
    if (g_surface.value == 0)
    {
        return;
    }
    if (g_api->surface_input(g_surface, e) == ENGINE_OK)
    {
        ++g_forwarded;
    }
    else
    {
        ++g_forward_failures;
    }
}

/* What a host with its own toolkit does with its window's messages: it hands them to the engine's surface. */
static LRESULT CALLBACK HostWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    engine_input_event e;
    switch (message)
    {
    case WM_MOUSEMOVE:
        e = InputOf(ENGINE_INPUT_POINTER_MOVE);
        e.x = (double)(short)LOWORD(lparam);
        e.y = (double)(short)HIWORD(lparam);
        Forward(&e);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        e = InputOf(ENGINE_INPUT_POINTER_BUTTON);
        e.button = ENGINE_BUTTON_LEFT;
        e.down = message == WM_LBUTTONDOWN ? 1 : 0;
        e.x = (double)(short)LOWORD(lparam);
        e.y = (double)(short)HIWORD(lparam);
        Forward(&e);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (wparam >= 'A' && wparam <= 'Z')
        {
            e = InputOf(ENGINE_INPUT_KEY);
            e.key = (uint32_t)(ENGINE_KEY_A + (wparam - 'A'));
            e.down = message == WM_KEYDOWN ? 1 : 0;
            Forward(&e);
        }
        return 0;
    case WM_CHAR:
        if (wparam >= 0x20 && wparam < 0x7f)
        {
            char text = (char)wparam;
            e = InputOf(ENGINE_INPUT_TEXT);
            e.text.data = &text;
            e.text.length = 1;
            Forward(&e);
        }
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        e = InputOf(ENGINE_INPUT_FOCUS);
        e.down = message == WM_SETFOCUS ? 1 : 0;
        Forward(&e);
        return 0;
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

static const wchar_t kClassName[] = L"engine_host_win32";

static int RegisterHostClass(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &HostWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

/* Pumps what is queued for this thread's windows (a host's message loop, one turn). */
static void Pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* A window whose client area is `width` x `height`, shown. The sizes the test uses are above the smallest width Windows
   gives a window with a caption (a 200-pixel client area came out 232 wide): Vulkan draws the window's real client
   area, Direct3D 12 the size the host gives. */
static HWND MakeWindow(unsigned width, unsigned height)
{
    RECT r = {0, 0, (LONG)width, (LONG)height};
    HWND hwnd;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowExW(0, kClassName, L"engine host_win32", WS_OVERLAPPEDWINDOW, 40, 40, r.right - r.left,
                           r.bottom - r.top, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hwnd != NULL)
    {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Pump();
    }
    return hwnd;
}

/* The host resizes its window's client area (what a toolkit does, then it tells the engine). */
static void ResizeClient(HWND hwnd, unsigned width, unsigned height)
{
    RECT r = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump();
}

/* ---- the host's answers to the engine's requests ---- */

static uint32_t g_cursor_shape = 0xffffffffu;

static int HostSetCursor(void* user, uint32_t shape)
{
    LPCWSTR id = IDC_ARROW;
    (void)user;
    switch (shape)
    {
    case ENGINE_CURSOR_HIDDEN:
        SetCursor(NULL);
        g_cursor_shape = shape;
        return 1;
    case ENGINE_CURSOR_ARROW: id = IDC_ARROW; break;
    default: id = IDC_ARROW; break;
    }
    SetCursor(LoadCursorW(NULL, id));
    g_cursor_shape = shape;
    return 1;
}

/* The text of the clipboard, kept for clipboard_get (valid until the next callback, as engine_c.h says). */
static char g_clipboard_text[1024];

static int HostClipboardSet(void* user, engine_string text)
{
    wchar_t wide[512];
    int n;
    HGLOBAL memory;
    (void)user;
    n = MultiByteToWideChar(CP_UTF8, 0, text.data, (int)text.length, wide, 511);
    if (n < 0 || !OpenClipboard(NULL))
    {
        return 0;
    }
    wide[n] = L'\0';
    EmptyClipboard();
    memory = GlobalAlloc(GMEM_MOVEABLE, (size_t)(n + 1) * sizeof(wchar_t));
    if (memory != NULL)
    {
        memcpy(GlobalLock(memory), wide, (size_t)(n + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == NULL)
        {
            GlobalFree(memory);
            memory = NULL;
        }
    }
    CloseClipboard();
    return memory != NULL;
}

static int HostClipboardGet(void* user, engine_string* text)
{
    HANDLE data;
    int n = 0;
    (void)user;
    if (!OpenClipboard(NULL))
    {
        return 0;
    }
    data = GetClipboardData(CF_UNICODETEXT);
    if (data != NULL)
    {
        const wchar_t* wide = (const wchar_t*)GlobalLock(data);
        if (wide != NULL)
        {
            n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, g_clipboard_text, (int)sizeof(g_clipboard_text), NULL, NULL);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    if (n <= 1)
    {
        return 0;
    }
    text->data = g_clipboard_text;
    text->length = (size_t)(n - 1);
    return 1;
}

static engine_surface_callbacks g_callbacks;

/* ---- the engine ---- */

static engine_surface_info InfoOf(HWND hwnd, unsigned width, unsigned height, uint32_t api_flag)
{
    engine_surface_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.kind = ENGINE_SURFACE_WIN32;
    info.flags = api_flag;
    info.width = width;
    info.height = height;
    info.scale = 1.0f;
    info.display = GetModuleHandleW(NULL);
    info.surface = hwnd;
    info.callbacks = &g_callbacks;
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
    Pump();
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

static void TestArguments(engine_handle engine, HWND hwnd)
{
    engine_surface_info info = InfoOf(hwnd, 400, 240, 0);
    engine_surface_info bad;
    engine_handle surface;
    memset(&surface, 0, sizeof(surface));
    bad = info;
    bad.kind = ENGINE_SURFACE_X11; /* not this platform's */
    bad.window_id = 1;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_UNSUPPORTED);
    bad = info;
    bad.flags = ENGINE_SURFACE_FLAG_VULKAN | ENGINE_SURFACE_FLAG_D3D12;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.flags = 0x100;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.surface = NULL;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    bad = info;
    bad.display = NULL;
    CHECK_STATUS(g_api->surface_create(engine, &bad, &surface), ENGINE_ERROR_INVALID_ARGUMENT);
    CHECK(surface.value == 0);
}

/* What the window procedure forwards: messages posted to the host's own window (no real input is made). */
static void TestForwarding(HWND hwnd)
{
    engine_surface_state state;
    const int before = g_forwarded;
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(20, 30));
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 30));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(20, 30));
    PostMessageW(hwnd, WM_KEYDOWN, 'A', 0);
    PostMessageW(hwnd, WM_CHAR, 'a', 0);
    PostMessageW(hwnd, WM_KEYUP, 'A', 0);
    Pump();
    CHECK(g_forwarded - before >= 6);
    CHECK(g_forward_failures == 0);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(g_surface, &state), ENGINE_OK);
    CHECK(state.input_dropped == 0);
    /* The cursor callback, as the engine's UI calls it: the host changes its window's cursor. */
    CHECK(g_callbacks.set_cursor(g_callbacks.user, ENGINE_CURSOR_ARROW) == 1 && g_cursor_shape == ENGINE_CURSOR_ARROW);
}

/* The window's real client size equals what the host asked for. */
static int ClientIs(HWND hwnd, unsigned width, unsigned height)
{
    RECT r;
    return GetClientRect(hwnd, &r) && (unsigned)(r.right - r.left) == width && (unsigned)(r.bottom - r.top) == height;
}

/* Whether this library draws with Vulkan: a tree built without the Vulkan SDK has Direct3D 12 only and refuses a Vulkan
   surface as UNSUPPORTED. */
static int g_vulkan_built = 1;

static int VulkanBuilt(engine_handle engine, HWND window)
{
    engine_surface_info info = InfoOf(window, 400, 240, ENGINE_SURFACE_FLAG_VULKAN);
    engine_handle probe;
    engine_status made;
    memset(&probe, 0, sizeof(probe));
    made = g_api->surface_create(engine, &info, &probe);
    if (made == ENGINE_OK)
    {
        CHECK_STATUS(g_api->surface_destroy(probe), ENGINE_OK);
    }
    return made != ENGINE_ERROR_UNSUPPORTED;
}

static void TestRendering(engine_handle engine, HWND window, uint32_t api_flag, const char* api_name)
{
    engine_surface_info info = InfoOf(window, 400, 240, api_flag);
    engine_surface_state state;
    engine_handle surface;
    engine_pixels pixels;
    float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float half[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    memset(&surface, 0, sizeof(surface));
    info.present_mode = ENGINE_PRESENT_MAILBOX;
    if (g_api->surface_create(engine, &info, &surface) != ENGINE_OK)
    {
        engine_buffer why;
        memset(&why, 0, sizeof(why));
        g_api->last_error(&why);
        printf("host_win32: %s: no surface on the window: %.*s\n", api_name, (int)why.size, (const char*)why.data);
        engine_buffer_release(&why);
        ++g_failures;
        return;
    }
    g_surface = surface;
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);
    TestForwarding(window);

    /* The image is the window's size, and its pixels are the clear color. */
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(ClientIs(window, 400, 240));
    CHECK(pixels.width == 400 && pixels.height == 240 && pixels.stride == 400 * 4);
    CHECK(AllPixels(&pixels, 255, 0, 0, 0));
    engine_buffer_release(&pixels.data);
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.present_mode == ENGINE_PRESENT_MAILBOX || state.present_mode == ENGINE_PRESENT_FIFO);

    /* sRGB mid grey (0.5 linear is 188 in the sRGB image). */
    CHECK_STATUS(g_api->surface_set_background(surface, half), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(AllPixels(&pixels, 188, 188, 188, 2));
    engine_buffer_release(&pixels.data);
    CHECK_STATUS(g_api->surface_set_background(surface, red), ENGINE_OK);

    /* The host resizes its window and tells the engine: the image follows. */
    ResizeClient(window, 480, 300);
    CHECK(ClientIs(window, 480, 300));
    CHECK_STATUS(g_api->surface_resized(surface, 480, 300, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 480 && pixels.height == 300);
    CHECK(AllPixels(&pixels, 255, 0, 0, 0));
    engine_buffer_release(&pixels.data);

    /* Hidden (size 0): nothing is drawn and the read-back stays pending; with a size again it resumes. */
    CHECK_STATUS(g_api->surface_resized(surface, 0, 0, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_ERROR_NOT_READY);
    CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
    CHECK(state.visible == 0 && state.frames_skipped >= 1 && state.lost == 0);
    CHECK_STATUS(g_api->surface_resized(surface, 480, 300, 1.0f), ENGINE_OK);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    CHECK(pixels.width == 480 && pixels.height == 300);
    engine_buffer_release(&pixels.data);

    /* The host hides its window (its own business): the engine renders on. */
    ShowWindow(window, SW_HIDE);
    CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
    engine_buffer_release(&pixels.data);

    /* The host destroys its window under the engine: frames go on without a crash, and a new window recovers through
     * surface_rebind. */
    {
        int frames_after;
        HWND fresh;
        engine_surface_info info2;
        engine_frame_info frame;
        engine_handle ticket;
        memset(&frame, 0, sizeof(frame));
        frame.struct_size = sizeof(frame);
        DestroyWindow(window);
        Pump();
        for (frames_after = 0; frames_after < 5; ++frames_after)
        {
            CHECK_STATUS(g_api->frame(engine, &frame, NULL), ENGINE_OK);
        }
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        printf("host_win32: %s: after the window was destroyed: lost=%u visible=%u skipped=%llu\n", api_name,
               state.lost, state.visible, (unsigned long long)state.frames_skipped);
        fresh = MakeWindow(360, 220);
        CHECK(fresh != NULL && ClientIs(fresh, 360, 220));
        info2 = InfoOf(fresh, 360, 220, api_flag);
        CHECK_STATUS(g_api->surface_rebind(surface, &info2), ENGINE_OK);
        CHECK_STATUS(g_api->surface_state(surface, &state), ENGINE_OK);
        CHECK(state.lost == 0 && state.recoveries == 1);
        CHECK_STATUS(FrameAndRead(engine, surface, &pixels), ENGINE_OK);
        CHECK(pixels.width == 360 && pixels.height == 220);
        CHECK(AllPixels(&pixels, 255, 0, 0, 0));
        engine_buffer_release(&pixels.data);
        /* Another graphics API on rebind is refused: a surface keeps its own. */
        info2.flags = api_flag == ENGINE_SURFACE_FLAG_VULKAN ? ENGINE_SURFACE_FLAG_D3D12 : ENGINE_SURFACE_FLAG_VULKAN;
        /* Without Vulkan in the library, the Vulkan flag is refused before the surface is looked at. */
        CHECK_STATUS(g_api->surface_rebind(surface, &info2),
                     g_vulkan_built ? ENGINE_ERROR_INVALID_ARGUMENT : ENGINE_ERROR_UNSUPPORTED);

        /* A read-back ticket whose surface is destroyed fails with CANCELLED; the engine refuses to go while the
         * surface exists. */
        memset(&pixels, 0, sizeof(pixels));
        pixels.struct_size = sizeof(pixels);
        CHECK_STATUS(g_api->surface_read_pixels(surface, &ticket), ENGINE_OK);
        CHECK_STATUS(g_api->destroy(engine), ENGINE_ERROR_IN_USE);
        g_surface.value = 0;
        CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_OK);
        CHECK_STATUS(g_api->pixels_finish(ticket, &pixels), ENGINE_ERROR_CANCELLED);
        CHECK_STATUS(g_api->pixels_destroy(ticket), ENGINE_OK);
        CHECK_STATUS(g_api->surface_destroy(surface), ENGINE_ERROR_STALE_HANDLE);
        DestroyWindow(fresh);
        Pump();
    }
}

/* Whether the session is locked: the desktop shows nothing then, and the test has nothing to look at. */
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

int main(void)
{
    engine_handle engine;
    engine_create_info create;
    engine_info info;
    int round;
    static const uint32_t kApis[2] = {ENGINE_SURFACE_FLAG_D3D12, ENGINE_SURFACE_FLAG_VULKAN};
    static const char* const kApiNames[2] = {"Direct3D 12", "Vulkan"};
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (engine_get_api(ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR, sizeof(engine_api), &g_api) != ENGINE_OK)
    {
        printf("host_win32: engine_get_api failed\n");
        return 2;
    }
    if (SessionLocked())
    {
        printf("host_win32: skipped: the session is locked\n");
        return 0;
    }
    if (!RegisterHostClass())
    {
        printf("host_win32: RegisterClassExW failed\n");
        return 2;
    }
    memset(&g_callbacks, 0, sizeof(g_callbacks));
    g_callbacks.struct_size = sizeof(g_callbacks);
    g_callbacks.set_cursor = &HostSetCursor;
    g_callbacks.clipboard_set = &HostClipboardSet;
    g_callbacks.clipboard_get = &HostClipboardGet;
    memset(&create, 0, sizeof(create));
    create.struct_size = sizeof(create);
    create.flags = ENGINE_CREATE_LOG_SUPPRESS_CONSOLE;
    memset(&engine, 0, sizeof(engine));
    CHECK_STATUS(g_api->create(&create, &engine), ENGINE_OK);
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    CHECK_STATUS(g_api->get_info(engine, &info), ENGINE_OK);
    CHECK((info.caps & ENGINE_CAP_SURFACE_WIN32) != 0 && (info.caps & ENGINE_CAP_READBACK) != 0);
    CHECK((info.caps & (ENGINE_CAP_SURFACE_X11 | ENGINE_CAP_SURFACE_WAYLAND)) == 0);
    for (round = 0; round < 2; ++round)
    {
        HWND window = MakeWindow(400, 240);
        CHECK(window != NULL);
        if (window == NULL)
        {
            break;
        }
        if (round == 0)
        {
            TestArguments(engine, window);
            g_vulkan_built = VulkanBuilt(engine, window);
        }
        if (kApis[round] == ENGINE_SURFACE_FLAG_VULKAN && !g_vulkan_built)
        {
            printf("host_win32: Vulkan: not in this library (built without the Vulkan SDK): skipped\n");
            DestroyWindow(window);
            Pump();
            break;
        }
        TestRendering(engine, window, kApis[round], kApiNames[round]);
    }
    CHECK_STATUS(g_api->destroy(engine), ENGINE_OK);
    g_api->thread_detach();
    UnregisterClassW(kClassName, GetModuleHandleW(NULL));
    if (g_failures != 0)
    {
        printf("host_win32: %d checks failed\n", g_failures);
        return 1;
    }
    printf("host_win32: ok (%s)\n", g_vulkan_built ? "Direct3D 12 and Vulkan" : "Direct3D 12");
    return 0;
}
