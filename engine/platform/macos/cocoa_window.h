#pragma once

//! The macOS window backend (Phase 26 Day 311): an `NSWindow` whose content view is backed by a `CAMetalLayer`, run by
//! the program's own loop (`PumpEvents` drains AppKit's event queue). C++ only: the AppKit objects live in
//! `cocoa_window.mm`.
//!
//! AppKit allows windows on the main thread only: [[os::cocoa::CocoaWindow::Open]] refuses any other thread with a log
//! line. The application object (`NSApplication`, a regular, dock-visible app with a Quit menu) is set up once, by the
//! entry point of a windowed program or by the first window a test opens. Cmd-Q, the Dock's Quit and the menu's Quit do
//! not end the process: they make every window's [[os::Window::ShouldClose]] true, as the close button does, so the
//! program decides.
#include "containers/unique_ptr.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/window.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::os::cocoa
{

/// Sets up the application object once: `NSApplication` with the regular activation policy, a menu with Quit (Cmd-Q),
/// `finishLaunching`. Called by [[ENGINE_WINDOWED_PROGRAM]]'s expansion and by the first
/// [[os::cocoa::CocoaWindow::Open]].
///
/// - Returns: true once the application exists; false off the main thread or without a window server (an ssh session).
/// - Fails: false, with the reason logged, off the main thread or without a window server.
/// - Thread-safety: main thread only.
bool EnsureApplication();

/// Whether Quit was chosen (Cmd-Q, the Dock, the menu) since the application started.
///
/// - Returns: true once Quit has been asked for.
/// - Fails: never.
/// - Thread-safety: main thread only.
bool QuitRequested();

/// Withdraws a Quit request, for a program that decided not to quit (the user cancelled at "save changes?"): every
/// window's [[os::Window::ShouldClose]] reports its own close button again, and a logout or shutdown that was waiting
/// for the program is told it stays (and is cancelled).
///
/// - Fails: never.
/// - Thread-safety: main thread only.
void ClearQuitRequest();

/// Whether a Quit's Apple-event reason (`kAEQuitReason`) is the session ending: a logout, restart or shutdown. Those
/// are answered "later" instead of cancelled, so the system waits for the program to end instead of being refused; a
/// plain Quit (Cmd-Q, the Dock, a reason of 0) is cancelled and only asks the windows to close.
///
/// - Param reason: the reason's four-character code, or 0 for none.
/// - Returns: true for a logout, restart or shutdown (asked or forced).
/// - Fails: never.
bool QuitReasonEndsSession(u32 reason);

/// An `NSWindow` as an [[os::Window]] (see the file comment).
///
/// - Thread-safety: main thread only (AppKit).
/// - Ownership: owns its window, view and delegate; destroying it closes the window.
class CocoaWindow final : public Window
{
public:
    CocoaWindow();
    ~CocoaWindow() override;
    CocoaWindow(const CocoaWindow&) = delete;
    CocoaWindow& operator=(const CocoaWindow&) = delete;

    /// Opens a titled, closable, resizable window of `width` by `height` points (the size in pixels is that times the
    /// backing scale), shows it and makes it the key window.
    ///
    /// - Param title: the title, UTF-8.
    /// - Param width: the content width in points.
    /// - Param height: the content height in points.
    /// - Param input: the input state the window fills (Day 312), or null for a private one.
    /// - Returns: true if the window is open.
    /// - Fails: false, logged, off the main thread, without a window server, for a zero size, or when already open.
    bool Open(StringView title, u32 width, u32 height, InputState* input);
    /// Closes the window (no effect when closed).
    void Close();

    /// True while the window is open.
    bool Valid() const override;
    /// Drains AppKit's event queue without waiting and dispatches every event, to every window of the program.
    void PumpEvents() override;
    /// The next queued [[os::InputEvent]], oldest first.
    bool PollEvent(InputEvent* out) override { return events_.Pop(out); }
    /// Events dropped because the queue was full.
    u64 DroppedEvents() const override { return events_.Dropped(); }
    /// True after the close button (or Quit) was used; the window stays open until the program closes it.
    bool ShouldClose() const override;
    /// The content width in pixels.
    u32 Width() const override { return width_; }
    /// The content height in pixels.
    u32 Height() const override { return height_; }
    /// The backing scale factor (2 on a Retina display).
    f32 ContentScale() const override { return scale_; }
    /// `cocoa`.
    const char* BackendName() const override { return "cocoa"; }
    /// `NativeWindowKind::Cocoa`: `display` is the content `NSView*`, `surface` its `CAMetalLayer*`, `window_id` the
    /// window number.
    NativeWindowInfo GetNativeInfo() const override;
    /// True while miniaturized (in the Dock) or of zero size.
    bool IsMinimized() const override;
    /// Bumped on every size or backing-scale change.
    u64 ResizeSerial() const override { return resize_serial_; }
    /// True for an open, titled window.
    bool SupportsFullscreen() const override;
    /// Asks for native fullscreen or back (`toggleFullScreen:`); it animates into its own Space, so
    /// [[os::Window::IsFullscreen]] follows when the change is done.
    bool SetFullscreen(bool fullscreen) override;
    /// True from `windowDidEnterFullScreen` to `windowDidExitFullScreen`.
    bool IsFullscreen() const override { return fullscreen_; }
    /// Called while AppKit runs its own loop (a live resize), so the program can keep drawing.
    void SetModalLoopCallback(ModalLoopCallback callback, void* user) override;

    // What the window's delegate (cocoa_window.mm) reports; not for programs.
    struct Native;
    void OnCloseRequested() { close_requested_ = true; }
    void OnGeometryChanged(bool in_live_resize);
    void OnFocusChanged(bool focused);
    void OnFullscreenTransition(bool started);
    void OnFullscreenChanged(bool fullscreen);

private:
    void QueueEvent(InputEvent event);

    UniquePtr<Native> native_;
    InputState* input_ = nullptr;
    InputState own_input_;
    InputEventQueue events_;
    u32 width_ = 0;
    u32 height_ = 0;
    f32 scale_ = 1.0f;
    u64 resize_serial_ = 0;
    bool close_requested_ = false;
    bool fullscreen_ = false;             // from windowDidEnterFullScreen / windowDidExitFullScreen
    bool fullscreen_wanted_ = false;      // the last SetFullscreen
    bool fullscreen_transition_ = false;  // AppKit is animating: a request waits for the end
    bool focused_ = false;
    ModalLoopCallback modal_callback_ = nullptr;
    void* modal_user_ = nullptr;
};

}  // namespace engine_placeholder::os::cocoa
