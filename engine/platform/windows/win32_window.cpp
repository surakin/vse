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
    static void KeyMessage(Win32Window& self, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static u32 LayoutCharacter(const Win32Window& self, u32 virtual_key, u32 scan_code);
    static void UpdateModifiers(Win32Window& self);
    static void MouseMove(Win32Window& self, HWND hwnd, LPARAM lparam);
    static void MouseButton(Win32Window& self, HWND hwnd, u32 bit, bool pressed, LPARAM lparam);
    static void ReleaseInput(Win32Window& self, HWND hwnd);
};

namespace {

// The wheel in the units the Linux backends use: Wayland reports about 10 per notch, and X11's wheel buttons are given 10 each.
constexpr f64 kWin32ScrollUnitsPerNotch = 10.0;

i32 SignedLow(LPARAM lparam) { return static_cast<i16>(LOWORD(static_cast<DWORD_PTR>(lparam))); }
i32 SignedHigh(LPARAM lparam) { return static_cast<i16>(HIWORD(static_cast<DWORD_PTR>(lparam))); }

} // namespace

// The character the active layout gives a key with the modifiers that choose a level: Shift, Caps Lock and AltGr, not Ctrl or Alt (so
// Ctrl+A is still `A`, as XKB names it). Flag 4 leaves the keyboard state alone, so a dead key still composes in the system's WM_CHAR.
u32 Win32Window::Proc::LayoutCharacter(const Win32Window& self, u32 virtual_key, u32 scan_code) {
    Array<BYTE, 256> state = {};
    const KeyboardState& keyboard = self.input_->keyboard;
    if (keyboard.IsDown(Key::LeftShift) || keyboard.IsDown(Key::RightShift)) state[VK_SHIFT] = 0x80;
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) state[VK_CAPITAL] = 0x01;
    if (self.altgr_ctrl_held_) {
        state[VK_CONTROL] = 0x80;
        state[VK_LCONTROL] = 0x80;
        state[VK_MENU] = 0x80;
        state[VK_RMENU] = 0x80;
    }
    Array<wchar_t, 8> text = {};
    const int n = ToUnicodeEx(virtual_key, scan_code, state.Data(), text.Data(), static_cast<int>(text.Size()), 0x4, GetKeyboardLayout(0));
    if (n == 1) return text[0];
    if (n == 2 && text[0] >= 0xD800 && text[0] < 0xDC00 && text[1] >= 0xDC00 && text[1] < 0xE000) {
        return 0x10000u + ((static_cast<u32>(text[0]) - 0xD800u) << 10) + (static_cast<u32>(text[1]) - 0xDC00u);
    }
    return 0; // nothing, a dead key (negative) or more than one character
}

// The modifier mask, from the keys this window saw held, as the Linux backends report it from their events: AltGr is not Alt (XKB's
// level-3 shift is not Mod1); the locks are the system's toggle state.
void Win32Window::Proc::UpdateModifiers(Win32Window& self) {
    KeyboardState& keyboard = self.input_->keyboard;
    u32 mods = 0;
    if (keyboard.IsDown(Key::LeftShift) || keyboard.IsDown(Key::RightShift)) mods |= kModShift;
    if (keyboard.IsDown(Key::LeftCtrl) || keyboard.IsDown(Key::RightCtrl)) mods |= kModCtrl;
    if (keyboard.IsDown(Key::LeftAlt) || (keyboard.IsDown(Key::RightAlt) && !self.altgr_ctrl_held_)) mods |= kModAlt;
    if (keyboard.IsDown(Key::LeftSuper) || keyboard.IsDown(Key::RightSuper)) mods |= kModSuper;
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) mods |= kModCapsLock;
    if ((GetKeyState(VK_NUMLOCK) & 1) != 0) mods |= kModNumLock;
    keyboard.modifiers = mods;
}

void Win32Window::Proc::KeyMessage(Win32Window& self, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (self.input_ == nullptr) return;
    const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const u32 vk = static_cast<u32>(wparam);
    u32 scan = static_cast<u32>(lparam >> 16) & 0xFFu;
    bool extended = ((lparam >> 24) & 1) != 0;
    if (scan == 0) {
        // Injected input (a remote session, an automation tool) can carry the virtual key alone; without a scan code every such key would
        // share one slot of held_, and one key's release would clear another's. The layout's scan code for the key, with its 0xE0 prefix
        // as the extended flag.
        const UINT mapped = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        scan = mapped & 0xFFu;
        extended = extended || (mapped >> 8) == 0xE0u || (mapped >> 8) == 0xE1u;
    }
    // AltGr: Windows sends a left Ctrl (VK_CONTROL without the extended flag) just before the right Alt, with the same time. The Linux
    // backends report AltGr as the right Alt alone, so that Ctrl is not a key here.
    if (vk == VK_CONTROL && !extended) {
        if (pressed) {
            const DWORD time = static_cast<DWORD>(GetMessageTime()); // this message's: peeking at the next one makes that one "the last"
            MSG next;
            if (PeekMessageW(&next, hwnd, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE) && (next.message == WM_KEYDOWN || next.message == WM_SYSKEYDOWN) &&
                next.wParam == VK_MENU && ((next.lParam >> 24) & 1) != 0 && next.time == time) {
                self.altgr_ctrl_held_ = true;
                return;
            }
        } else if (self.altgr_ctrl_held_) {
            self.altgr_ctrl_held_ = false;
            UpdateModifiers(self);
            return;
        }
    }
    KeyboardState& keyboard = self.input_->keyboard;
    const u32 index = Win32KeyIndex(scan, extended);
    if (pressed) {
        const bool repeat = ((lparam >> 30) & 1) != 0;
        if (!repeat || self.held_[index] == Key::Unknown) {
            const Key key = KeyFromWin32(Win32KeyMessage{vk, scan, extended, LayoutCharacter(self, vk, scan)});
            self.held_[index] = key;
            keyboard.SetDown(key, true);
        }
    } else {
        Key key = self.held_[index];
        if (key == Key::Unknown) key = KeyFromWin32(Win32KeyMessage{vk, scan, extended, LayoutCharacter(self, vk, scan)}); // pressed before focus
        keyboard.SetDown(key, false);
        self.held_[index] = Key::Unknown;
        if (vk == VK_SHIFT) {
            // With both Shift keys held, Windows sends no key-up for the first one released, only for the last: a Shift release ends both.
            keyboard.SetDown(Key::LeftShift, false);
            keyboard.SetDown(Key::RightShift, false);
            self.held_[Win32KeyIndex(0x2A, false)] = Key::Unknown;
            self.held_[Win32KeyIndex(0x36, false)] = Key::Unknown;
        }
    }
    UpdateModifiers(self);
}

void Win32Window::Proc::MouseMove(Win32Window& self, HWND hwnd, LPARAM lparam) {
    if (self.input_ == nullptr) return;
    InputState& input = *self.input_;
    const i32 x = SignedLow(lparam);
    const i32 y = SignedHigh(lparam);
    input.mouse_x = x; // client coordinates, physical pixels; outside the client area (negative) while a button holds the capture
    input.mouse_y = y;
    RECT client{};
    GetClientRect(hwnd, &client);
    input.pointer_inside = x >= 0 && y >= 0 && x < client.right && y < client.bottom;
    if (input.pointer_inside && !self.tracking_leave_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        self.tracking_leave_ = TrackMouseEvent(&track) != FALSE;
    }
}

// A press captures the mouse (SetCapture) so the release arrives even outside the window, as X11's implicit grab and Wayland deliver it.
void Win32Window::Proc::MouseButton(Win32Window& self, HWND hwnd, u32 bit, bool pressed, LPARAM lparam) {
    if (self.input_ == nullptr) return;
    InputState& input = *self.input_;
    input.mouse_x = SignedLow(lparam);
    input.mouse_y = SignedHigh(lparam);
    if (pressed) {
        if (input.mouse_buttons == 0) SetCapture(hwnd);
        input.mouse_buttons |= bit;
    } else {
        input.mouse_buttons &= ~bit;
        if (input.mouse_buttons == 0 && GetCapture() == hwnd) ReleaseCapture();
    }
}

// Focus lost: the releases of what is held will go to another window, so everything held is released now (keys, buttons, modifiers).
// Windows sends WM_KILLFOCUS only when the keyboard focus really leaves (another window, Alt+Tab, a click elsewhere), not for the window's
// own menus, which is the line the X11 backend draws with its grab modes.
void Win32Window::Proc::ReleaseInput(Win32Window& self, HWND hwnd) {
    for (Key& key : self.held_) key = Key::Unknown;
    self.altgr_ctrl_held_ = false;
    if (self.input_ != nullptr) {
        self.input_->keyboard.ReleaseAll();
        self.input_->keyboard.modifiers = 0;
        self.input_->mouse_buttons = 0;
    }
    if (GetCapture() == hwnd) ReleaseCapture();
}

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
    case WM_KEYDOWN:
    case WM_KEYUP:
        Proc::KeyMessage(self, hwnd, message, wparam, lparam);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        Proc::KeyMessage(self, hwnd, message, wparam, lparam);
        break; // the default procedure turns Alt+F4 into WM_CLOSE and Alt+Space into the system menu
    case WM_SYSCHAR:
        if (wparam == ' ') break; // Alt+Space: the system menu
        return 0;                 // Alt+letter: no menu to open, and no beep
    case WM_SYSCOMMAND:
        // Alt or F10 alone would put the window into menu mode (the next keys go to a menu bar it does not have); Alt+Space still opens the
        // system menu (lParam is the space), and the other commands (close, minimize, move) go on.
        if ((wparam & 0xFFF0) == SC_KEYMENU && lparam == 0) return 0;
        break;
    case WM_KILLFOCUS:
        Proc::ReleaseInput(self, hwnd);
        break;
    case WM_CAPTURECHANGED:
        // Another window took the mouse while a button was held: its release will not come here.
        if (reinterpret_cast<HWND>(lparam) != hwnd && self.input_ != nullptr) self.input_->mouse_buttons = 0;
        break;
    case WM_MOUSEMOVE:
        Proc::MouseMove(self, hwnd, lparam);
        return 0;
    case WM_MOUSELEAVE:
        self.tracking_leave_ = false;
        if (self.input_ != nullptr) self.input_->pointer_inside = false; // held buttons stay held, as on Linux
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        Proc::MouseButton(self, hwnd, kMouseLeft, message == WM_LBUTTONDOWN, lparam);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        Proc::MouseButton(self, hwnd, kMouseRight, message == WM_RBUTTONDOWN, lparam);
        return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        Proc::MouseButton(self, hwnd, kMouseMiddle, message == WM_MBUTTONDOWN, lparam);
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: {
        const u32 bit = HIWORD(wparam) == XBUTTON1 ? kMouseBack : kMouseForward;
        Proc::MouseButton(self, hwnd, bit, message == WM_XBUTTONDOWN, lparam);
        return TRUE; // the X buttons' messages want TRUE
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (self.input_ != nullptr) {
            // Positive is the wheel turned away from the user (up) or tilted right; InputState's positive scroll is down and right.
            // A high-resolution wheel sends fractions of WHEEL_DELTA, which add up.
            const f64 notches = static_cast<f64>(static_cast<i16>(HIWORD(wparam))) / WHEEL_DELTA;
            if (message == WM_MOUSEWHEEL) {
                self.input_->scroll_dy -= notches * kWin32ScrollUnitsPerNotch;
            } else {
                self.input_->scroll_dx += notches * kWin32ScrollUnitsPerNotch;
            }
        }
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

u32 Win32Window::ScaleForDpi(u32 logical, u32 dpi) {
    const u64 d = dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
    const u64 scaled = (static_cast<u64>(logical) * d + USER_DEFAULT_SCREEN_DPI / 2) / USER_DEFAULT_SCREEN_DPI;
    return scaled == 0 ? 1 : (scaled > 0x7FFF ? 0x7FFF : static_cast<u32>(scaled)); // Win32 sizes are 16-bit in WM_SIZE
}

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

    // The outer size for the system DPI first (the monitor is not known before the window exists), then corrected below. The requested
    // size is logical: scaled by the DPI, so a window is as large on a high-DPI display as elsewhere.
    const u32 system_dpi = GetDpiForSystem();
    RECT outer{0, 0, static_cast<LONG>(ScaleForDpi(width, system_dpi)), static_cast<LONG>(ScaleForDpi(height, system_dpi))};
    AdjustWindowRectExForDpi(&outer, kWin32Style, FALSE, kWin32ExStyle, system_dpi);
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
    ApplyClientSize(ScaleForDpi(width, dpi_), ScaleForDpi(height, dpi_));
    RECT client{};
    GetClientRect(hwnd, &client);
    width_ = static_cast<u32>(client.right - client.left);
    height_ = static_cast<u32>(client.bottom - client.top);
    resize_serial_ = detail::NextResizeSerial();
    ShowWindow(hwnd, SW_SHOWDEFAULT); // the launcher's choice (a shortcut set to "minimized"), else shown normally
    return true;
}

// Sets the outer size so the client area is `width` x `height` physical pixels, the frame at the window's own DPI.
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
