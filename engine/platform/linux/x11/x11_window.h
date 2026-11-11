#pragma once

#include "platform/input.h"
#include "platform/linux/x11/x11_clipboard.h"
#include "platform/linux/x11/x11_cursor.h"
#include "platform/linux/x11/x11_dnd.h"
#include "platform/window.h"
#include "string/string.h"
#include "types.h"

//! X11 window backend: the fallback `os::Window` built on libX11.

// Opaque Xlib handles, forward-declared so including this header doesn't
// pull in <X11/Xlib.h> (its Bool/Status/None/KeyPress macros collide
// with ordinary identifiers). Window and Atom are plain XIDs
// (unsigned long) in Xlib.
struct _XDisplay;
struct _XIC;
struct _XIM;

namespace engine_placeholder::os::x11
{

/// Toplevel window on an X11 display, translating X events into an `InputState`.
///
/// Events are polled rather than delivered through callbacks capturing `this`,
/// so the type is movable.
///
/// - Thread-safety: owner-thread-only. Create, pump and destroy it on one thread.
/// - Ownership: owns the X display connection, window, and input method and context;
///   all are released on destruction. A moved-from window is invalid.
class X11Window final : public Window
{
public:
    /// Opens the display named by `$DISPLAY`, creates and maps a toplevel window, and registers `WM_DELETE_WINDOW`.
    /// - Param title: window title.
    /// - Param width: initial client width in pixels.
    /// - Param height: initial client height in pixels.
    /// - Returns: the window; check [[os::x11::X11Window::Valid]].
    /// - Fails: returns an invalid window (logged via `LogPlatform`) if the display cannot be opened or a request
    /// fails.
    static X11Window Create(StringView title, u32 width, u32 height);

    /// Constructs an invalid window that owns nothing.
    X11Window() = default;

    /// Destroys the window and closes the display connection.
    ~X11Window() override;

    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;
    /// Takes over the resources of `other`, leaving it invalid.
    X11Window(X11Window&& other) noexcept;

    /// Releases this window's resources, then takes over those of `other`, leaving it invalid.
    X11Window& operator=(X11Window&& other) noexcept;

    /// Returns whether the display connection is open.
    bool Valid() const override { return display_ != nullptr; }

    /// Drains every event already queued without blocking, updating window state and `input`.
    ///
    /// Handles Expose, ConfigureNotify and ClientMessage (close request), and,
    /// when `input` is non-null, key, button and motion events.
    /// - Param input: input state to update, or null to handle events but ignore input.
    void PumpEvents(InputState* input);

    /// Drains pending events into the `InputState` set by [[os::x11::X11Window::SetInputState]], if any.
    void PumpEvents() override { PumpEvents(input_); }
    /// Sets the input state that [[os::x11::X11Window::PumpEvents()]] fills.
    ///
    /// - Param input: state to fill, or null to ignore input; not owned and must outlive its use here.
    void SetInputState(InputState* input) { input_ = input; }
    /// Removes the oldest queued input event; see [[os::Window::PollEvent]]. Events are queued by every `PumpEvents`,
    /// whether or not an `InputState` is set.
    bool PollEvent(InputEvent* out) override { return events_.Pop(out); }
    /// Returns the number of events dropped because the queue was full.
    u64 DroppedEvents() const override { return events_.Dropped(); }
    /// Returns whether the window manager asked the window to close.
    bool ShouldClose() const override { return should_close_; }
    /// Returns the current client width in pixels.
    u32 Width() const override { return width_; }
    /// Returns the current client height in pixels.
    u32 Height() const override { return height_; }
    /// Returns the backend id, `"x11"`.
    const char* BackendName() const override { return "x11"; }
    /// Returns the `Display*` and window id, with kind `Xlib`; `Invalid` if the window is invalid.
    NativeWindowInfo GetNativeInfo() const override
    {
        NativeWindowInfo info;
        if (display_ != nullptr)
        {
            info.kind = NativeWindowKind::Xlib;
            info.display = display_;
            info.window_id = window_;
        }
        return info;
    }
    /// Returns whether the window is closed, has zero area, or the window manager has unmapped or iconified it.
    bool IsMinimized() const override { return display_ == nullptr || width_ == 0 || height_ == 0 || unmapped_; }
    /// Returns a counter that changes each time the size changes; 0 for an invalid window.
    u64 ResizeSerial() const override { return resize_serial_; }
    /// Returns true on a valid window: X11 fullscreen goes through the window manager (`_NET_WM_STATE_FULLSCREEN`),
    /// which may still refuse.
    bool SupportsFullscreen() const override { return display_ != nullptr; }
    /// Sends the `_NET_WM_STATE` client message to the root window that adds or removes fullscreen.
    /// - Param fullscreen: the state wanted.
    /// - Returns: true if the message was sent; false on an invalid window.
    bool SetFullscreen(bool fullscreen) override;
    /// Returns whether `_NET_WM_STATE` currently lists fullscreen (read from the window manager's property).
    bool IsFullscreen() const override;

    /// Returns true on a valid window: shapes come from the cursor theme or the core cursor font.
    bool SupportsCursorShape() const override { return display_ != nullptr; }
    /// Shows a standard cursor shape; see [[os::Window::SetCursor]].
    bool SetCursor(CursorShape shape) override { return CursorSetShape(display_, window_, cursor_, shape); }
    /// Returns true on a valid window: the image becomes an Xcursor.
    bool SupportsCursorImage() const override { return display_ != nullptr; }
    /// Shows a custom cursor image at its pixel size (X11 has no cursor scaling); see [[os::Window::SetCursorImage]].
    bool SetCursorImage(const CursorImage& image) override { return CursorSetImage(display_, window_, cursor_, image); }
    /// Removes the custom cursor image.
    void ClearCursorImage() override { CursorClearImage(display_, window_, cursor_); }
    /// Returns true on a valid window: the lock is a pointer grab with XInput 2 raw motion (without XInput 2 the grab
    /// works and no relative motion arrives).
    bool SupportsPointerLock() const override { return display_ != nullptr; }
    /// Locks or unlocks the pointer; see [[os::Window::SetPointerLock]]. The grab is taken at once if the window has
    /// focus, else when focus arrives.
    bool SetPointerLock(bool locked) override;
    /// Returns whether this window holds the pointer grab.
    bool PointerLocked() const override { return cursor_.lock.Active(); }
    /// Returns how many server cursors the window owns, so a test can check that replacing and clearing cursor images
    /// frees them.
    u32 CursorObjectCount() const { return cursor_.live; }

    /// Returns true on a valid window: both selections work (the clipboard and the primary selection).
    bool SupportsClipboard(ClipboardKind kind) const override
    {
        return display_ != nullptr && static_cast<usize>(kind) < static_cast<usize>(ClipboardKind::Count);
    }
    /// Takes the selection; see [[os::Window::SetClipboard]]. The contents are served from `PumpEvents`, in INCR pieces
    /// when large.
    bool SetClipboard(ClipboardKind kind, const ClipboardOffer& offer) override;
    /// Asks the owner of the selection; see [[os::Window::RequestClipboard]].
    u32 RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name) override;
    /// Hands over a finished result once.
    bool TakeClipboardResult(u32 request_id, ClipboardResult* out) override
    {
        return clipboard_.requests.Take(request_id, out);
    }
    /// Returns whether the selection offers the format (from our own data, or from the targets of the other owner read
    /// at the last `ClipboardChanged`).
    bool ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const override
    {
        return ClipboardHas(display_, clipboard_, kind, format, custom_name);
    }

    /// Returns true on a valid window: XDND version 5.
    bool SupportsDragAndDrop() const override { return display_ != nullptr; }
    /// Sets the accepted drop formats and advertises `XdndAware` (removed when nothing is accepted).
    void SetDropAccept(const DropAccept& accept) override { DndSetAccept(display_, window_, dnd_, accept); }
    /// Answers the drag over the window; an extra `XdndStatus` is sent at once.
    void SetDropAction(DragAction action) override { DndSetAction(display_, window_, dnd_, action); }
    /// Reads one format of the dropped data through `XdndSelection`.
    u32 RequestDropData(ClipboardFormat format, StringView custom_name) override;
    /// Sends `XdndFinished`.
    void FinishDrop(DragAction taken) override { DndFinish(display_, window_, dnd_, taken); }
    /// Starts a drag: grabs the pointer (and keyboard, for Escape), owns `XdndSelection`, and follows the pointer from
    /// `PumpEvents`. The drag image is not drawn on X11.
    bool BeginDrag(const DragSource& source) override;
    /// Cancels the running drag.
    void CancelDrag() override;
    /// Returns whether a drag started by this window is running.
    bool Dragging() const override { return dnd_.source.active; }
    /// The pointer moved to (`root_x`, `root_y`) in root coordinates with the modifier `state` while this window drags;
    /// `PumpEvents` calls it for each grabbed motion. Public so that a test can drive a drag without a pointer. Does
    /// nothing when no drag is running.
    void DragPointerMoved(int root_x, int root_y, unsigned long state);
    /// The button was released while this window drags: drops on the target under the pointer, or ends the drag.
    /// `PumpEvents` calls it; public for the same reason.
    void DragButtonReleased();

    /// Returns the underlying Xlib `Display*`, or null if invalid.
    ///
    /// - Ownership: borrowed; owned by this window and closed with it.
    _XDisplay* NativeDisplay() const { return display_; }
    /// Returns the underlying X window id (an XID), or 0 if invalid.
    ///
    /// - Ownership: borrowed; owned by this window and destroyed with it.
    unsigned long NativeWindow() const { return window_; }

private:
    void ReleaseAll();
    void HandleKey(const void* xkey_event, bool pressed, InputState* input);
    void QueueEvent(InputEvent event);
    void ReleaseAllKeys(InputState* input);

    _XDisplay* display_ = nullptr;
    unsigned long window_ = 0;
    unsigned long wm_delete_window_ = 0;
    _XIM* input_method_ = nullptr;
    _XIC* input_context_ = nullptr;
    InputState* input_ = nullptr;
    u32 width_ = 0;
    u32 height_ = 0;
    bool should_close_ = false;
    // Set by UnmapNotify (iconified or hidden), cleared by MapNotify.
    bool unmapped_ = false;
    u64 resize_serial_ = 0;
    // The Key each held X keycode was *pressed* as, so a release clears
    // that Key even if modifiers changed in between (same rationale as
    // WaylandInput::held_). X keycodes are 8-bit.
    Key held_[256] = {};
    InputEventQueue events_;
    // Whether the window has keyboard focus as far as FocusIn/FocusOut told (grab-induced focus changes do not count).
    bool focused_ = false;
    X11CursorState cursor_;
    X11ClipboardState clipboard_;
    X11DndState dnd_;
};

}  // namespace engine_placeholder::os::x11
