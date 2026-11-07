#pragma once

#include "containers/array.h"
#include "platform/input.h"
#include "platform/window.h"
#include "string_view.h"
#include "types.h"

//! The Windows window backend (Phase 13 Day 158): an [[os::Window]] over a Win32 top-level window.
//!
//! Sizes are in physical pixels: every engine program is per-monitor DPI aware (version 2, from its manifest; a thread that is not gets the
//! awareness set around the window's creation), so a window on a 150% monitor that asks for 1280x720 gets a 1280x720 client area, and a move
//! to a monitor of another scale keeps the window the same size to the eye (the system's suggested rectangle is applied). Fullscreen is
//! borderless on the window's monitor (`platform/windows/FULLSCREEN.md`). Input is translated on Day 159.

// The Win32 handle types, forward-declared so including this header does not include <windows.h> (`HWND` is `HWND__*`, `HINSTANCE` is
// `HINSTANCE__*` with `STRICT`, the default).
struct HWND__;
struct HINSTANCE__;

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

    /// Creates and shows a top-level window whose client area is `width` x `height` physical pixels.
    ///
    /// Param title: the title bar text (UTF-8; cut at 511 UTF-16 code units).
    /// Param width: client width in pixels (at least 1).
    /// Param height: client height in pixels (at least 1).
    /// Param input: receives keyboard and pointer state (from Day 159), or null; not owned, must outlive the window.
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
    /// Calls `callback` about every 10 ms while Windows runs a move, resize or menu loop of its own (the user dragging the frame).
    ///
    /// Param callback: the function, or null to stop; it must not close or destroy the window. A call to `PumpEvents` of any window from
    ///   it returns at once (the platform's loop is the one pumping), and no window's callback is entered while one runs on the thread.
    /// Param user: passed to `callback`.
    void SetModalLoopCallback(ModalLoopCallback callback, void* user) override;

    /// Returns the window's DPI (96 is 100%), as Windows last reported it; 0 if not open.
    u32 Dpi() const { return hwnd_ != nullptr ? dpi_ : 0; }
    /// Returns the `HWND`, or null if not open.
    ///
    /// Ownership: borrowed; the window owns it.
    HWND__* NativeHandle() const { return hwnd_; }

private:
    struct Proc; // the window procedure (win32_window.cpp), which reaches the private members

    void ApplyClientSize(u32 width, u32 height);
    void UpdateFullscreenState();

    HWND__* hwnd_ = nullptr;
    HINSTANCE__* instance_ = nullptr;
    InputState* input_ = nullptr;
    u32 width_ = 0;
    u32 height_ = 0;
    u32 dpi_ = 96;
    u64 resize_serial_ = 0;
    bool should_close_ = false;
    bool minimized_ = false;
    bool fullscreen_wanted_ = false; // asked for; `fullscreen_` follows once the client area covers the monitor
    bool fullscreen_ = false;
    bool in_modal_loop_ = false;
    ModalLoopCallback modal_callback_ = nullptr;
    void* modal_user_ = nullptr;
    // What leaving fullscreen gives back: the style (`GWL_STYLE`) and the placement (a `WINDOWPLACEMENT`, kept as bytes so this header
    // needs no Windows type; its size is checked in the .cpp).
    i64 saved_style_ = 0;
    Array<byte, 44> saved_placement_ = {};
};

} // namespace engine_placeholder::os::win32
