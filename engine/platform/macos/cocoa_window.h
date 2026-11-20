#pragma once

//! The macOS window backend (Phase 21 Day 295): an `NSWindow` whose content view is backed by a `CAMetalLayer`, run by
//! the program's own loop (`PumpEvents` drains AppKit's event queue). C++ only: the AppKit objects live in
//! `cocoa_window.mm`.
//!
//! AppKit allows windows on the main thread only: [[os::cocoa::CocoaWindow::Open]] refuses any other thread with a log
//! line. The application object (`NSApplication`, a regular, dock-visible app with a Quit menu) is set up once, by the
//! entry point of a windowed program or by the first window a test opens. Cmd-Q, the Dock's Quit and the menu's Quit do
//! not end the process: they make every window's [[os::Window::ShouldClose]] true, as the close button does, so the
//! program decides.
#include "containers/array.h"
#include "containers/unique_ptr.h"
#include "platform/macos/cocoa_keys.h"
#include "platform/clipboard.h"
#include "platform/cursor.h"
#include "platform/drag_drop.h"
#include "platform/input.h"
#include "platform/input_event.h"
#include "platform/window.h"
#include "string_view.h"
#include "types.h"

namespace vse::os::cocoa
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

/// How many `[NSCursor hide]` calls the program's windows have not balanced with `unhide` yet (AppKit counts them, and
/// an unbalanced hide leaves the cursor hidden after the window is gone).
///
/// - Returns: the depth; 0 or 1 in a correct program.
/// - Fails: never.
/// - Thread-safety: main thread only.
i32 CursorHideDepth();

/// The cursor a shape shows on this macOS version (macOS has no diagonal resize cursor before 15 and no four-way one).
///
/// - Param shape: the shape; `Hidden` and out-of-range values give the arrow.
/// - Returns: the `NSCursor*`, as a pointer for C++ callers.
/// - Fails: never.
/// - Thread-safety: main thread only.
void* SystemCursorFor(CursorShape shape);

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
    /// - Param input: the input state the window fills, or null for a private one.
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

    /// True for an open window: the ten shapes are `NSCursor`s ([[os::cocoa::SystemCursorFor]]).
    bool SupportsCursorShape() const override { return native_ != nullptr; }
    /// Shows a shape while the pointer is over the window; `Hidden` is `[NSCursor hide]`, balanced on leave and close.
    bool SetCursor(CursorShape shape) override;
    /// True for an open window.
    bool SupportsCursorImage() const override { return native_ != nullptr; }
    /// An `NSCursor` from the pixels, `width / scale` points wide; an image equal to the one showing is not made again.
    bool SetCursorImage(const CursorImage& image) override;
    /// Back to the shape.
    void ClearCursorImage() override;
    /// True for an open window: the cursor is hidden, warped to the view's centre and detached from the mouse
    /// (`CGAssociateMouseAndMouseCursorPosition`); `RelativeMotion` comes from the events' deltas.
    bool SupportsPointerLock() const override { return native_ != nullptr; }
    /// The [[os::PointerLockState]] rules: held only while the window is key.
    bool SetPointerLock(bool locked) override;
    /// True while locked.
    bool PointerLocked() const override { return lock_.Active(); }

    /// True for `Clipboard` on an open window; macOS has no primary selection.
    bool SupportsClipboard(ClipboardKind kind) const override;
    /// Writes the pasteboard (text, file URLs, or the custom type named by the offer); the system keeps it after the
    /// window closes.
    bool SetClipboard(ClipboardKind kind, const ClipboardOffer& offer) override;
    /// Reads the pasteboard at once; the answer is queued as a `ClipboardData` event before this returns.
    u32 RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name) override;
    /// The result of a finished request, once.
    bool TakeClipboardResult(u32 request_id, ClipboardResult* out) override
    {
        return clipboard_requests_.Take(request_id, out);
    }
    /// Whether the pasteboard has the format now.
    bool ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const override;

    /// True for an open window.
    bool SupportsDragAndDrop() const override { return native_ != nullptr; }
    /// Registers the view for the accepted formats' pasteboard types.
    void SetDropAccept(const DropAccept& accept) override;
    /// The answer to the drag over the window.
    void SetDropAction(DragAction action) override;
    /// Answers from the copy read when the drop happened.
    u32 RequestDropData(ClipboardFormat format, StringView custom_name) override;
    /// Drops that copy.
    void FinishDrop(DragAction taken) override;
    /// `beginDraggingSessionWithItems:event:source:` from the last mouse-down; refused without a button held.
    bool BeginDrag(const DragSource& source) override;
    /// Makes the running drag end with `Nothing` (macOS cannot stop the session: the drop it ends in is refused).
    void CancelDrag() override;
    /// True from `BeginDrag` to the session's end.
    bool Dragging() const override;

    /// Tests only: use this `NSPasteboard*` instead of the general one (a private, uniquely named pasteboard keeps the
    /// user's clipboard untouched); null goes back to the general one.
    void SetPasteboardForTest(void* pasteboard);

    // What the window's delegate and its view (cocoa_window.mm) report; not for programs. The input handlers take the
    // NSEvent's fields as numbers (cocoa_keys.h): `flags` is `modifierFlags`, positions are window pixels from the top
    // left.
    struct Native;
    void OnCloseRequested() { close_requested_ = true; }
    void OnGeometryChanged(bool in_live_resize);
    void OnFocusChanged(bool focused);
    void OnFullscreenTransition(bool started);
    void OnFullscreenChanged(bool fullscreen);
    /// Returns true when the press may type text (neither Control nor Command held), so the view interprets it.
    bool OnKeyDown(u32 key_code, u32 character, u64 flags, bool is_repeat);
    void OnKeyUp(u32 key_code, u64 flags);
    void OnFlagsChanged(u32 key_code, u64 flags);
    void OnText(const char* utf8, usize length);
    void OnPointerMove(f64 x, f64 y, bool inside, u64 flags);
    void OnPointerButton(u32 button, bool down, f64 x, f64 y, u64 flags);
    void OnPointerEnter(f64 x, f64 y, u64 flags);
    void OnPointerLeave(u64 flags);
    void OnWheel(f64 dx_notches, f64 dy_notches, u64 flags);
    void OnRelativeMotion(f64 dx, f64 dy, u64 flags);
    void ApplyCursor();    // shows, hides or sets the cursor the state asks for (cocoa_cursor.mm)
    void ReleaseCursor();  // the window is closing: unhide, unlock
    void OnDragEnter(f64 x, f64 y, u8 offered, u8 allowed);
    void OnDragOver(f64 x, f64 y, u8 allowed);
    void OnDragLeave();
    void OnDrop(f64 x, f64 y, u8 allowed, DragAction action);
    void OnDragEnded(DragAction action);
    DragAction ChooseDropAction() const;
    Native* NativeState() { return native_.Get(); }

private:
    void QueueEvent(InputEvent event);
    void QueueKey(InputEventType type, Key key, bool repeat, u32 modifiers);
    void SetModifiers(u32 modifiers);
    void ReleaseKeys();
    void ApplyLock(PointerLockState::Action action);
    void WarpToCentre();
    void PollPasteboard();
    void DndTick(u64 now_ns);
    bool StillHeld(Key key) const;

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
    Array<Key, kCocoaKeyCodeCount> held_ = {};  // by key code: the Key it was pressed as, Unknown when up
    u32 modifiers_ = 0;                         // the ModifierBit mask after the last key or modifier event
    CursorShape cursor_shape_ = CursorShape::Arrow;
    u64 image_hash_ = 0;          // HashCursorImage of the custom image, 0 without one
    bool cursor_hidden_ = false;  // this window's [NSCursor hide] is in effect
    PointerLockState lock_;
    bool locked_ = false;       // the mouse is detached from the cursor (CGAssociateMouseAndMouseCursorPosition false)
    bool skip_motion_ = false;  // the first motion after the warp carries the warp's distance: dropped
    ClipboardRequests clipboard_requests_;
    i64 pasteboard_count_ = 0;         // the pasteboard's changeCount as last seen (after our own writes too)
    u64 next_pasteboard_poll_ns_ = 0;  // changeCount asks the pasteboard server: at most every 250 ms
    ModalLoopCallback modal_callback_ = nullptr;
    void* modal_user_ = nullptr;
};

}  // namespace vse::os::cocoa
