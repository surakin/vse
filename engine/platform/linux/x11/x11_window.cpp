#include "platform/linux/x11/x11_window.h"
#include "cstr/mem_ops.h"

#include "unity/x11_enable.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>

#undef CursorShape // <X11/X.h> defines it as a number; the engine has os::CursorShape


#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/os.h"
#include "platform/linux/key_translate.h"
#include "platform/linux/x11/x11_clipboard_impl.inc"
#include "platform/linux/x11/x11_dnd_impl.inc"
#include "platform/linux/x11/x11_cursor_impl.inc" // x11_cursor.h's functions: see the note at the top of that file

namespace engine_placeholder::os::x11 {

namespace {

// Window titles have no hard OS limit; an overlong one is truncated
// (cosmetic) rather than failed, same policy as WaylandWindow.
constexpr usize kMaxTitleBytes = 256;

// Same placeholder fill color as WaylandWindow (XRGB) so both backends
// look alike until Phase 4's renderer draws into the window.
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


// Appends the text a key press produced (UTF-8, or Latin-1 when there is no input context) as one Text event per Unicode scalar; control characters are keys, not text,
// and a press with Ctrl or Alt held is a shortcut.
void AppendText(const char* bytes, int length, bool latin1, u32 modifiers, u64 now_ns, InputEventQueue& queue) {
    if ((modifiers & (kModCtrl | kModAlt)) != 0) {
        return;
    }
    int i = 0;
    while (i < length) {
        const u8 b = static_cast<u8>(bytes[i]);
        InputEvent e;
        e.type = InputEventType::Text;
        e.modifiers = modifiers;
        e.time_ns = now_ns;
        if (latin1) {
            ++i;
            if (b < 0x20 || (b >= 0x7f && b < 0xa0)) continue;
            if (b < 0x80) {
                e.text[0] = static_cast<char>(b);
                e.text_length = 1;
            } else {
                e.text[0] = static_cast<char>(0xC0 | (b >> 6));
                e.text[1] = static_cast<char>(0x80 | (b & 0x3F));
                e.text_length = 2;
            }
        } else {
            const int n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
            if (i + n > length || (n == 1 && b >= 0x80)) {
                break; // a truncated or invalid sequence from the input method: drop the rest
            }
            if (b < 0x20 || b == 0x7f) {
                i += n;
                continue;
            }
            for (int k = 0; k < n; ++k) e.text[k] = bytes[i + k];
            e.text_length = static_cast<u8>(n);
            i += n;
        }
        queue.Push(e);
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
                       ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask | PropertyChangeMask;
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
                    reinterpret_cast<const byte*>(title_buf), static_cast<int>(n));

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
    CursorInit(display, result.cursor_);
    ClipboardInit(display, window, result.clipboard_, &result.events_);
    DndInit(display, window, result.dnd_, &result.events_);
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
      resize_serial_(other.resize_serial_),
      events_(other.events_),
      focused_(other.focused_),
      cursor_(other.cursor_),
      clipboard_(static_cast<X11ClipboardState&&>(other.clipboard_)),
      dnd_(static_cast<X11DndState&&>(other.dnd_)) {
    MemCopy(held_, other.held_, sizeof(held_));
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
    other.events_.Clear();
    other.focused_ = false;
    other.cursor_ = X11CursorState();
    clipboard_.requests.Init(&events_); // the queue moved with the window
    clipboard_.queue = &events_;
    dnd_.queue = &events_;
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
        events_ = other.events_;
        focused_ = other.focused_;
        cursor_ = other.cursor_;
        clipboard_ = static_cast<X11ClipboardState&&>(other.clipboard_);
        dnd_ = static_cast<X11DndState&&>(other.dnd_);
        other.events_.Clear();
        other.focused_ = false;
        other.cursor_ = X11CursorState();
        clipboard_.requests.Init(&events_);
        clipboard_.queue = &events_;
        dnd_.queue = &events_;
        MemCopy(held_, other.held_, sizeof(held_));
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
    if (display_ != nullptr) {
        CursorRelease(display_, window_, cursor_); // the grab and the cursors go before the display closes
        DndRelease(display_, window_, clipboard_, dnd_, MonotonicTimeNanos());
        ClipboardRelease(display_, clipboard_, MonotonicTimeNanos()); // selections are given up: the contents die with the window
    }
    cursor_ = X11CursorState();
    clipboard_ = X11ClipboardState();
    dnd_ = X11DndState();
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

bool X11Window::SetFullscreen(bool fullscreen) {
    if (display_ == nullptr) {
        return false;
    }
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window_;
    event.xclient.message_type = XInternAtom(display_, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = fullscreen ? 1 : 0; // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
    event.xclient.data.l[1] = static_cast<long>(XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False));
    event.xclient.data.l[3] = 1; // source: a normal application
    const Status sent = XSendEvent(display_, DefaultRootWindow(display_), False,
                                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display_);
    return sent != 0;
}

bool X11Window::IsFullscreen() const {
    if (display_ == nullptr) {
        return false;
    }
    const Atom state = XInternAtom(display_, "_NET_WM_STATE", False);
    const Atom fullscreen = XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False);
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    byte* data = nullptr;
    bool result = false;
    if (XGetWindowProperty(display_, window_, state, 0, 64, False, XA_ATOM, &type, &format, &count, &after, &data) == Success &&
        data != nullptr) {
        const Atom* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < count; ++i) {
            if (atoms[i] == fullscreen) {
                result = true;
            }
        }
    }
    if (data != nullptr) {
        XFree(data);
    }
    return result;
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
        if (dnd_.source.active) {
            // While this window drags, the pointer grab sends every motion and the release here: they steer the drag and are not input.
            if (event.type == MotionNotify) {
                DragPointerMoved(event.xmotion.x_root, event.xmotion.y_root, event.xmotion.state);
                continue;
            }
            if (event.type == ButtonRelease && event.xbutton.button >= Button1 && event.xbutton.button <= Button3) {
                DragButtonReleased();
                continue;
            }
            if (event.type == ButtonPress) {
                continue;
            }
            if (event.type == KeyPress || event.type == KeyRelease) {
                if (event.type == KeyPress && XLookupKeysym(&event.xkey, 0) == XK_Escape) {
                    DndCancel(display_, window_, clipboard_, dnd_, MonotonicTimeNanos());
                }
                // The release of a key that went down before the drag is still input: dropped, the key (Shift of a move drag, say) would stay down for the program.
                if (event.type == KeyRelease && event.xkey.keycode < 256 && held_[event.xkey.keycode] != Key::Unknown) HandleKey(&event.xkey, false, input);
                continue;
            }
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
                InputEvent resize;
                resize.type = InputEventType::Resize;
                resize.width = w;
                resize.height = h;
                QueueEvent(resize);
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
            if (DndHandleClientMessage(display_, window_, clipboard_, dnd_, &event, MonotonicTimeNanos())) {
                break;
            }
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
        case ButtonRelease: {
            const XButtonEvent& be = event.xbutton;
            const bool pressed = event.type == ButtonPress;
            if (be.button >= Button4 && be.button <= 7) {
                if (pressed) {
                    InputEvent wheel;
                    wheel.type = InputEventType::Wheel;
                    wheel.modifiers = ModifiersFromState(be.state);
                    if (be.button == Button4) wheel.y = -1.0;
                    if (be.button == Button5) wheel.y = 1.0;
                    if (be.button == 6) wheel.x = -1.0;
                    if (be.button == 7) wheel.x = 1.0;
                    QueueEvent(wheel);
                }
            } else {
                u32 bit = 0;
                switch (be.button) {
                case Button1: bit = kMouseLeft; break;
                case Button2: bit = kMouseMiddle; break;
                case Button3: bit = kMouseRight; break;
                case 8: bit = kMouseBack; break;
                case 9: bit = kMouseForward; break;
                default: break;
                }
                if (bit != 0) {
                    InputEvent button;
                    button.type = InputEventType::PointerButton;
                    button.button = bit;
                    button.down = pressed;
                    button.x = be.x;
                    button.y = be.y;
                    button.modifiers = ModifiersFromState(be.state);
                    QueueEvent(button);
                }
            }
            if (input != nullptr) {
                HandleButton(be, pressed, *input);
            }
            break;
        }
        case MotionNotify: {
            InputEvent move;
            move.type = InputEventType::PointerMove;
            move.x = event.xmotion.x;
            move.y = event.xmotion.y;
            move.modifiers = ModifiersFromState(event.xmotion.state);
            QueueEvent(move);
            if (input != nullptr) {
                input->mouse_x = event.xmotion.x;
                input->mouse_y = event.xmotion.y;
                input->pointer_inside = true;
                input->keyboard.modifiers = ModifiersFromState(event.xmotion.state);
            }
            break;
        }
        case EnterNotify: {
            InputEvent enter;
            enter.type = InputEventType::PointerEnter;
            enter.x = event.xcrossing.x;
            enter.y = event.xcrossing.y;
            enter.modifiers = ModifiersFromState(event.xcrossing.state);
            QueueEvent(enter);
            if (input != nullptr) {
                input->pointer_inside = true;
                input->mouse_x = event.xcrossing.x;
                input->mouse_y = event.xcrossing.y;
            }
            break;
        }
        case LeaveNotify: {
            InputEvent leave;
            leave.type = InputEventType::PointerLeave;
            leave.modifiers = ModifiersFromState(event.xcrossing.state);
            QueueEvent(leave);
            if (input != nullptr) {
                // Held buttons survive leaving (X grabs deliver the
                // release), matching the Wayland backend.
                input->pointer_inside = false;
            }
            break;
        }
        case GenericEvent:
            CursorHandleGeneric(display_, cursor_, &event, events_); // XInput 2 raw motion while the pointer is locked
            break;
        case FocusIn:
            // Real focus only: not the grab-induced changes, not focus moving inside the window, not the pointer-root pseudo focus.
            if (event.xfocus.mode != NotifyGrab && event.xfocus.mode != NotifyUngrab && event.xfocus.detail != NotifyInferior &&
                event.xfocus.detail != NotifyPointer && event.xfocus.detail != NotifyPointerRoot && event.xfocus.detail != NotifyDetailNone && !focused_) {
                focused_ = true;
                CursorSetFocus(display_, window_, cursor_, true);
                InputEvent gained;
                gained.type = InputEventType::FocusGained;
                QueueEvent(gained);
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
                if (focused_) {
                    focused_ = false;
                    CursorSetFocus(display_, window_, cursor_, false);
                    InputEvent lost;
                    lost.type = InputEventType::FocusLost;
                    QueueEvent(lost);
                }
            }
            break;
        default:
            ClipboardHandleEvent(display_, window_, clipboard_, &event, MonotonicTimeNanos());
            break;
        }
    }
    ClipboardTick(display_, clipboard_, MonotonicTimeNanos());
    DndTick(display_, window_, clipboard_, dnd_, MonotonicTimeNanos());
}

u32 X11Window::RequestDropData(ClipboardFormat format, StringView custom_name) {
    return DndRequestData(display_, window_, clipboard_, dnd_, format, custom_name, MonotonicTimeNanos());
}

bool X11Window::BeginDrag(const DragSource& source) {
    if (cursor_.lock.Active()) return false; // the pointer grab of the lock is in use
    return DndBegin(display_, window_, clipboard_, dnd_, source, MonotonicTimeNanos());
}

void X11Window::DragPointerMoved(int root_x, int root_y, unsigned long state) {
    DndSourceMotion(display_, window_, clipboard_, dnd_, root_x, root_y, state, MonotonicTimeNanos());
}

void X11Window::DragButtonReleased() { DndSourceRelease(display_, window_, clipboard_, dnd_, MonotonicTimeNanos()); }

void X11Window::CancelDrag() { DndCancel(display_, window_, clipboard_, dnd_, MonotonicTimeNanos()); }

bool X11Window::SetClipboard(ClipboardKind kind, const ClipboardOffer& offer) {
    return ClipboardSet(display_, window_, clipboard_, kind, offer, MonotonicTimeNanos());
}

u32 X11Window::RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name) {
    return ClipboardRequest(display_, window_, clipboard_, kind, format, custom_name, MonotonicTimeNanos());
}

bool X11Window::SetPointerLock(bool locked) {
    if (display_ == nullptr) {
        return false;
    }
    CursorSetLock(display_, window_, cursor_, locked);
    return true;
}

void X11Window::QueueEvent(InputEvent event) {
    event.time_ns = MonotonicTimeNanos();
    events_.Push(event);
}

void X11Window::ReleaseAllKeys(InputState* input) {
    // Keys still held when focus goes are released in the stream too, so a consumer never sees a key stay down.
    for (usize code = 0; code < 256; ++code) {
        if (held_[code] != Key::Unknown) {
            InputEvent up;
            up.type = InputEventType::KeyUp;
            up.key = held_[code];
            QueueEvent(up);
        }
    }
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
    // Copy: the XKeyEvent API takes a mutable pointer.
    XKeyEvent xkey = *static_cast<const XKeyEvent*>(xkey_event);
    const u32 modifiers = ModifiersFromState(xkey.state);
    if (input != nullptr) {
        input->keyboard.modifiers = modifiers;
    }
    unsigned int code = xkey.keycode;

    // An input method commits composed text as a KeyPress with keycode 0 (or through a press whose lookup returns characters but no key): the text is the event.
    if (pressed && code == 0) {
        if (input_context_ != nullptr) {
            char text[64];
            Status status = 0;
            const int n = Xutf8LookupString(reinterpret_cast<XIC>(input_context_), &xkey, text, sizeof(text), nullptr, &status);
            if ((status == XLookupChars || status == XLookupBoth) && n > 0) {
                AppendText(text, n, false, modifiers, MonotonicTimeNanos(), events_);
            }
        }
        return;
    }
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
        if (key != Key::Unknown) {
            InputEvent up;
            up.type = InputEventType::KeyUp;
            up.key = key;
            up.modifiers = modifiers;
            QueueEvent(up);
        }
        if (input != nullptr) {
            input->keyboard.SetDown(key, false);
        }
        held_[code] = Key::Unknown;
        return;
    }

    // Full keysym translation through the input context when there is
    // one (honors layout group, modifiers and compose), else the plain
    // XLookupString path. The same lookup gives the press's text.
    KeySym sym = NoSymbol;
    char text[32];
    int text_length = 0;
    bool latin1 = false;
    if (input_context_ != nullptr) {
        Status status = 0;
        text_length = Xutf8LookupString(reinterpret_cast<XIC>(input_context_), &xkey, text, sizeof(text), &sym, &status);
        if (status != XLookupChars && status != XLookupBoth) {
            text_length = 0;
        }
        if (status != XLookupKeySym && status != XLookupBoth) {
            sym = NoSymbol;
        }
    } else {
        text_length = XLookupString(&xkey, text, sizeof(text), &sym, nullptr);
        latin1 = true;
    }
    if (sym == NoSymbol) {
        sym = XLookupKeysym(&xkey, 0);
    }
    Key key = KeyFromKeysym(static_cast<u32>(sym));
    // With detectable auto-repeat (set at creation) a repeat is a press of a key that is already down.
    const bool repeat = held_[code] != Key::Unknown;
    held_[code] = key;
    if (input != nullptr) {
        input->keyboard.SetDown(key, true);
    }
    const u64 now = MonotonicTimeNanos();
    if (key != Key::Unknown) { // an unnamed key (a Cyrillic letter) types its text but is neither down nor up: nothing could release it
        InputEvent down;
        down.type = InputEventType::KeyDown;
        down.key = key;
        down.repeat = repeat;
        down.modifiers = modifiers;
        down.time_ns = now;
        events_.Push(down);
    }
    if (text_length > 0) {
        AppendText(text, text_length, latin1, modifiers, now, events_);
    }
}

} // namespace engine_placeholder::os::x11

#include "unity/x11_disable.h"
