// The Win32 window backend (Phase 13 Day 158): window class, window procedure, size, DPI, fullscreen and the modal-loop callback.
#include "platform/windows/win32_window.h"

#include <cstring>
#include <mutex>

#include "containers/array.h"
#include "containers/vector.h"
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
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
    static void Character(Win32Window& self, WPARAM unit);
    static void Wheel(Win32Window& self, UINT message, WPARAM wparam);
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
    // The events carry the modifiers held before the key, as X11 reports them (its state field) and the Linux backends queue them.
    InputEvent event;
    event.modifiers = keyboard.modifiers;
    if (pressed) {
        // Windows auto-repeats with more WM_KEYDOWN messages, bit 30 set ("was down"): a repeat is a press of a key already held here.
        const bool was_down = ((lparam >> 30) & 1) != 0 && self.held_[index] != Key::Unknown;
        Key key = self.held_[index];
        if (!was_down) {
            key = KeyFromWin32(Win32KeyMessage{vk, scan, extended, LayoutCharacter(self, vk, scan)});
            self.held_[index] = key;
            keyboard.SetDown(key, true);
        }
        if (key != Key::Unknown) { // an unnamed key types its text (WM_CHAR) but has no down or up: nothing could release it
            event.type = InputEventType::KeyDown;
            event.key = key;
            event.repeat = was_down;
            self.QueueEvent(event);
        }
    } else {
        Key key = self.held_[index];
        if (key == Key::Unknown) key = KeyFromWin32(Win32KeyMessage{vk, scan, extended, LayoutCharacter(self, vk, scan)}); // pressed before focus
        keyboard.SetDown(key, false);
        self.held_[index] = Key::Unknown;
        event.type = InputEventType::KeyUp;
        if (key != Key::Unknown) {
            event.key = key;
            self.QueueEvent(event);
        }
        if (vk == VK_SHIFT) {
            // With both Shift keys held, Windows sends no key-up for the first one released, only for the last: a Shift release ends both.
            for (const u32 shift_scan : {0x2Au, 0x36u}) {
                const u32 other = Win32KeyIndex(shift_scan, false);
                if (self.held_[other] != Key::Unknown && self.held_[other] != key) {
                    event.key = self.held_[other];
                    self.QueueEvent(event);
                }
                self.held_[other] = Key::Unknown;
            }
            keyboard.SetDown(Key::LeftShift, false);
            keyboard.SetDown(Key::RightShift, false);
        }
    }
    UpdateModifiers(self);
}

// WM_CHAR: one UTF-16 code unit of the text a key produced, after TranslateMessage turned the key into characters with the layout (dead keys
// are composed there: the dead key gives WM_DEADCHAR, ignored, and the next key the composed character). A character outside the BMP comes as
// two messages, the high surrogate first; they become one Text event. Control characters (Enter, Tab, Backspace, Escape, Ctrl+letter) and a
// press with Ctrl or Alt held are keys, not text, as on Linux; AltGr is neither (its Ctrl is not counted, see UpdateModifiers).
void Win32Window::Proc::Character(Win32Window& self, WPARAM unit_param) {
    const u32 unit = static_cast<u32>(unit_param) & 0xFFFFu;
    u32 scalar = unit;
    if (unit >= 0xD800 && unit < 0xDC00) {
        self.high_surrogate_ = static_cast<u16>(unit);
        return;
    }
    if (unit >= 0xDC00 && unit < 0xE000) {
        if (self.high_surrogate_ == 0) return; // a low surrogate alone: not a character
        scalar = 0x10000u + ((static_cast<u32>(self.high_surrogate_) - 0xD800u) << 10) + (unit - 0xDC00u);
    }
    self.high_surrogate_ = 0;
    const u32 modifiers = self.input_->keyboard.modifiers;
    if (scalar < 0x20 || scalar == 0x7F || (scalar >= 0x80 && scalar < 0xA0) || (modifiers & (kModCtrl | kModAlt)) != 0) return;
    InputEvent event;
    event.type = InputEventType::Text;
    event.modifiers = modifiers;
    if (scalar < 0x80) {
        event.text[0] = static_cast<char>(scalar);
        event.text_length = 1;
    } else if (scalar < 0x800) {
        event.text[0] = static_cast<char>(0xC0 | (scalar >> 6));
        event.text[1] = static_cast<char>(0x80 | (scalar & 0x3F));
        event.text_length = 2;
    } else if (scalar < 0x10000) {
        event.text[0] = static_cast<char>(0xE0 | (scalar >> 12));
        event.text[1] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        event.text[2] = static_cast<char>(0x80 | (scalar & 0x3F));
        event.text_length = 3;
    } else {
        event.text[0] = static_cast<char>(0xF0 | (scalar >> 18));
        event.text[1] = static_cast<char>(0x80 | ((scalar >> 12) & 0x3F));
        event.text[2] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        event.text[3] = static_cast<char>(0x80 | (scalar & 0x3F));
        event.text_length = 4;
    }
    self.QueueEvent(event);
}

// WM_MOUSEWHEEL and WM_MOUSEHWHEEL: Windows counts positive away from the user (up) and to the right; the engine's scroll is positive down and
// right. A high-resolution wheel sends fractions of WHEEL_DELTA, which add up. InputState keeps the Linux backends' units (10 per notch), the
// event reports notches.
void Win32Window::Proc::Wheel(Win32Window& self, UINT message, WPARAM wparam) {
    const f64 notches = static_cast<f64>(static_cast<i16>(HIWORD(wparam))) / WHEEL_DELTA;
    InputEvent event;
    event.type = InputEventType::Wheel;
    event.modifiers = self.input_->keyboard.modifiers;
    if (message == WM_MOUSEWHEEL) {
        self.input_->scroll_dy -= notches * kWin32ScrollUnitsPerNotch;
        event.y = -notches;
    } else {
        self.input_->scroll_dx += notches * kWin32ScrollUnitsPerNotch;
        event.x = notches;
    }
    self.QueueEvent(event);
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
    const bool was_inside = input.pointer_inside;
    input.pointer_inside = x >= 0 && y >= 0 && x < client.right && y < client.bottom;
    InputEvent event;
    event.modifiers = input.keyboard.modifiers;
    event.x = x;
    event.y = y;
    if (input.pointer_inside && !was_inside) {
        event.type = InputEventType::PointerEnter; // Windows has no enter message: the first move inside is the enter
        self.QueueEvent(event);
    }
    event.type = InputEventType::PointerMove;
    self.QueueEvent(event);
    if (!input.pointer_inside && was_inside) {
        event.type = InputEventType::PointerLeave; // moved out while a button holds the capture
        event.x = 0;
        event.y = 0;
        self.QueueEvent(event);
    }
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
    if (pressed == ((input.mouse_buttons & bit) != 0)) return; // no change (a second press of a held button): no event, as on Linux
    if (pressed) {
        if (input.mouse_buttons == 0) SetCapture(hwnd);
        input.mouse_buttons |= bit;
    } else {
        input.mouse_buttons &= ~bit;
        if (input.mouse_buttons == 0 && GetCapture() == hwnd) ReleaseCapture();
    }
    InputEvent event;
    event.type = InputEventType::PointerButton;
    event.modifiers = input.keyboard.modifiers;
    event.button = bit;
    event.down = pressed;
    self.QueueEvent(event);
}

// Focus lost: the releases of what is held will go to another window, so everything held is released now (keys, buttons, modifiers).
// Windows sends WM_KILLFOCUS only when the keyboard focus really leaves (another window, Alt+Tab, a click elsewhere), not for the window's
// own menus, which is the line the X11 backend draws with its grab modes.
void Win32Window::Proc::ReleaseInput(Win32Window& self, HWND hwnd) {
    // The stream gets a KeyUp for every key still held, then FocusLost, so a program tracking keys from it never sees one stuck.
    for (Key& key : self.held_) {
        if (key != Key::Unknown) {
            InputEvent up;
            up.type = InputEventType::KeyUp;
            up.key = key;
            self.QueueEvent(up);
        }
        key = Key::Unknown;
    }
    self.high_surrogate_ = 0;
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
            const bool size_changed = width != self.width_ || height != self.height_;
            self.width_ = width;
            self.height_ = height;
            self.resize_serial_ = detail::NextResizeSerial();
            if (size_changed) {
                InputEvent resize;
                resize.type = InputEventType::Resize;
                resize.width = width;
                resize.height = height;
                self.QueueEvent(resize);
            }
        }
        self.UpdateFullscreenState();
        if (self.lock_.Active()) self.ClipToCentre();
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
    case WM_SETFOCUS:
        if (!self.focused_) {
            self.focused_ = true;
            InputEvent gained;
            gained.type = InputEventType::FocusGained;
            self.QueueEvent(gained);
        }
        self.ApplyLock(self.lock_.SetFocused(true)); // a lock still wanted is asked for again
        break;
    case WM_KILLFOCUS:
        Proc::ReleaseInput(self, hwnd);
        self.ApplyLock(self.lock_.SetFocused(false));
        if (self.focused_) {
            self.focused_ = false;
            InputEvent lost;
            lost.type = InputEventType::FocusLost;
            self.QueueEvent(lost);
        }
        break;
    case WM_CLIPBOARDUPDATE:
        self.ClipboardUpdated();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) { // over the frame and the borders the system shows its own resize cursors
            self.ApplyCursor();
            return TRUE;
        }
        break;
    case WM_MOVE:
        if (self.lock_.Active()) self.ClipToCentre();
        break;
    case WM_INPUT: {
        // Raw mouse motion, registered only while the lock is held: device units, before the pointer's acceleration.
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (self.lock_.Active() &&
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0 &&
            (raw.data.mouse.lLastX != 0 || raw.data.mouse.lLastY != 0)) {
            InputEvent motion;
            motion.type = InputEventType::RelativeMotion;
            motion.modifiers = self.input_->keyboard.modifiers;
            motion.x = raw.data.mouse.lLastX;
            motion.y = raw.data.mouse.lLastY;
            self.QueueEvent(motion);
        }
        break; // the default procedure frees the input's buffer
    }
    case WM_CHAR:
        Proc::Character(self, wparam);
        return 0;
    case WM_CAPTURECHANGED:
        // Another window took the mouse while a button was held: its release will not come here.
        if (reinterpret_cast<HWND>(lparam) != hwnd && self.input_ != nullptr) self.input_->mouse_buttons = 0;
        break;
    case WM_MOUSEMOVE:
        Proc::MouseMove(self, hwnd, lparam);
        return 0;
    case WM_MOUSELEAVE:
        self.tracking_leave_ = false;
        if (self.input_ != nullptr && self.input_->pointer_inside) {
            self.input_->pointer_inside = false; // held buttons stay held, as on Linux
            InputEvent leave;
            leave.type = InputEventType::PointerLeave;
            leave.modifiers = self.input_->keyboard.modifiers;
            self.QueueEvent(leave);
        }
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
        if (self.input_ != nullptr) Proc::Wheel(self, message, wparam);
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

void Win32Window::QueueEvent(InputEvent event) {
    event.time_ns = MonotonicTimeNanos();
    events_.Push(event);
}

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
    own_input_ = InputState{};
    input_ = input != nullptr ? input : &own_input_; // the events need the key and modifier tracking either way
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
    // The clipboard: requests announce themselves in this window's stream; another program's change queues ClipboardChanged.
    clipboard_requests_.Init(&events_);
    clipboard_listener_ = AddClipboardFormatListener(hwnd) != FALSE;
    own_clipboard_sequence_ = 0;
    // The stream starts with the size the window settled on (the sizes Windows went through while it was made are not news).
    events_.Clear();
    high_surrogate_ = 0;
    focused_ = false;
    InputEvent resize;
    resize.type = InputEventType::Resize;
    resize.width = width_;
    resize.height = height_;
    QueueEvent(resize);
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
    if (clipboard_listener_) {
        RemoveClipboardFormatListener(hwnd_);
        clipboard_listener_ = false;
    }
    clipboard_requests_.FailAll(ClipboardStatus::Failed, MonotonicTimeNanos()); // none is open (Win32 answers at once); kept for the contract
    // Destroying the window releases the lock (the wish is kept, as PointerLockState says) and frees the custom cursor.
    if (lock_.Active() || lock_.Pending()) {
        ClipCursor(nullptr);
        if (raw_input_) {
            RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
            RegisterRawInputDevices(&device, 1, sizeof(device));
            raw_input_ = false;
        }
    }
    lock_.Cleared();
    lock_.SetFocused(false);
    if (image_cursor_ != nullptr) {
        ::SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))); // not showing the cursor that is destroyed next
        DestroyCursor(image_cursor_);
        image_cursor_ = nullptr;
        image_hash_ = 0;
    }
    cursor_shape_ = CursorShape::Arrow;
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

// ---- the cursor and the pointer lock (Phase 15 Day 205) ----

namespace {

// The system cursor of each shape (IDC_* resource numbers; the macros are the A/W-switched forms); Hidden has none.
const wchar_t* SystemCursorOf(CursorShape shape) {
    switch (shape) {
    case CursorShape::Arrow: return MAKEINTRESOURCEW(32512);              // IDC_ARROW
    case CursorShape::IBeam: return MAKEINTRESOURCEW(32513);              // IDC_IBEAM
    case CursorShape::Hand: return MAKEINTRESOURCEW(32649);               // IDC_HAND
    case CursorShape::ResizeHorizontal: return MAKEINTRESOURCEW(32644);   // IDC_SIZEWE
    case CursorShape::ResizeVertical: return MAKEINTRESOURCEW(32645);     // IDC_SIZENS
    case CursorShape::ResizeDiagonalNWSE: return MAKEINTRESOURCEW(32642); // IDC_SIZENWSE
    case CursorShape::ResizeDiagonalNESW: return MAKEINTRESOURCEW(32643); // IDC_SIZENESW
    case CursorShape::ResizeAll: return MAKEINTRESOURCEW(32646);          // IDC_SIZEALL
    case CursorShape::NotAllowed: return MAKEINTRESOURCEW(32648);         // IDC_NO
    case CursorShape::Hidden:
    case CursorShape::Count: break;
    }
    return nullptr;
}

// One premultiplied channel made straight (Windows draws a cursor's alpha as straight alpha).
byte Win32Straight(u32 channel, u32 alpha) {
    if (alpha == 0) return 0;
    const u32 c = channel * 255u / alpha;
    return static_cast<byte>(c > 255u ? 255u : c);
}

// Makes a cursor from premultiplied RGBA: a 32-bit top-down DIB with straight alpha and an empty mask.
HCURSOR MakeImageCursor(const CursorImage& image) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(image.width);
    header.bV5Height = -static_cast<LONG>(image.height); // top-down
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) {
        if (color != nullptr) DeleteObject(color);
        return nullptr;
    }
    byte* out = static_cast<byte*>(bits);
    const usize pixels = static_cast<usize>(image.width) * image.height;
    for (usize i = 0; i < pixels; ++i) {
        const byte* in = image.rgba + i * 4;
        const u32 a = in[3];
        out[i * 4 + 0] = Win32Straight(in[2], a); // blue
        out[i * 4 + 1] = Win32Straight(in[1], a);
        out[i * 4 + 2] = Win32Straight(in[0], a); // red
        out[i * 4 + 3] = static_cast<byte>(a);
    }
    // The mask is ignored for a 32-bit image with alpha, but CreateIconIndirect needs one: all zero.
    Vector<byte> mask_bits;
    const usize stride = ((static_cast<usize>(image.width) + 15) / 16) * 2; // monochrome rows are WORD aligned
    mask_bits.Resize(stride * image.height);
    for (usize i = 0; i < mask_bits.Size(); ++i) mask_bits[i] = 0;
    HBITMAP mask = CreateBitmap(static_cast<int>(image.width), static_cast<int>(image.height), 1, 1, mask_bits.Data());
    ICONINFO info{};
    info.fIcon = FALSE; // a cursor, with a hotspot
    info.xHotspot = static_cast<DWORD>(image.hotspot_x);
    info.yHotspot = static_cast<DWORD>(image.hotspot_y);
    info.hbmMask = mask;
    info.hbmColor = color;
    HCURSOR cursor = mask != nullptr ? CreateIconIndirect(&info) : nullptr;
    if (mask != nullptr) DeleteObject(mask);
    DeleteObject(color); // CreateIconIndirect copies both bitmaps
    return cursor;
}

} // namespace

void Win32Window::ApplyCursor() {
    if (lock_.Active() || (image_cursor_ == nullptr && cursor_shape_ == CursorShape::Hidden)) {
        ::SetCursor(nullptr);
    } else if (image_cursor_ != nullptr) {
        ::SetCursor(image_cursor_);
    } else {
        ::SetCursor(LoadCursorW(nullptr, SystemCursorOf(cursor_shape_)));
    }
}

bool Win32Window::SetCursor(CursorShape shape) {
    if (hwnd_ == nullptr || static_cast<u32>(shape) >= static_cast<u32>(CursorShape::Count)) return false;
    cursor_shape_ = shape;
    if (input_->pointer_inside) ApplyCursor(); // else WM_SETCURSOR applies it when the pointer comes in
    return true;
}

bool Win32Window::SetCursorImage(const CursorImage& image) {
    if (hwnd_ == nullptr || ValidateCursorImage(image) != CursorImageError::Ok) return false;
    const u64 hash = HashCursorImage(image);
    if (image_cursor_ != nullptr && hash == image_hash_) return true; // the image showing: not made again
    HCURSOR made = MakeImageCursor(image);
    if (made == nullptr) {
        LogWin32Failure("CreateIconIndirect (cursor image)");
        return false;
    }
    HCURSOR old = image_cursor_;
    image_cursor_ = made;
    image_hash_ = hash;
    if (input_->pointer_inside) ApplyCursor();
    if (old != nullptr) DestroyCursor(old); // after the new one shows: never destroy the cursor in use
    return true;
}

void Win32Window::ClearCursorImage() {
    if (image_cursor_ == nullptr) return;
    HCURSOR old = image_cursor_;
    image_cursor_ = nullptr;
    image_hash_ = 0;
    if (input_->pointer_inside) ApplyCursor();
    DestroyCursor(old);
}

bool Win32Window::SetPointerLock(bool locked) {
    if (hwnd_ == nullptr) return false;
    ApplyLock(lock_.SetWanted(locked));
    return true;
}

void Win32Window::ClipToCentre() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    POINT centre{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(hwnd_, &centre);
    const RECT clip{centre.x, centre.y, centre.x + 1, centre.y + 1};
    ClipCursor(&clip);
}

void Win32Window::ApplyLock(PointerLockState::Action action) {
    if (hwnd_ == nullptr) return;
    if (action == PointerLockState::Action::Acquire) {
        RAWINPUTDEVICE device{0x01, 0x02, 0, hwnd_}; // generic desktop, mouse
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            LogWin32Failure("RegisterRawInputDevices (pointer lock)");
            lock_.Failed();
            return;
        }
        raw_input_ = true;
        ClipToCentre();
        lock_.Granted();
        ApplyCursor(); // hidden while locked
    } else if (action == PointerLockState::Action::Release) {
        ClipCursor(nullptr);
        if (raw_input_) {
            RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
            RegisterRawInputDevices(&device, 1, sizeof(device));
            raw_input_ = false;
        }
        if (input_->pointer_inside) ApplyCursor();
    }
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
