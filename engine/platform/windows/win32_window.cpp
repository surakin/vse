// The Win32 window backend (Phase 13 Day 158): window class, window procedure, size, DPI, fullscreen and the modal-loop callback.
#include "platform/windows/win32_window.h"

#include <cstring>
#include <mutex>

#include "containers/array.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"
#include "string/string.h"

namespace engine_placeholder::os::win32 {

namespace {

constexpr const wchar_t* kWin32ClassName = L"EngineWindow";
constexpr UINT_PTR kWin32ModalTimer = 1; // the timer that calls the modal-loop callback
constexpr DWORD kWin32Style = WS_OVERLAPPEDWINDOW;
constexpr DWORD kWin32ExStyle = WS_EX_APPWINDOW;

static_assert(sizeof(WINDOWPLACEMENT) == 44, "Win32Window::saved_placement_ holds a WINDOWPLACEMENT");

// Set while a modal-loop callback runs on this thread, whichever window's it is: a pump from inside it, of any window, would take the
// platform loop's own messages (the mouse release that ends a drag) out of the queue.
thread_local bool t_in_modal_callback = false;

void LogWin32Failure(const char* what) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("Window: ").Append(what).Append(" failed (error ").AppendUInt(GetLastError()).Append(')');
    Log(LogPlatform, LogVerbosity::Error, line.View());
}

// The module that holds this code: the window class belongs to it, and it is the `HINSTANCE` a graphics surface is made with.
HINSTANCE Win32WindowModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Win32WindowModule), &module);
    return module;
}

bool SameWin32Rect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

} // namespace

struct Win32Window::Proc {
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT Handle(Win32Window& self, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static bool RegisterClassOnce(HINSTANCE instance);
};

bool Win32Window::Proc::RegisterClassOnce(HINSTANCE instance) {
    static std::once_flag once;
    static bool registered = false;
    std::call_once(once, [instance] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW; // no CS_OWNDC: the swapchain draws, not GDI
        wc.lpfnWndProc = &Proc::WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW (the macro is the A/W-switched form)
        wc.lpszClassName = kWin32ClassName;
        // No background brush: the window is drawn by the swapchain, and erasing first would flicker.
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) LogWin32Failure("RegisterClassExW");
    });
    return registered;
}

// The window procedure. Messages sent before WM_NCCREATE (WM_GETMINMAXINFO comes first) and after WM_NCDESTROY have no object and go to
// the default procedure; WM_NCCREATE stores the object in the window's user data, WM_NCDESTROY removes it.
LRESULT CALLBACK Win32Window::Proc::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        Win32Window* self = static_cast<Win32Window*>(create->lpCreateParams);
        if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam); // a window of this class not made by Open
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd; // so messages sent during CreateWindowExW (WM_SIZE, WM_DPICHANGED) see an open window
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    Win32Window* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return Handle(*self, hwnd, message, wparam, lparam);
}

LRESULT Win32Window::Proc::Handle(Win32Window& self, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLOSE:
        self.should_close_ = true; // the owner closes the window; DefWindowProc would destroy it now
        return 0;
    case WM_SIZE: {
        if (wparam == SIZE_MINIMIZED) {
            self.minimized_ = true; // the size kept is the last one before minimizing
            return 0;
        }
        const u32 width = LOWORD(lparam);
        const u32 height = HIWORD(lparam);
        const bool left_minimized = self.minimized_;
        self.minimized_ = false;
        if (left_minimized || width != self.width_ || height != self.height_) {
            self.width_ = width;
            self.height_ = height;
            self.resize_serial_ = detail::NextResizeSerial();
        }
        self.UpdateFullscreenState();
        return 0;
    }
    case WM_DPICHANGED: {
        self.dpi_ = HIWORD(wparam);
        if (self.fullscreen_wanted_) {
            self.SetFullscreen(true); // fill the new monitor instead of taking the suggested rectangle
        } else {
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
    case WM_ENTERMENULOOP:
        self.in_modal_loop_ = true;
        if (self.modal_callback_ != nullptr) SetTimer(hwnd, kWin32ModalTimer, USER_TIMER_MINIMUM, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
    case WM_EXITMENULOOP:
        self.in_modal_loop_ = false;
        KillTimer(hwnd, kWin32ModalTimer);
        return 0;
    case WM_TIMER:
        if (wparam != kWin32ModalTimer) break;
        if (self.modal_callback_ != nullptr && !t_in_modal_callback) {
            t_in_modal_callback = true;
            self.modal_callback_(self.modal_user_);
            t_in_modal_callback = false;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

Win32Window::~Win32Window() { Close(); }

bool Win32Window::Open(StringView title, u32 width, u32 height, InputState* input) {
    if (hwnd_ != nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Window: Win32Window::Open on an open window");
        return false;
    }
    if (width == 0 || height == 0) {
        Log(LogPlatform, LogVerbosity::Error, "Window: Win32Window::Open with a zero size");
        return false;
    }
    instance_ = Win32WindowModule();
    if (!Proc::RegisterClassOnce(instance_)) return false;

    Array<wchar_t, 512> wide_title;
    windows::Utf8ToUtf16(title, wide_title.Data(), wide_title.Size());

    // Every engine program is per-monitor-v2 aware from its manifest. A thread that is not (a host process without the manifest) gets it
    // for the creation, which fixes the window's awareness for its life.
    const DPI_AWARENESS_CONTEXT previous = GetThreadDpiAwarenessContext();
    const bool switch_awareness = AreDpiAwarenessContextsEqual(previous, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE;
    if (switch_awareness) SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // The outer size for the system DPI first (the monitor is not known before the window exists), then corrected below.
    RECT outer{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectExForDpi(&outer, kWin32Style, FALSE, kWin32ExStyle, GetDpiForSystem());
    input_ = input;
    should_close_ = false;
    minimized_ = false;
    fullscreen_wanted_ = false;
    fullscreen_ = false;
    width_ = 0;
    height_ = 0;
    const HWND hwnd = CreateWindowExW(kWin32ExStyle, kWin32ClassName, wide_title.Data(), kWin32Style, CW_USEDEFAULT, CW_USEDEFAULT, outer.right - outer.left,
                                      outer.bottom - outer.top, nullptr, nullptr, instance_, this);
    if (switch_awareness) SetThreadDpiAwarenessContext(previous);
    if (hwnd == nullptr) {
        LogWin32Failure("CreateWindowExW");
        hwnd_ = nullptr;
        return false;
    }
    hwnd_ = hwnd;
    dpi_ = GetDpiForWindow(hwnd);
    ApplyClientSize(width, height);
    RECT client{};
    GetClientRect(hwnd, &client);
    width_ = static_cast<u32>(client.right - client.left);
    height_ = static_cast<u32>(client.bottom - client.top);
    resize_serial_ = detail::NextResizeSerial();
    ShowWindow(hwnd, SW_SHOWDEFAULT); // the launcher's choice (a shortcut set to "minimized"), else shown normally
    return true;
}

// Sets the outer size so the client area is `width` x `height` at the window's own DPI.
void Win32Window::ApplyClientSize(u32 width, u32 height) {
    RECT outer{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectExForDpi(&outer, kWin32Style, FALSE, kWin32ExStyle, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32Window::Close() {
    if (hwnd_ == nullptr) return;
    KillTimer(hwnd_, kWin32ModalTimer);
    // The window forgets this object first, so nothing DestroyWindow sends (WM_DESTROY, WM_NCDESTROY), and no message still queued for the
    // window, can reach it; the window then has no object and gets the default procedure.
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    in_modal_loop_ = false;
}

void Win32Window::PumpEvents() {
    if (t_in_modal_callback) return; // inside a modal-loop callback (of any window): the platform's loop is pumping
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            should_close_ = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

NativeWindowInfo Win32Window::GetNativeInfo() const {
    NativeWindowInfo info;
    if (hwnd_ != nullptr) {
        info.kind = NativeWindowKind::Win32;
        info.display = instance_;
        info.surface = hwnd_;
    }
    return info;
}

bool Win32Window::SetFullscreen(bool fullscreen) {
    if (hwnd_ == nullptr) return false;
    if (fullscreen) {
        // A minimized window is restored first: its placement would bring it back minimized, and an iconic window has no client area to
        // cover the monitor with.
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
        if (!fullscreen_wanted_) {
            saved_style_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            GetWindowPlacement(hwnd_, &placement);
            std::memcpy(saved_placement_.Data(), &placement, sizeof(placement));
            SetWindowLongPtrW(hwnd_, GWL_STYLE, saved_style_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
            fullscreen_wanted_ = true;
        }
        const RECT& r = monitor.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        UpdateFullscreenState(); // a window that already had the monitor's size gets no WM_SIZE
        return true;
    }
    if (!fullscreen_wanted_) return true;
    fullscreen_wanted_ = false;
    fullscreen_ = false;
    WINDOWPLACEMENT placement{};
    std::memcpy(&placement, saved_placement_.Data(), sizeof(placement));
    SetWindowLongPtrW(hwnd_, GWL_STYLE, saved_style_);
    SetWindowPlacement(hwnd_, &placement);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    return true;
}

// Fullscreen once it was asked for and the client area covers the window's monitor.
void Win32Window::UpdateFullscreenState() {
    if (!fullscreen_wanted_ || hwnd_ == nullptr) {
        fullscreen_ = false;
        return;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    RECT client{};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor) || !GetClientRect(hwnd_, &client)) {
        fullscreen_ = false;
        return;
    }
    POINT origin{0, 0};
    ClientToScreen(hwnd_, &origin);
    const RECT on_screen{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    fullscreen_ = SameWin32Rect(on_screen, monitor.rcMonitor);
}

void Win32Window::SetModalLoopCallback(ModalLoopCallback callback, void* user) {
    modal_callback_ = callback;
    modal_user_ = user;
    if (hwnd_ == nullptr || !in_modal_loop_) return;
    if (callback != nullptr) {
        SetTimer(hwnd_, kWin32ModalTimer, USER_TIMER_MINIMUM, nullptr);
    } else {
        KillTimer(hwnd_, kWin32ModalTimer);
    }
}

} // namespace engine_placeholder::os::win32
