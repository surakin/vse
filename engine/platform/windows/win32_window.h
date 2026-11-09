#pragma once

#include "containers/array.h"
#include "platform/cursor.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/window.h"
#include "platform/windows/win32_keys.h"
#include "string_view.h"
#include "types.h"

//! The Windows window backend (Phase 13 Day 158): an [[os::Window]] over a Win32 top-level window.
//!
//! Sizes are in physical pixels: every engine program is per-monitor DPI aware (version 2, from its manifest; a thread that is not gets the
//! awareness set around the window's creation), so a window on a 150% monitor that asks for 1280x720 gets a 1280x720 client area, and a move
//! to a monitor of another scale keeps the window the same size to the eye (the system's suggested rectangle is applied). Fullscreen is
//! borderless on the window's monitor (`platform/windows/FULLSCREEN.md`). Input fills the bound [[os::InputState]] with the meaning the Linux backends
//! give it (`platform/windows/win32_keys.h` for the keys; the guide's "Windows input" for the rest).

// The Win32 handle types, forward-declared so including this header does not include <windows.h> (`HWND` is `HWND__*`, `HINSTANCE` is
// `HINSTANCE__*` with `STRICT`, the default).
struct HWND__;
struct HINSTANCE__;
struct HICON__; // HCURSOR is HICON


namespace engine_placeholder::os::win32 {

/// A top-level Win32 window, as an [[os::Window]].
///
/// The window procedure finds the object through the window's user data, so the object cannot move: it is neither copyable nor movable,
/// and a holder keeps it in place (the window host holds it by value).
///
/// Thread-safety: owner-thread-only. Open, pump and destroy it on one thread: Windows delivers a window's messages to the thread that made it.
/// Ownership: owns the `HWND`; destroying the object destroys the window. The window class is registered once per process and never
///   unregistered (the system removes it at exit), so no window can outlive its class.
class Win32Window final : public Window {
public:
    /// An invalid window that owns nothing; call [[os::win32::Win32Window::Open]].
    Win32Window() = default;
    /// Destroys the window, if one is open.
    ~Win32Window() override;
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    /// Creates and shows a top-level window whose client area is `width` x `height` logical pixels: pixels at 100% scaling, multiplied
    /// by the monitor's DPI over 96 ([[os::win32::Win32Window::ScaleForDpi]]), so a window is as large on a high-DPI display as on any
    /// other. [[os::Window::Width]] and `Height` report the physical pixels, which is what a swapchain needs.
    ///
    /// Param title: the title bar text (UTF-8; cut at 511 UTF-16 code units).
    /// Param width: client width in logical pixels (at least 1).
    /// Param height: client height in logical pixels (at least 1).
    /// Param input: receives keyboard and pointer state, or null to ignore input; not owned, must outlive the window.
    /// Returns: true if the window opened.
    /// Fails: returns false (logged on `LogPlatform`), leaving the object invalid, if it is already open, a size is 0, or the class or the
    ///   window cannot be created.
    bool Open(StringView title, u32 width, u32 height, InputState* input);

    /// Destroys the window now (as the destructor does); the object is invalid afterwards and can be opened again.
    void Close();

    /// Returns whether the window is open.
    bool Valid() const override { return hwnd_ != nullptr; }
    /// Removes and dispatches every message waiting for this thread, without blocking.
    ///
    /// Messages of every window on the thread are dispatched (each reaches its own window); `WM_QUIT` counts as a close request.
    void PumpEvents() override;
    /// Returns true once the window was asked to close (`WM_CLOSE`: the close button, Alt+F4, the taskbar). The window stays open until
    /// its owner closes it.
    bool ShouldClose() const override { return should_close_; }
    /// Returns the client width in physical pixels (kept while minimized).
    u32 Width() const override { return width_; }
    /// Returns the client height in physical pixels (kept while minimized).
    u32 Height() const override { return height_; }
    /// Returns the backend id, `"win32"`.
    const char* BackendName() const override { return "win32"; }
    /// Returns kind `Win32`, the `HINSTANCE` of the module that registered the window class and the `HWND`; `Invalid` if the window is not open.
    NativeWindowInfo GetNativeInfo() const override;
    /// Returns whether the window is not open, minimized, or has no client area.
    bool IsMinimized() const override { return hwnd_ == nullptr || minimized_ || width_ == 0 || height_ == 0; }
    /// Returns a counter that changes on every client size change and when the window leaves the minimized state; 0 if not open.
    u64 ResizeSerial() const override { return hwnd_ != nullptr ? resize_serial_ : 0; }
    /// Returns true for an open window.
    bool SupportsFullscreen() const override { return hwnd_ != nullptr; }
    /// Makes the window borderless over its whole monitor, or gives back the style and placement it had before.
    ///
    /// A minimized window is restored first.
    ///
    /// Param fullscreen: the state wanted.
    /// Returns: true if the change was made (or the window is already in that state).
    /// Fails: returns false, changing nothing, if the window is not open.
    bool SetFullscreen(bool fullscreen) override;
    /// Returns whether the window is fullscreen: asked for, and its client area covers its monitor.
    bool IsFullscreen() const override { return hwnd_ != nullptr && fullscreen_; }
    /// Removes the oldest queued input event (Phase 15 Day 205): keys, text from `WM_CHAR`, the pointer, buttons, the wheel, focus and resize,
    /// in the order the window procedure saw them (`platform/BACKEND.md`).
    ///
    /// Param out: receives the event.
    /// Returns: true if an event was removed.
    bool PollEvent(InputEvent* out) override { return events_.Pop(out); }
    /// Returns how many events were dropped because the queue was full.
    u64 DroppedEvents() const override { return events_.Dropped(); }
    /// Returns true for an open window: the ten shapes are the system cursors (`LoadCursor`).
    bool SupportsCursorShape() const override { return hwnd_ != nullptr; }
    /// Shows a standard shape while the pointer is over the client area (applied on `WM_SETCURSOR`, and at once when the pointer is inside).
    ///
    /// Param shape: the shape; `Hidden` shows none.
    /// Returns: true if the shape is set.
    /// Fails: returns false, changing nothing, if the window is not open or `shape` is out of range.
    bool SetCursor(CursorShape shape) override;
    /// Returns true for an open window.
    bool SupportsCursorImage() const override { return hwnd_ != nullptr; }
    /// Shows a custom image, made with `CreateIconIndirect` from a 32-bit top-down DIB (the premultiplied pixels made straight, as Windows
    /// draws cursor alpha). The image keeps its pixel size: Windows does not scale a cursor by the window's DPI, so `scale` is not applied.
    ///
    /// Param image: a valid image ([[os::ValidateCursorImage]]); an image equal to the one showing is not made again.
    /// Returns: true if the image shows (or will, once the pointer is over the window).
    /// Fails: returns false, changing nothing, if the window is not open, the image is invalid or the system cannot make the cursor.
    bool SetCursorImage(const CursorImage& image) override;
    /// Removes the custom image and destroys its cursor; the last shape shows again.
    void ClearCursorImage() override;
    /// Returns true for an open window: the lock is `ClipCursor` to the client area's centre, a hidden cursor and raw mouse input.
    bool SupportsPointerLock() const override { return hwnd_ != nullptr; }
    /// Asks for the pointer lock on or off; the rules are [[os::PointerLockState]] (held only while focused). While locked, `WM_INPUT` relative
    /// mouse motion arrives as `RelativeMotion` events.
    ///
    /// Param locked: true to lock.
    /// Returns: true if the request was accepted.
    /// Fails: returns false if the window is not open.
    bool SetPointerLock(bool locked) override;
    /// Returns whether the lock is in effect now.
    bool PointerLocked() const override { return lock_.Active(); }
    /// Returns how many cursor objects this window made and has not destroyed yet (0 or 1), so a test can see that replacing images does not leak.
    u32 CursorObjects() const { return image_cursor_ != nullptr ? 1u : 0u; }
    /// Calls `callback` about every 10 ms while Windows runs a move, resize or menu loop of its own (the user dragging the frame).
    ///
    /// Param callback: the function, or null to stop; it must not close or destroy the window. A call to `PumpEvents` of any window from
    ///   it returns at once (the platform's loop is the one pumping), and no window's callback is entered while one runs on the thread.
    /// Param user: passed to `callback`.
    void SetModalLoopCallback(ModalLoopCallback callback, void* user) override;

    /// Returns the window's DPI (96 is 100%), as Windows last reported it; 0 if not open.
    u32 Dpi() const { return hwnd_ != nullptr ? dpi_ : 0; }

    /// Converts a size in logical pixels to physical pixels at a DPI, rounding to nearest, never below 1.
    ///
    /// Param logical: the size at 100% scaling.
    /// Param dpi: the DPI (96 is 100%); 0 counts as 96.
    /// Returns: the size in physical pixels.
    /// Thread-safety: thread-safe. A pure function.
    static u32 ScaleForDpi(u32 logical, u32 dpi);
    /// Returns the `HWND`, or null if not open.
    ///
    /// Ownership: borrowed; the window owns it.
    HWND__* NativeHandle() const { return hwnd_; }

private:
    struct Proc; // the window procedure (win32_window.cpp), which reaches the private members

    void ApplyClientSize(u32 width, u32 height);
    void UpdateFullscreenState();
    void QueueEvent(InputEvent event); // stamps the time and queues
    void ApplyCursor();                 // shows the cursor the state asks for (the system's SetCursor)
    void ApplyLock(PointerLockState::Action action);
    void ClipToCentre();                // keeps the locked pointer at the client area's centre

    HWND__* hwnd_ = nullptr;
    HINSTANCE__* instance_ = nullptr;
    InputState* input_ = nullptr;   // the caller's, or own_input_ when it gave none (the events need the key and modifier tracking either way)
    InputState own_input_;
    InputEventQueue events_;
    u16 high_surrogate_ = 0;        // the first half of a character outside the BMP, from the WM_CHAR before
    bool focused_ = false;
    // The cursor and the lock (Day 205).
    CursorShape cursor_shape_ = CursorShape::Arrow;
    HICON__* image_cursor_ = nullptr; // the custom image's cursor, owned
    u64 image_hash_ = 0;
    PointerLockState lock_;
    bool raw_input_ = false; // raw mouse input is registered for this window
    u32 width_ = 0;
    u32 height_ = 0;
    u32 dpi_ = 96;
    u64 resize_serial_ = 0;
    bool should_close_ = false;
    bool minimized_ = false;
    bool fullscreen_wanted_ = false; // asked for; `fullscreen_` follows once the client area covers the monitor
    bool fullscreen_ = false;
    bool in_modal_loop_ = false;
    // Input: the Key each physical key (Win32KeyIndex) was pressed as, so a release clears that Key even if the modifiers changed
    // (as on X11); whether AltGr's synthetic left Ctrl is held; whether a WM_MOUSELEAVE was asked for.
    Array<Key, kWin32KeyIndexCount> held_ = {};
    bool altgr_ctrl_held_ = false;
    bool tracking_leave_ = false;
    ModalLoopCallback modal_callback_ = nullptr;
    void* modal_user_ = nullptr;
    // What leaving fullscreen gives back: the style (`GWL_STYLE`) and the placement (a `WINDOWPLACEMENT`, kept as bytes so this header
    // needs no Windows type; its size is checked in the .cpp).
    i64 saved_style_ = 0;
    Array<byte, 44> saved_placement_ = {};
};

} // namespace engine_placeholder::os::win32
