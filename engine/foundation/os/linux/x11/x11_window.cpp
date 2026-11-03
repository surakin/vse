#include "os/linux/x11/x11_window.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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
      width_(other.width_),
      height_(other.height_),
      should_close_(other.should_close_) {
    other.display_ = nullptr;
    other.window_ = 0;
    other.wm_delete_window_ = 0;
    other.input_method_ = nullptr;
    other.input_context_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.should_close_ = false;
}

X11Window& X11Window::operator=(X11Window&& other) noexcept {
    if (this != &other) {
        ReleaseAll();
        display_ = other.display_;
        window_ = other.window_;
        wm_delete_window_ = other.wm_delete_window_;
        input_method_ = other.input_method_;
        input_context_ = other.input_context_;
        width_ = other.width_;
        height_ = other.height_;
        should_close_ = other.should_close_;
        other.display_ = nullptr;
        other.window_ = 0;
        other.wm_delete_window_ = 0;
        other.input_method_ = nullptr;
        other.input_context_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.should_close_ = false;
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
    should_close_ = false;
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
                LogResize(w, h);
            }
            break;
        }
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
        default:
            break;
        }
    }
}

void X11Window::HandleKey(const void* xkey_event, bool pressed, InputState* input) {
    (void)xkey_event;
    (void)pressed;
    (void)input;
    // Day 25.
}

} // namespace engine_placeholder::os::x11
