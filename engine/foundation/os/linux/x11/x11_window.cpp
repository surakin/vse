#include "os/linux/x11/x11_window.h"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstring>

#include "log/log.h"
#include "mem/arena_allocator.h"

namespace engine_placeholder::os::x11 {

namespace {

// Window titles have no hard OS limit; an overlong one is truncated
// (cosmetic) rather than failed, same policy as WaylandWindow.
constexpr usize kMaxTitleBytes = 256;

// Same placeholder fill color as WaylandWindow (XRGB) so both backends
// look alike until Phase 3's renderer draws into the window.
constexpr unsigned long kPlaceholderFillColor = 0x264F78;

void LogResize(u32 width, u32 height) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("X11: window resized to ").AppendUInt(width).Append('x').AppendUInt(height);
    Log(LogPlatform, LogVerbosity::Info, line.View());
}

u32 ModifiersFromState(unsigned int state) {
    u32 mods = 0;
    if ((state & ShiftMask) != 0) mods |= kModShift;
    if ((state & ControlMask) != 0) mods |= kModCtrl;
    if ((state & Mod1Mask) != 0) mods |= kModAlt;
    if ((state & Mod4Mask) != 0) mods |= kModSuper;
    if ((state & LockMask) != 0) mods |= kModCapsLock;
    if ((state & Mod2Mask) != 0) mods |= kModNumLock;
    return mods;
}

// X reports the wheel as buttons 4-7. Wayland reports ~10 units per
// wheel notch, so use the same magnitude to keep InputState's scroll
// values comparable across backends.
constexpr f64 kScrollUnitsPerNotch = 10.0;

void HandleButton(const XButtonEvent& e, bool pressed, InputState& input) {
    input.mouse_x = e.x;
    input.mouse_y = e.y;
    input.keyboard.modifiers = ModifiersFromState(e.state);
    u32 bit = 0;
    switch (e.button) {
    case Button1: bit = kMouseLeft; break;
    case Button2: bit = kMouseMiddle; break;
    case Button3: bit = kMouseRight; break;
    case 8: bit = kMouseBack; break;
    case 9: bit = kMouseForward; break;
    case Button4:
        if (pressed) input.scroll_dy -= kScrollUnitsPerNotch; // up
        return;
    case Button5:
        if (pressed) input.scroll_dy += kScrollUnitsPerNotch; // down
        return;
    case 6:
        if (pressed) input.scroll_dx -= kScrollUnitsPerNotch; // left
        return;
    case 7:
        if (pressed) input.scroll_dx += kScrollUnitsPerNotch; // right
        return;
    default: return;
    }
    if (pressed) {
        input.mouse_buttons |= bit;
    } else {
        input.mouse_buttons &= ~bit;
    }
}

} // namespace

X11Window X11Window::Create(StringView title, u32 width, u32 height) {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "X11: XOpenDisplay failed (no X server / $DISPLAY unset?)");
        return X11Window();
    }
    if (width == 0 || height == 0) {
        Log(LogPlatform, LogVerbosity::Error, "X11: X11Window::Create called with a zero-sized window");
        XCloseDisplay(display);
        return X11Window();
    }

    int screen = DefaultScreen(display);
    ::Window root = RootWindow(display, screen);

    XSetWindowAttributes attrs = {};
    attrs.background_pixel = kPlaceholderFillColor;
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                       ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask;
    ::Window window = XCreateWindow(display, root, 0, 0, width, height, 0, CopyFromParent, InputOutput,
                                    CopyFromParent, CWBackPixel | CWEventMask, &attrs);
    if (window == 0) {
        Log(LogPlatform, LogVerbosity::Error, "X11: XCreateWindow failed");
        XCloseDisplay(display);
        return X11Window();
    }

    char title_buf[kMaxTitleBytes];
    usize n = title.Length() < kMaxTitleBytes - 1 ? title.Length() : kMaxTitleBytes - 1;
    for (usize i = 0; i < n; ++i) {
        title_buf[i] = title[i];
    }
    title_buf[n] = '\0';
    XStoreName(display, window, title_buf);
    // _NET_WM_NAME carries the UTF-8 title modern WMs prefer.
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, window, net_wm_name, utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title_buf), static_cast<int>(n));

    // Ask the WM to send a ClientMessage on close instead of killing the
    // connection.
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    // Day 25: an input context is what makes Xutf8LookupString usable.
    // Optional — without an input method we fall back to XLookupString.
    XSetLocaleModifiers("");
    XIM xim = XOpenIM(display, nullptr, nullptr, nullptr);
    XIC xic = nullptr;
    if (xim != nullptr) {
        xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window,
                        XNFocusWindow, window, nullptr);
    }

    // Without this, X synthesizes a KeyRelease+KeyPress pair for every
    // autorepeat tick, which would flicker keys up/down across frames.
    XkbSetDetectableAutoRepeat(display, True, nullptr);

    XMapWindow(display, window);
    XSync(display, False);

    X11Window result;
    result.display_ = display;
    result.window_ = window;
    result.wm_delete_window_ = wm_delete;
    result.input_method_ = reinterpret_cast<_XIM*>(xim);
    result.input_context_ = reinterpret_cast<_XIC*>(xic);
    result.width_ = width;
    result.height_ = height;
    result.resize_serial_ = detail::NextResizeSerial();
    return result;
}

X11Window::~X11Window() {
    ReleaseAll();
}

X11Window::X11Window(X11Window&& other) noexcept
    : display_(other.display_),
      window_(other.window_),
      wm_delete_window_(other.wm_delete_window_),
      input_method_(other.input_method_),
      input_context_(other.input_context_),
      input_(other.input_),
      width_(other.width_),
      height_(other.height_),
      should_close_(other.should_close_),
      unmapped_(other.unmapped_),
      resize_serial_(other.resize_serial_) {
    std::memcpy(held_, other.held_, sizeof(held_));
    other.display_ = nullptr;
    other.window_ = 0;
    other.wm_delete_window_ = 0;
    other.input_method_ = nullptr;
    other.input_context_ = nullptr;
    other.input_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.should_close_ = false;
    other.unmapped_ = false;
    other.resize_serial_ = 0;
}

X11Window& X11Window::operator=(X11Window&& other) noexcept {
    if (this != &other) {
        ReleaseAll();
        display_ = other.display_;
        window_ = other.window_;
        wm_delete_window_ = other.wm_delete_window_;
        input_method_ = other.input_method_;
        input_context_ = other.input_context_;
        input_ = other.input_;
        width_ = other.width_;
        height_ = other.height_;
        should_close_ = other.should_close_;
        unmapped_ = other.unmapped_;
        resize_serial_ = other.resize_serial_;
        std::memcpy(held_, other.held_, sizeof(held_));
        other.display_ = nullptr;
        other.window_ = 0;
        other.wm_delete_window_ = 0;
        other.input_method_ = nullptr;
        other.input_context_ = nullptr;
        other.input_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.should_close_ = false;
        other.unmapped_ = false;
        other.resize_serial_ = 0;
    }
    return *this;
}

void X11Window::ReleaseAll() {
    if (input_context_ != nullptr) {
        XDestroyIC(reinterpret_cast<XIC>(input_context_));
        input_context_ = nullptr;
    }
    if (input_method_ != nullptr) {
        XCloseIM(reinterpret_cast<XIM>(input_method_));
        input_method_ = nullptr;
    }
    if (display_ != nullptr) {
        if (window_ != 0) {
            XDestroyWindow(display_, window_);
            window_ = 0;
        }
        XCloseDisplay(display_);
        display_ = nullptr;
    }
    input_ = nullptr;
    should_close_ = false;
    unmapped_ = false;
    width_ = 0;
    height_ = 0;
    resize_serial_ = 0;
}

void X11Window::PumpEvents(InputState* input) {
    if (display_ == nullptr) {
        return;
    }
    // XPending flushes the output buffer and reads whatever is already on
    // the socket without blocking when the queue is empty.
    while (XPending(display_) > 0) {
        XEvent event;
        XNextEvent(display_, &event);
        // Input methods may swallow events (e.g. compose sequences).
        if (XFilterEvent(&event, None) != 0) {
            continue;
        }
        switch (event.type) {
        case ConfigureNotify: {
            u32 w = static_cast<u32>(event.xconfigure.width);
            u32 h = static_cast<u32>(event.xconfigure.height);
            if (w != width_ || h != height_) {
                width_ = w;
                height_ = h;
                resize_serial_ = detail::NextResizeSerial();
                LogResize(w, h);
            }
            break;
        }
        case UnmapNotify:
            if (event.xunmap.window == window_) {
                unmapped_ = true;
            }
            break;
        case MapNotify:
            if (event.xmap.window == window_) {
                unmapped_ = false;
            }
            break;
        case ClientMessage:
            if (static_cast<unsigned long>(event.xclient.data.l[0]) == wm_delete_window_ &&
                event.xclient.message_type == XInternAtom(display_, "WM_PROTOCOLS", False)) {
                should_close_ = true;
            }
            break;
        case Expose:
            // The window background is a server-side solid fill, so
            // there's nothing to repaint until a renderer exists.
            break;
        case KeyPress:
            HandleKey(&event.xkey, true, input);
            break;
        case KeyRelease:
            HandleKey(&event.xkey, false, input);
            break;
        case ButtonPress:
        case ButtonRelease:
            if (input != nullptr) {
                HandleButton(event.xbutton, event.type == ButtonPress, *input);
            }
            break;
        case MotionNotify:
            if (input != nullptr) {
                input->mouse_x = event.xmotion.x;
                input->mouse_y = event.xmotion.y;
                input->pointer_inside = true;
                input->keyboard.modifiers = ModifiersFromState(event.xmotion.state);
            }
            break;
        case EnterNotify:
            if (input != nullptr) {
                input->pointer_inside = true;
                input->mouse_x = event.xcrossing.x;
                input->mouse_y = event.xcrossing.y;
            }
            break;
        case LeaveNotify:
            if (input != nullptr) {
                // Held buttons survive leaving (X grabs deliver the
                // release), matching the Wayland backend.
                input->pointer_inside = false;
            }
            break;
        case FocusOut:
            // Only a real loss of keyboard focus means we will miss the
            // releases. WMs also generate FocusOut with mode
            // NotifyGrab/NotifyWhileGrabbed for transient keyboard grabs
            // (Alt-Tab, menus), followed by a FocusIn when the grab ends —
            // the keys are still physically held then — and
            // NotifyInferior is focus moving between our own windows.
            if (event.xfocus.mode != NotifyGrab && event.xfocus.mode != NotifyWhileGrabbed &&
                event.xfocus.detail != NotifyInferior) {
                ReleaseAllKeys(input);
            }
            break;
        default:
            break;
        }
    }
}

void X11Window::ReleaseAllKeys(InputState* input) {
    for (Key& k : held_) {
        k = Key::Unknown;
    }
    if (input != nullptr) {
        input->keyboard.ReleaseAll();
        // Modifier releases happen elsewhere too; don't leave Ctrl etc.
        // latched in the mask until the next input event rewrites it.
        input->keyboard.modifiers = 0;
    }
}

void X11Window::HandleKey(const void* xkey_event, bool pressed, InputState* input) {
    if (input == nullptr) {
        return;
    }
    // Copy: the XKeyEvent API takes a mutable pointer.
    XKeyEvent xkey = *static_cast<const XKeyEvent*>(xkey_event);
    input->keyboard.modifiers = ModifiersFromState(xkey.state);
    unsigned int code = xkey.keycode;
    if (code >= 256) {
        return;
    }

    if (!pressed) {
        Key key = held_[code];
        if (key == Key::Unknown) {
            // Press predates us (e.g. focus arrived mid-hold): translate.
            KeySym sym = XLookupKeysym(&xkey, 0);
            key = KeyFromKeysym(static_cast<u32>(sym));
        }
        input->keyboard.SetDown(key, false);
        held_[code] = Key::Unknown;
        return;
    }

    // Full keysym translation through the input context when there is
    // one (honors layout group, modifiers and compose), else the plain
    // XLookupString path. The UTF-8 text itself is not consumed yet —
    // text input events are a later input-layer feature — but it is the
    // reason the IC exists and is validated by the lookup status.
    KeySym sym = NoSymbol;
    char text[32];
    if (input_context_ != nullptr) {
        Status status = 0;
        Xutf8LookupString(reinterpret_cast<XIC>(input_context_), &xkey, text, sizeof(text), &sym, &status);
        if (status != XLookupKeySym && status != XLookupBoth) {
            sym = NoSymbol;
        }
    } else {
        XLookupString(&xkey, text, sizeof(text), &sym, nullptr);
    }
    if (sym == NoSymbol) {
        sym = XLookupKeysym(&xkey, 0);
    }
    Key key = KeyFromKeysym(static_cast<u32>(sym));
    held_[code] = key;
    input->keyboard.SetDown(key, true);
}

} // namespace engine_placeholder::os::x11
