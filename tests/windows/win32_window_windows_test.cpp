// The Win32 window backend and the windowed programs' entry point (Phase 13 Day 158). The shared window tests
// (tests/foundation's window_backend_test.cpp and window_fullscreen_test.cpp) run on Windows too; these cover what only
// Windows has: DPI, the minimized state, the close request, destruction with messages still queued, two windows on one
// thread, the modal-loop callback, and the subsystem and arguments of a program written with ENGINE_WINDOWED_PROGRAM.
#include <catch2/catch_test_macros.hpp>

#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "cstr/format.h"

#include "display_session.h"
#include "io/file.h"
#include "io/file_system.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "os/windows/win32.h"
#include "platform/input.h"
#include "platform/windows/win32_window.h"

using namespace engine_placeholder;
using os::win32::Win32Window;

namespace
{

// Pumps until `done` or about a second.
template <typename Pred> bool PumpUntil(Win32Window& window, Pred done)
{
    for (int i = 0; i < 100; ++i)
    {
        window.PumpEvents();
        if (done())
        {
            return true;
        }
        os::SleepNanos(10'000'000);
    }
    return false;
}

u32 ClientWidth(HWND hwnd)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    return static_cast<u32>(r.right - r.left);
}

u32 ClientHeight(HWND hwnd)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    return static_cast<u32>(r.bottom - r.top);
}

// Sets the outer size so the client area is `width` x `height` at the window's DPI (as a user's drag would end).
void ResizeClient(HWND hwnd, u32 width, u32 height)
{
    RECT outer{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectExForDpi(&outer, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                             static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), GetDpiForWindow(hwnd));
    SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The PE header's subsystem field of an executable file: 2 is a GUI program, 3 a console program; 0 if the file cannot
// be read.
u32 Subsystem(const io::Path& path)
{
    ArenaAllocator arena(64 * 1024 * 1024);
    u8* header = nullptr;
    usize got = 0;
    if (!io::ReadFile(path, arena, &header, &got).Ok() || got < 0x40 || header[0] != 'M' || header[1] != 'Z')
    {
        return 0;
    }
    u32 pe = 0;
    MemCopy(&pe, header + 0x3C, 4);
    // "PE\0\0", the 20-byte file header, then the optional header, whose Subsystem field is at offset 68.
    if (pe + 24 + 70 > got || MemCompare(header + pe, "PE\0\0", 4) != 0)
    {
        return 0;
    }
    u16 subsystem = 0;
    MemCopy(&subsystem, header + pe + 24 + 68, 2);
    return subsystem;
}

struct ProbeResult
{
    DWORD exit_code = 0xFFFFFFFF;
    char out[8192] = {};
    char err[1024] = {};
};

void ReadAll(HANDLE h, char* buffer, usize capacity)
{
    usize used = 0;
    for (;;)
    {
        DWORD got = 0;
        if (used + 1 >= capacity ||
            !ReadFile(h, buffer + used, static_cast<DWORD>(capacity - 1 - used), &got, nullptr) || got == 0)
        {
            break;
        }
        used += got;
    }
    buffer[used] = '\0';
}

// Starts the windowed probe with `arguments` (a raw command-line tail), its standard output and error on pipes, as
// ctest starts a program.
ProbeResult RunWindowedProbe(const wchar_t* arguments)
{
    ProbeResult result;
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
    HANDLE out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
    REQUIRE(CreatePipe(&out_read, &out_write, &inherit, 0));
    REQUIRE(CreatePipe(&err_read, &err_write, &inherit, 0));
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
    wchar_t command[2048];
    WFormatTo(command, 2048, L"\"%ls\" %ls", WINDOWED_PROBE_PATH, arguments);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = out_write;
    startup.hStdError = err_write;
    PROCESS_INFORMATION process = {};
    REQUIRE(CreateProcessW(nullptr, command, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process));
    CloseHandle(out_write);
    CloseHandle(err_write);
    ReadAll(out_read, result.out, sizeof(result.out));
    ReadAll(err_read, result.err, sizeof(result.err));
    REQUIRE(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(out_read);
    CloseHandle(err_read);
    return result;
}

void AppendHexLine(char* out, usize capacity, const char* label, int index, const char* utf8)
{
    usize used = CStrLength(out);
    used += static_cast<usize>(FormatTo(out + used, capacity - used, "%s %d ", label, index));
    for (const char* c = utf8; *c != '\0'; ++c)
    {
        used += static_cast<usize>(
            FormatTo(out + used, capacity - used, "%02x", static_cast<unsigned>(static_cast<u8>(*c))));
    }
    FormatTo(out + used, capacity - used, "\n");
}

void StripCarriageReturns(char* text)
{
    usize j = 0;
    for (usize i = 0; text[i] != '\0'; ++i)
    {
        if (text[i] != '\r')
        {
            text[j++] = text[i];
        }
    }
    text[j] = '\0';
}

struct ModalCount
{
    Win32Window* window = nullptr;
    int calls = 0;
};

void CountModalCall(void* user)
{
    ModalCount* count = static_cast<ModalCount*>(user);
    ++count->calls;
    count->window->PumpEvents();  // returns at once inside the callback: the platform's loop is the one pumping
}

// A callback that pumps another window, as a program's frame might; records whether that pump took a message out of the
// queue.
struct OtherWindowPump
{
    Win32Window* other = nullptr;
    int calls = 0;
    bool other_saw_close = false;
};

void PumpOtherWindow(void* user)
{
    OtherWindowPump* pump = static_cast<OtherWindowPump*>(user);
    ++pump->calls;
    pump->other->PumpEvents();
    if (pump->other->ShouldClose())
    {
        pump->other_saw_close = true;
    }
}

}  // namespace

// ---- the window
// ------------------------------------------------------------------------------------------------------------------

TEST_CASE("Windows: a Win32 window opens with the client size asked for and hands out its HWND", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("w\xC3\xA4hlen \xE2\x9C\x93", 640, 360, &input));
    REQUIRE(window.Valid());
    const HWND hwnd = window.NativeHandle();
    REQUIRE(IsWindow(hwnd));
    CHECK(window.Width() == Win32Window::ScaleForDpi(640, window.Dpi()));
    CHECK(window.Height() == Win32Window::ScaleForDpi(360, window.Dpi()));
    CHECK(ClientWidth(hwnd) == Win32Window::ScaleForDpi(640, window.Dpi()));
    CHECK(ClientHeight(hwnd) == Win32Window::ScaleForDpi(360, window.Dpi()));
    CHECK(window.Dpi() == GetDpiForWindow(hwnd));
    CHECK(window.Dpi() >= 96);
    CHECK(StringView(window.BackendName()) == StringView("win32"));
    CHECK_FALSE(window.IsMinimized());
    CHECK(window.ResizeSerial() != 0);
    CHECK_FALSE(window.ShouldClose());
    const os::NativeWindowInfo info = window.GetNativeInfo();
    CHECK(info.kind == os::NativeWindowKind::Win32);
    CHECK(info.surface == hwnd);
    CHECK(info.display == reinterpret_cast<void*>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)));
    wchar_t title[64] = {};
    GetWindowTextW(hwnd, title, 64);
    CHECK(WStrCompare(title, L"w\x00E4hlen \x2713") == 0);
    // The window is per-monitor-v2 aware, whatever the thread was.
    CHECK(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(hwnd), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    window.Close();
    CHECK_FALSE(window.Valid());
    CHECK_FALSE(IsWindow(hwnd));
    CHECK(window.GetNativeInfo().kind == os::NativeWindowKind::Invalid);
    CHECK(window.ResizeSerial() == 0);
    CHECK(window.IsMinimized());
    REQUIRE(window.Open("again", 320, 240, &input));  // a closed window opens again
    CHECK(window.Width() == Win32Window::ScaleForDpi(320, window.Dpi()));
}

TEST_CASE("Windows: Open refuses a zero size and a second Open", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window zero;
    CHECK_FALSE(zero.Open("zero", 0, 240, &input));
    CHECK_FALSE(zero.Valid());
    Win32Window window;
    REQUIRE(window.Open("once", 320, 240, &input));
    const HWND first = window.NativeHandle();
    CHECK_FALSE(window.Open("twice", 320, 240, &input));
    CHECK(window.NativeHandle() == first);
}

TEST_CASE("Windows: a programmatic resize changes the size and the serial at once, and the same size changes nothing",
          "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("resize", 320, 240, &input));
    const u64 before = window.ResizeSerial();
    ResizeClient(window.NativeHandle(), 500, 300);  // WM_SIZE is sent during SetWindowPos, as during a drag
    CHECK(window.Width() == 500);
    CHECK(window.Height() == 300);
    const u64 after = window.ResizeSerial();
    CHECK(after != before);
    ResizeClient(window.NativeHandle(), 500, 300);
    window.PumpEvents();
    CHECK(window.ResizeSerial() == after);
}

TEST_CASE("Windows: minimizing keeps the size and reports minimized; restoring changes the serial", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("minimize", 400, 300, &input));
    const u64 before = window.ResizeSerial();
    ShowWindow(window.NativeHandle(), SW_MINIMIZE);
    REQUIRE(PumpUntil(window, [&] { return window.IsMinimized(); }));
    CHECK(window.Width() == Win32Window::ScaleForDpi(400, window.Dpi()));
    CHECK(window.Height() == Win32Window::ScaleForDpi(300, window.Dpi()));
    ShowWindow(window.NativeHandle(), SW_RESTORE);
    REQUIRE(PumpUntil(window, [&] { return !window.IsMinimized(); }));
    CHECK(window.ResizeSerial() != before);
    CHECK(window.Width() == Win32Window::ScaleForDpi(400, window.Dpi()));
    CHECK(window.Height() == Win32Window::ScaleForDpi(300, window.Dpi()));
}

TEST_CASE("Windows: the close button asks to close and leaves the window open", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("close", 320, 240, &input));
    PostMessageW(window.NativeHandle(), WM_CLOSE, 0, 0);
    CHECK_FALSE(window.ShouldClose());  // nothing happens until the owner pumps
    window.PumpEvents();
    CHECK(window.ShouldClose());
    CHECK(window.Valid());
    CHECK(IsWindow(window.NativeHandle()));
}

TEST_CASE("Windows: WM_QUIT counts as a close request", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("quit", 320, 240, &input));
    PostQuitMessage(0);
    window.PumpEvents();
    CHECK(window.ShouldClose());
}

TEST_CASE("Windows: two windows on one thread close separately, whichever one pumps", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window a;
    Win32Window b;
    REQUIRE(a.Open("a", 320, 240, &input));
    REQUIRE(b.Open("b", 300, 200, &input));
    CHECK(a.NativeHandle() != b.NativeHandle());
    PostMessageW(b.NativeHandle(), WM_CLOSE, 0, 0);
    a.PumpEvents();  // dispatches b's message to b
    CHECK_FALSE(a.ShouldClose());
    CHECK(b.ShouldClose());
    CHECK(a.Width() == Win32Window::ScaleForDpi(320, a.Dpi()));
    CHECK(b.Width() == Win32Window::ScaleForDpi(300, b.Dpi()));
}

TEST_CASE("Windows: a window destroyed with messages still queued is never reached by them", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window survivor;
    REQUIRE(survivor.Open("survivor", 320, 240, &input));
    HWND gone_hwnd = nullptr;
    {
        Win32Window gone;
        REQUIRE(gone.Open("gone", 320, 240, &input));
        gone_hwnd = gone.NativeHandle();
        PostMessageW(gone_hwnd, WM_CLOSE, 0, 0);
        PostMessageW(gone_hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(10, 10));
        PostMessageW(gone_hwnd, WM_TIMER, 1, 0);
        PostMessageW(gone_hwnd, WM_USER + 7, 0, 0);
    }  // destroyed here, before anyone pumps
    CHECK_FALSE(IsWindow(gone_hwnd));
    survivor.PumpEvents();  // drains what is left (ASan reports any use of the destroyed object)
    CHECK_FALSE(survivor.ShouldClose());
    CHECK(survivor.Width() == Win32Window::ScaleForDpi(320, survivor.Dpi()));
}

TEST_CASE("Windows: a window's size is logical, scaled by its DPI", "[window][win32]")
{
    // The user saw tiny windows on a high-DPI monitor (2026-11-08): the size was taken as physical pixels.
    CHECK(Win32Window::ScaleForDpi(640, 96) == 640);
    CHECK(Win32Window::ScaleForDpi(640, 144) == 960);
    CHECK(Win32Window::ScaleForDpi(640, 192) == 1280);
    CHECK(Win32Window::ScaleForDpi(333, 120) == 416);  // 416.25, rounded to nearest
    CHECK(Win32Window::ScaleForDpi(1, 48) == 1);       // never below 1
    CHECK(Win32Window::ScaleForDpi(640, 0) == 640);    // 0 counts as 96
    CHECK(Win32Window::ScaleForDpi(100000, 192) == 0x7FFF);
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("logical", 400, 300, &input));
    const u32 dpi = window.Dpi();
    INFO("window DPI " << dpi);
    CHECK(window.Width() == ClientWidth(window.NativeHandle()));
    CHECK(window.Width() == Win32Window::ScaleForDpi(400, dpi));
    CHECK(window.Height() == Win32Window::ScaleForDpi(300, dpi));
}

TEST_CASE("Windows: a DPI change applies the suggested rectangle and reports the new DPI", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("dpi", 400, 300, &input));
    const HWND hwnd = window.NativeHandle();
    const u32 dpi = window.Dpi();
    const u64 before = window.ResizeSerial();
    // As Windows sends it when the window moves to a monitor of twice the scale: the new DPI and a rectangle twice the
    // size.
    RECT current{};
    GetWindowRect(hwnd, &current);
    RECT suggested{current.left, current.top, current.left + 2 * (current.right - current.left),
                   current.top + 2 * (current.bottom - current.top)};
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi * 2, dpi * 2), reinterpret_cast<LPARAM>(&suggested));
    CHECK(window.Dpi() == dpi * 2);
    RECT now{};
    GetWindowRect(hwnd, &now);
    CHECK(now.right - now.left == suggested.right - suggested.left);
    CHECK(now.bottom - now.top == suggested.bottom - suggested.top);
    CHECK(window.Width() == ClientWidth(hwnd));
    CHECK(window.Height() == ClientHeight(hwnd));
    CHECK(window.ResizeSerial() != before);
}

TEST_CASE("Windows: the modal-loop callback runs during a move or resize loop and stops after it", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("modal", 320, 240, &input));
    ModalCount count;
    count.window = &window;
    window.SetModalLoopCallback(&CountModalCall, &count);
    window.PumpEvents();
    os::SleepNanos(50'000'000);
    window.PumpEvents();
    CHECK(count.calls == 0);  // no loop, no calls

    // What DefWindowProc's own loop does: WM_ENTERSIZEMOVE, then it dispatches messages (the timer's among them) until
    // the drag ends.
    SendMessageW(window.NativeHandle(), WM_ENTERSIZEMOVE, 0, 0);
    REQUIRE(PumpUntil(window, [&] { return count.calls >= 3; }));
    SendMessageW(window.NativeHandle(), WM_EXITSIZEMOVE, 0, 0);
    window.PumpEvents();
    const int after_exit = count.calls;
    os::SleepNanos(100'000'000);
    window.PumpEvents();
    CHECK(count.calls == after_exit);

    // Set during a loop, it starts at once; cleared, it stops.
    SendMessageW(window.NativeHandle(), WM_ENTERMENULOOP, 0, 0);
    window.SetModalLoopCallback(nullptr, nullptr);
    os::SleepNanos(50'000'000);
    window.PumpEvents();
    CHECK(count.calls == after_exit);
    window.SetModalLoopCallback(&CountModalCall, &count);
    REQUIRE(PumpUntil(window, [&] { return count.calls > after_exit; }));
    SendMessageW(window.NativeHandle(), WM_EXITMENULOOP, 0, 0);
}

TEST_CASE("Windows: a modal-loop callback that pumps another window takes no message from the platform's loop",
          "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window dragged;
    Win32Window other;
    REQUIRE(dragged.Open("dragged", 320, 240, &input));
    REQUIRE(other.Open("other", 320, 240, &input));
    OtherWindowPump pump;
    pump.other = &other;
    dragged.SetModalLoopCallback(&PumpOtherWindow, &pump);
    SendMessageW(dragged.NativeHandle(), WM_ENTERSIZEMOVE, 0, 0);
    PostMessageW(other.NativeHandle(), WM_CLOSE, 0, 0);  // stands for the mouse release the platform's loop waits for
    // The platform's loop dispatches the timer; a message posted behind it stays queued for that loop, not for the
    // callback's pump.
    MSG msg;
    for (int i = 0; i < 100 && pump.calls < 3; ++i)
    {
        if (PeekMessageW(&msg, nullptr, WM_TIMER, WM_TIMER, PM_REMOVE))
        {
            DispatchMessageW(&msg);
        }
        os::SleepNanos(5'000'000);
    }
    REQUIRE(pump.calls >= 3);
    CHECK_FALSE(pump.other_saw_close);
    SendMessageW(dragged.NativeHandle(), WM_EXITSIZEMOVE, 0, 0);
    other.PumpEvents();  // after the loop the message is delivered as usual
    CHECK(other.ShouldClose());
}

TEST_CASE("Windows: a window of the engine's class made without Open gets the default procedure", "[window][win32]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("registers the class", 320, 240, &input));
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window.NativeHandle(), GWLP_HINSTANCE));
    // No object in lpCreateParams: WM_NCCREATE must not take the null for one.
    const HWND foreign = CreateWindowExW(0, L"EngineWindow", L"foreign", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                         CW_USEDEFAULT, 200, 100, nullptr, nullptr, instance, nullptr);
    REQUIRE(foreign != nullptr);
    SendMessageW(foreign, WM_SIZE, SIZE_RESTORED, MAKELPARAM(50, 50));
    SendMessageW(foreign, WM_CLOSE, 0, 0);  // the default procedure destroys it
    CHECK_FALSE(IsWindow(foreign));
    window.PumpEvents();
    CHECK_FALSE(window.ShouldClose());
}

TEST_CASE("Windows: fullscreen asked for while minimized restores the window and covers the monitor",
          "[window][win32][fullscreen]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    Win32Window window;
    REQUIRE(window.Open("minimized fullscreen", 320, 240, &input));
    ShowWindow(window.NativeHandle(), SW_MINIMIZE);
    REQUIRE(PumpUntil(window, [&] { return window.IsMinimized(); }));
    REQUIRE(window.SetFullscreen(true));
    window.PumpEvents();
    CHECK_FALSE(window.IsMinimized());
    CHECK(window.IsFullscreen());
    REQUIRE(window.SetFullscreen(false));
    window.PumpEvents();
    CHECK_FALSE(window.IsFullscreen());
}

// ---- the windowed programs' entry point
// -------------------------------------------------------------------------------------------

TEST_CASE("Windows: a program written with ENGINE_WINDOWED_PROGRAM is a GUI program, and a test program a console one",
          "[win32][entry_point]")
{
    CHECK(Subsystem(io::Path(WINDOWED_PROBE_PATH_UTF8)) == 2);  // IMAGE_SUBSYSTEM_WINDOWS_GUI
    io::Path self;
    REQUIRE(io::ExecutablePath(&self).Ok());
    CHECK(Subsystem(self) == 3);  // IMAGE_SUBSYSTEM_WINDOWS_CUI
}

TEST_CASE(
    "Windows: a windowed program gets UTF-8 arguments split by the Windows rules, and its output reaches the parent's pipes",
    "[win32][entry_point]")
{
    // A quoted argument keeps its spaces; 2n backslashes before a quote give n backslashes; 2n+1 give n and a literal
    // quote; backslashes not before a quote are literal.
    ProbeResult r = RunWindowedProbe(
        L"\"h\x00E9llo w\x00F6rld\" a\\\\\\\"b \"C:\\path with space\\\\\" \x2713\xD83D\xDE00 plain");
    CHECK(r.exit_code == 7);
    const char* expected[] = {"h\xC3\xA9llo w\xC3\xB6rld", "a\\\"b", "C:\\path with space\\",
                              "\xE2\x9C\x93\xF0\x9F\x98\x80", "plain"};
    char want[4096] = {};
    for (int i = 0; i < 5; ++i)
    {
        AppendHexLine(want, sizeof(want), "arg", i, expected[i]);
    }
    for (int i = 0; i < 5; ++i)
    {
        AppendHexLine(want, sizeof(want), "stash", i, expected[i]);
    }
    StripCarriageReturns(r.out);  // the C runtime may end lines with CR LF on a text-mode standard output
    CHECK(CStrCompare(r.out, want) == 0);
    CHECK(CStrCompare(r.err, "stderr ok\n") == 0);
}
