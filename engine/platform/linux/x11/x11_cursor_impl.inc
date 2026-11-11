// The implementation of x11_cursor.h. It is included by x11_window.cpp, inside that file's X11 region, and is not a
// translation unit of its own: in a unity build a second .cpp that includes the X11 headers finds them already included
// (their guards stay set) while the macros they define have been removed by x11_disable.h, so it would not compile.
// Included here, the X11 headers are seen once, with the macros, by both.

namespace engine_placeholder::os::x11
{

namespace
{

struct ShapeNames
{
    const char* theme;  // the cursor theme name (freedesktop / CSS)
    unsigned int font;  // the core font cursor used when the theme has none
};

constexpr ShapeNames kShapeNames[static_cast<usize>(CursorShape::Count)] = {
    {"left_ptr", XC_left_ptr},
    {"text", XC_xterm},
    {"pointer", XC_hand2},
    {"ew-resize", XC_sb_h_double_arrow},
    {"ns-resize", XC_sb_v_double_arrow},
    {"nwse-resize", XC_bottom_right_corner},
    {"nesw-resize", XC_bottom_left_corner},
    {"all-scroll", XC_fleur},
    {"not-allowed", XC_pirate},
    {nullptr, 0},  // Hidden: the blank cursor
};

unsigned long MakeBlank(Display* display, X11CursorState& state)
{
    if (state.blank_cursor == 0)
    {
        XcursorImage* image = XcursorImageCreate(1, 1);
        if (image == nullptr)
        {
            return 0;
        }
        image->xhot = 0;
        image->yhot = 0;
        image->pixels[0] = 0;  // fully transparent
        state.blank_cursor = XcursorImageLoadCursor(display, image);
        XcursorImageDestroy(image);
        if (state.blank_cursor != 0)
        {
            ++state.live;
        }
    }
    return state.blank_cursor;
}

unsigned long CursorForShape(Display* display, X11CursorState& state, CursorShape shape)
{
    if (shape == CursorShape::Hidden)
    {
        return MakeBlank(display, state);
    }
    const usize index = static_cast<usize>(shape);
    if (state.shape_cursor[index] == 0)
    {
        Cursor cursor = XcursorLibraryLoadCursor(display, kShapeNames[index].theme);
        if (cursor == 0)
        {
            cursor = XCreateFontCursor(display, kShapeNames[index].font);
        }
        state.shape_cursor[index] = cursor;
        if (cursor != 0)
        {
            ++state.live;
        }
    }
    return state.shape_cursor[index];
}

// The cursor the window should show now: the lock hides it, then a custom image, then the shape.
unsigned long EffectiveCursor(Display* display, X11CursorState& state)
{
    if (state.grab_held)
    {
        return MakeBlank(display, state);
    }
    if (state.image_active && state.image_cursor != 0)
    {
        return state.image_cursor;
    }
    return CursorForShape(display, state, state.shape);
}

void Apply(Display* display, ::Window window, X11CursorState& state)
{
    const unsigned long cursor = EffectiveCursor(display, state);
    if (cursor != 0)
    {
        XDefineCursor(display, window, cursor);
    }
    if (state.grab_held)
    {
        XChangeActivePointerGrab(display, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, cursor, CurrentTime);
    }
    XFlush(display);
}

void SelectRaw(Display* display, bool on, X11CursorState& state)
{
    if (!state.xi_ready)
    {
        return;
    }
    byte bits[XIMaskLen(XI_LASTEVENT)] = {};
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    if (on)
    {
        XISetMask(bits, XI_RawMotion);
    }
    XISelectEvents(display, DefaultRootWindow(display), &mask, 1);
}

void Acquire(Display* display, ::Window window, X11CursorState& state)
{
    // The grab confines the pointer to the window and keeps other clients from getting it; the cursor is blank.
    const unsigned long blank = MakeBlank(display, state);
    const int result = XGrabPointer(display, window, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                    GrabModeAsync, GrabModeAsync, window, blank, CurrentTime);
    if (result != GrabSuccess)
    {
        state.lock.Failed();
        return;
    }
    state.grab_held = true;
    state.lock.Granted();
    SelectRaw(display, true, state);
    XFlush(display);
}

void Drop(Display* display, X11CursorState& state)
{
    if (state.grab_held)
    {
        XUngrabPointer(display, CurrentTime);
        state.grab_held = false;
        SelectRaw(display, false, state);
    }
}

void DoAction(Display* display, ::Window window, X11CursorState& state, PointerLockState::Action action)
{
    if (action == PointerLockState::Action::Acquire)
    {
        Acquire(display, window, state);
        Apply(display, window, state);
    }
    else if (action == PointerLockState::Action::Release)
    {
        Drop(display, state);
        Apply(display, window, state);
    }
}

void FreeCursor(Display* display, unsigned long& cursor, X11CursorState& state)
{
    if (cursor != 0)
    {
        XFreeCursor(display, cursor);
        cursor = 0;
        if (state.live > 0)
        {
            --state.live;
        }
    }
}

}  // namespace

void CursorInit(_XDisplay* display, X11CursorState& state)
{
    int event = 0;
    int error = 0;
    if (!XQueryExtension(display, "XInputExtension", &state.xi_opcode, &event, &error))
    {
        return;
    }
    int major = 2;
    int minor = 0;
    state.xi_ready = XIQueryVersion(display, &major, &minor) == Success;
}

bool CursorSetShape(_XDisplay* display, unsigned long window, X11CursorState& state, CursorShape shape)
{
    if (display == nullptr || static_cast<usize>(shape) >= static_cast<usize>(CursorShape::Count))
    {
        return false;
    }
    state.shape = shape;
    Apply(display, window, state);
    return true;
}

bool CursorSetImage(_XDisplay* display, unsigned long window, X11CursorState& state, const CursorImage& image)
{
    if (display == nullptr || ValidateCursorImage(image) != CursorImageError::Ok)
    {
        return false;
    }
    const u64 hash = HashCursorImage(image);
    if (!(state.image_cursor != 0 && state.image_hash == hash))
    {
        XcursorImage* xi = XcursorImageCreate(static_cast<int>(image.width), static_cast<int>(image.height));
        if (xi == nullptr)
        {
            return false;
        }
        xi->xhot = static_cast<XcursorDim>(image.hotspot_x);
        xi->yhot = static_cast<XcursorDim>(image.hotspot_y);
        const usize count = static_cast<usize>(image.width) * image.height;
        for (usize i = 0; i < count; ++i)
        {
            const byte* p = image.rgba + i * 4;  // premultiplied RGBA bytes -> premultiplied ARGB word
            xi->pixels[i] = (static_cast<XcursorPixel>(p[3]) << 24) | (static_cast<XcursorPixel>(p[0]) << 16) |
                            (static_cast<XcursorPixel>(p[1]) << 8) | p[2];
        }
        const Cursor cursor = XcursorImageLoadCursor(display, xi);
        XcursorImageDestroy(xi);
        if (cursor == 0)
        {
            return false;
        }
        FreeCursor(display, state.image_cursor, state);
        state.image_cursor = cursor;
        state.image_hash = hash;
        ++state.live;
    }
    state.image_active = true;
    Apply(display, window, state);
    return true;
}

void CursorClearImage(_XDisplay* display, unsigned long window, X11CursorState& state)
{
    if (display == nullptr)
    {
        return;
    }
    state.image_active = false;
    FreeCursor(display, state.image_cursor, state);
    state.image_hash = 0;
    Apply(display, window, state);
}

void CursorSetLock(_XDisplay* display, unsigned long window, X11CursorState& state, bool locked)
{
    if (display == nullptr)
    {
        return;
    }
    DoAction(display, window, state, state.lock.SetWanted(locked));
}

void CursorSetFocus(_XDisplay* display, unsigned long window, X11CursorState& state, bool focused)
{
    if (display == nullptr)
    {
        return;
    }
    DoAction(display, window, state, state.lock.SetFocused(focused));
}

void CursorHandleGeneric(_XDisplay* display, X11CursorState& state, void* event, InputEventQueue& queue)
{
    XEvent* xevent = static_cast<XEvent*>(event);
    if (!state.xi_ready || xevent->xcookie.extension != state.xi_opcode || !XGetEventData(display, &xevent->xcookie))
    {
        return;
    }
    if (xevent->xcookie.evtype == XI_RawMotion && state.lock.Active())
    {
        const XIRawEvent* raw = static_cast<const XIRawEvent*>(xevent->xcookie.data);
        const byte* mask = raw->valuators.mask;
        const int mask_len = raw->valuators.mask_len;
        f64 dx = 0.0;
        f64 dy = 0.0;
        int index = 0;
        if (mask_len > 0 && XIMaskIsSet(mask, 0))
        {
            dx = raw->raw_values[index++];
        }
        if (mask_len > 0 && XIMaskIsSet(mask, 1))
        {
            dy = raw->raw_values[index];
        }
        if (dx != 0.0 || dy != 0.0)
        {
            InputEvent motion;
            motion.type = InputEventType::RelativeMotion;
            motion.x = dx;
            motion.y = dy;
            motion.time_ns = MonotonicTimeNanos();
            queue.Push(motion);
        }
    }
    XFreeEventData(display, &xevent->xcookie);
}

void CursorRelease(_XDisplay* display, unsigned long window, X11CursorState& state)
{
    if (display == nullptr)
    {
        return;
    }
    Drop(display, state);
    state.lock.Cleared();
    if (window != 0)
    {
        XUndefineCursor(display, window);
    }
    for (unsigned long& cursor : state.shape_cursor)
    {
        FreeCursor(display, cursor, state);
    }
    FreeCursor(display, state.image_cursor, state);
    FreeCursor(display, state.blank_cursor, state);
    state.image_active = false;
    XFlush(display);
}

}  // namespace engine_placeholder::os::x11
