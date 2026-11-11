#pragma once

#include "platform/input.h"
#include "platform/clipboard.h"
#include "platform/cursor.h"
#include "platform/drag_drop.h"
#include "platform/input_event.h"
#include "string/string.h"
#include "types.h"

//! Platform-neutral window interface implemented by each display backend (Wayland and X11 on Linux, Win32 on Windows).
//!
//! The interface is polled: pump events, then read size, close request and the bound `InputState`.

namespace engine_placeholder::os {

/// Which native windowing system a window belongs to, so a graphics layer knows how to read [[os::NativeWindowInfo]].
enum class NativeWindowKind : u8 {
    Invalid = 0, ///< No native window (an invalid or closed window); named to avoid the X11 `None` macro.
    Wayland,  ///< A Wayland surface: `display` is a `wl_display*`, `surface` is a `wl_surface*`.
    Xlib,     ///< An X11 window: `display` is a `Display*`, `window_id` is the `Window` (XID).
    Win32,    ///< A Win32 window: `display` is the `HINSTANCE` of the module that registered the window class, `surface` the `HWND`.
    Cocoa,    ///< Reserved for the macOS port: `surface` is the `NSView*` (or `CAMetalLayer*`).
    Android,  ///< Reserved for the Android port: `surface` is the `ANativeWindow*`.
};

/// The native handles of a window, as a plain value a graphics layer can create a presentation surface from.
///
/// This is how the RHI's swapchain gets at the window without including any platform header: the
/// `kind` says which fields are meaningful, and the pointers are opaque here. Unused fields are null or 0.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: every pointer is borrowed from the window that returned it and is valid until that window is
///   destroyed (or, for Wayland, closed); a graphics surface made from it must be destroyed first.
struct NativeWindowInfo {
    /// The windowing system; decides which of the other fields are meaningful.
    NativeWindowKind kind = NativeWindowKind::Invalid;
    /// The display or connection handle (`wl_display*`, `Display*`, `HINSTANCE`), or null.
    void* display = nullptr;
    /// The surface or window handle (`wl_surface*`, `HWND`, `NSView*`, `ANativeWindow*`), or null.
    void* surface = nullptr;
    /// An integer window identifier, for systems whose windows are not pointers (the X11 `Window`); otherwise 0.
    u64 window_id = 0;
};

/// Abstract window implemented by each platform backend, so upper layers never name a backend.
///
/// Input is delivered into the `InputState` the backend was bound to when the window was opened.
///
/// - Thread-safety: thread-compatible. Calls are not synchronized; use one window from one thread at a time.
/// - Ownership: the window owns its native surface and releases it on destruction.
class Window {
public:
    virtual ~Window() = default;

    /// Returns whether the window was created successfully and is still usable.
    virtual bool Valid() const = 0;

    /// Drains all pending platform events without blocking and updates the bound `InputState`.
    virtual void PumpEvents() = 0;

    /// Removes the oldest queued input event, if any.
    ///
    /// `PumpEvents` queues the events (key down, up and repeat, text, pointer, wheel, focus, resize) in the order the server delivered them, up to
    /// [[os::InputEventQueue::kCapacity]]; call this in a loop after it until it returns false. `InputState` is updated as before and is not affected.
    /// The default queues nothing, for a backend without an event stream.
    /// - Param out: receives the event.
    /// - Returns: true if `out` holds an event.
    virtual bool PollEvent([[maybe_unused]] InputEvent* out) { return false; }

    /// Returns how many events were dropped because the queue was full (a program that does not poll loses the newest events, visibly), since the window opened.
    virtual u64 DroppedEvents() const { return 0; }

    /// Returns true once the user or window manager has asked the window to close.
    virtual bool ShouldClose() const = 0;

    /// Returns the current window width in pixels.
    virtual u32 Width() const = 0;

    /// Returns the current window height in pixels.
    virtual u32 Height() const = 0;

    /// Returns the scale the system asks the window's content to be drawn at: physical pixels per logical unit (1 is 100%, the default).
    ///
    /// Win32 gives the window's DPI over 96, and the value changes when the window moves to a monitor of another DPI (a `Resize` follows).
    /// Wayland and X11 give 1: the Wayland compositor scales the window itself, and X11 has no per-window scale.
    virtual f32 ContentScale() const { return 1.0f; }

    /// Returns the stable lowercase backend id (`"wayland"`, `"x11"`, `"win32"`), for logs and tests.
    /// - Ownership: the string is static and is never freed by the caller.
    virtual const char* BackendName() const = 0;

    /// Returns the native handles a graphics layer needs to create a presentation surface for this window.
    ///
    /// - Returns: the handles; `kind` is `Invalid` and the rest empty if the window is invalid or closed.
    /// - Ownership: the pointers are borrowed from this window; see [[os::NativeWindowInfo]].
    virtual NativeWindowInfo GetNativeInfo() const = 0;

    /// Returns whether there is nothing to draw to: the window is not open, has no area, or is reported hidden.
    ///
    /// A window that is invalid, closed or has lost its connection counts, as does one with zero width or
    /// height. X11 also reports a window the window manager has unmapped or iconified (which includes a window
    /// on a hidden desktop), and Win32 a minimized window. Wayland has no client-visible minimized or occluded
    /// state, so there only the first two cases apply.
    virtual bool IsMinimized() const = 0;

    /// Returns a counter that changes every time the window size changes, so a swapchain can cheaply tell that it is stale.
    ///
    /// The value is opaque: compare it for equality with a value read earlier. Values come from one
    /// process-wide counter, so an open window never reports 0, and a value never repeats across size changes,
    /// windows, or closing and reopening. An invalid or closed window reports 0.
    virtual u64 ResizeSerial() const = 0;

    /// Returns whether [[os::Window::SetCursor]] can show the standard shapes on this window (the default is false).
    virtual bool SupportsCursorShape() const { return false; }

    /// Shows a standard cursor shape while the pointer is over the window. A custom image set by [[os::Window::SetCursorImage]] hides the shape until it is cleared.
    /// - Param shape: the shape; [[os::CursorShape::Hidden]] shows no cursor.
    /// - Returns: true if the shape is in effect or will be as soon as the pointer is over the window; setting the shape already shown is a no-op that returns true.
    /// - Fails: returns false, changing nothing, if the window is invalid, `shape` is out of range or cursor shapes are unsupported (`Hidden` may still work where the other shapes do not).
    virtual bool SetCursor([[maybe_unused]] CursorShape shape) { return false; }

    /// Returns whether [[os::Window::SetCursorImage]] works on this window (the default is false).
    virtual bool SupportsCursorImage() const { return false; }

    /// Shows a custom cursor image; it replaces the shape until [[os::Window::ClearCursorImage]]. The window copies the pixels.
    /// - Param image: a valid image: see [[os::ValidateCursorImage]].
    /// - Returns: true if the image is in effect or will be as soon as the pointer is over the window.
    /// - Fails: returns false, changing nothing, if the image is invalid (too large, hotspot outside, bad scale), the window is invalid or images are unsupported.
    virtual bool SetCursorImage([[maybe_unused]] const CursorImage& image) { return false; }

    /// Removes the custom cursor image; the last shape set by [[os::Window::SetCursor]] (the arrow at first) shows again. A no-op without an image.
    virtual void ClearCursorImage() {}

    /// Returns whether [[os::Window::SetPointerLock]] works on this window (the default is false). On Wayland it depends on the compositor offering the protocols.
    virtual bool SupportsPointerLock() const { return false; }

    /// Asks to lock the pointer: it stops moving, and the program gets [[os::InputEventType::RelativeMotion]] events instead (a camera control).
    ///
    /// The lock holds only while the window has focus: it is released on focus loss and asked for again on focus gain while the program still wants it, and a request made
    /// while unfocused takes effect when focus arrives. A second request for the same state is a no-op. Destroying the window releases the lock.
    /// - Param locked: true to lock, false to release.
    /// - Returns: true if the request was accepted (the lock may take effect a moment later: see [[os::Window::PointerLocked]]).
    /// - Fails: returns false, changing nothing, if the window is invalid or the lock is unsupported.
    virtual bool SetPointerLock([[maybe_unused]] bool locked) { return false; }

    /// Returns whether the pointer is locked right now (the request was granted and focus is held).
    virtual bool PointerLocked() const { return false; }

    /// Returns whether the clipboard of this kind works on this window (the default is false; Windows and macOS have no primary selection).
    virtual bool SupportsClipboard([[maybe_unused]] ClipboardKind kind) const { return false; }

    /// Takes ownership of a selection and offers `offer` to other programs; what was offered before is replaced.
    ///
    /// The window copies the bytes and serves other programs' requests for them from later `PumpEvents` calls, in pieces if they are large, so keep pumping. The contents vanish
    /// with the window (no clipboard manager keeps them). Text is made valid UTF-8 first. On Wayland the compositor accepts the offer only after a recent key press or click in the window.
    /// - Param kind: which selection.
    /// - Param offer: the format and bytes; at most [[os::kMaxClipboardBytes]].
    /// - Returns: true if the selection was taken.
    /// - Fails: returns false, changing nothing, if the window is invalid, the kind is unsupported, the offer is empty or too large, or a custom format has no name.
    virtual bool SetClipboard([[maybe_unused]] ClipboardKind kind, [[maybe_unused]] const ClipboardOffer& offer) { return false; }

    /// Asks the owner of a selection for its contents.
    ///
    /// The answer is asynchronous: a later `PumpEvents` queues a [[os::InputEventType::ClipboardData]] event with the returned id, and [[os::Window::TakeClipboardResult]] returns the bytes. A request nobody
    /// answers ends with `Timeout` after two seconds. Text from other programs is made valid UTF-8; file lists keep only absolute local `file://` entries.
    /// - Param kind: which selection.
    /// - Param format: what to ask for.
    /// - Param custom_name: `Custom`: the MIME type or atom name.
    /// - Returns: the request id (never 0).
    /// - Fails: returns 0 if the window is invalid, the kind is unsupported or too many requests are open.
    virtual u32 RequestClipboard([[maybe_unused]] ClipboardKind kind, [[maybe_unused]] ClipboardFormat format, [[maybe_unused]] StringView custom_name) { return 0; }

    /// Hands over the result of a finished request, once.
    /// - Param request_id: the id from [[os::Window::RequestClipboard]].
    /// - Param out: receives the result.
    /// - Returns: false if there is no unread result for that id (not finished yet, already taken, or dropped because more than a few were left unread).
    virtual bool TakeClipboardResult([[maybe_unused]] u32 request_id, [[maybe_unused]] ClipboardResult* out) { return false; }

    /// Returns whether the selection currently offers a format, as far as the last `ClipboardChanged` told (for enabling a Paste command).
    virtual bool ClipboardHasFormat([[maybe_unused]] ClipboardKind kind, [[maybe_unused]] ClipboardFormat format, [[maybe_unused]] StringView custom_name) const { return false; }

    /// Returns whether this window can take part in drag and drop with other programs (the default is false).
    virtual bool SupportsDragAndDrop() const { return false; }

    /// Sets what drags from other programs the window accepts; the default accepts none.
    ///
    /// - Param accept: the formats wanted; `formats == 0` refuses every drag. The name is copied.
    virtual void SetDropAccept([[maybe_unused]] const DropAccept& accept) {}

    /// Answers the drag over the window with the action the program would take (feeds the cursor the user sees); call it after a `DragEnter` or `DragOver` event.
    ///
    /// Until it is called the backend answers `Copy` when a format matches and the source allows it, else `Nothing`.
    /// - Param action: `Nothing` refuses the drop at this position.
    virtual void SetDropAction([[maybe_unused]] DragAction action) {}

    /// Reads one format of the data being dropped; call it after a `Drop` event.
    ///
    /// The answer is asynchronous like [[os::Window::RequestClipboard]]: a `DropData` event with the returned id, then [[os::Window::TakeClipboardResult]]. The same safety rules apply
    /// (valid UTF-8, local `file://` paths only, a file list cut at [[os::kMaxDropFileEntries]] entries with the rest counted in `rejected_entries`).
    /// - Param format: what to read.
    /// - Param custom_name: `Custom`: the MIME type or atom name.
    /// - Returns: the request id (never 0).
    /// - Fails: returns 0 if no drop is waiting or too many requests are open.
    virtual u32 RequestDropData([[maybe_unused]] ClipboardFormat format, [[maybe_unused]] StringView custom_name) { return 0; }

    /// Tells the source that the drop has been handled (the data was read, or is not wanted). Without it the drop ends by itself after [[os::kDropSessionTimeoutNs]].
    /// - Param taken: what the program did with the data; `Nothing` reports that it was refused.
    virtual void FinishDrop([[maybe_unused]] DragAction taken) {}

    /// Starts dragging data out of the window; the drag follows the pointer until the button is released.
    ///
    /// Call it from the handler of a button press or a pointer move with the button held (Wayland needs that input's serial). The bytes are copied. The end is reported by a
    /// `DragEnded` event.
    /// - Param source: the offered formats, allowed actions and optional image.
    /// - Returns: true if the drag started.
    /// - Fails: returns false if drag and drop is unsupported, a drag is already running, there are no offers or too many, or (X11) the pointer could not be grabbed.
    virtual bool BeginDrag([[maybe_unused]] const DragSource& source) { return false; }

    /// Cancels the drag this window started; a `DragEnded` event with `Nothing` follows.
    virtual void CancelDrag() {}

    /// Returns whether a drag started by this window is running.
    virtual bool Dragging() const { return false; }

    /// Returns whether this window can be made fullscreen (the platform and, where it can be known, the compositor allow it).
    ///
    /// The default is false; a backend that implements [[os::Window::SetFullscreen]] overrides it. A settings layer greys
    /// the `fullscreen` setting out when this is false.
    virtual bool SupportsFullscreen() const { return false; }

    /// Asks for fullscreen on or off.
    ///
    /// The change is a request: the window manager or compositor answers with a new size, which shows up as a change of
    /// [[os::Window::ResizeSerial]] after `PumpEvents`. Some compositors refuse; then nothing changes.
    ///
    /// - Param fullscreen: the state wanted.
    /// - Returns: true if the request was sent.
    /// - Fails: returns false, changing nothing, if the window is invalid or fullscreen is unsupported.
    virtual bool SetFullscreen(bool fullscreen) {
        (void)fullscreen;
        return false;
    }

    /// Returns whether the window is fullscreen as far as the last `PumpEvents` could tell.
    virtual bool IsFullscreen() const { return false; }

    /// A function a window calls while the platform holds the thread in a loop of its own, from which `PumpEvents` cannot return.
    using ModalLoopCallback = void (*)(void* user);

    /// Sets the function to call during the platform's own loops, so a program can keep drawing: on Windows, while the user moves or
    /// resizes the window by its frame (or has its system menu open), about every 10 ms; on Linux there is no such loop and it is never called.
    ///
    /// The default ignores it. A program renders a frame from the callback and reads the window's size there as usual.
    /// - Param callback: the function, or null to stop; it must not close or destroy the window.
    /// - Param user: passed to `callback`.
    virtual void SetModalLoopCallback(ModalLoopCallback callback, void* user) {
        (void)callback;
        (void)user;
    }
};

namespace detail {
/// Returns the next value of the process-wide resize counter shared by all windows; never 0.
u64 NextResizeSerial();
} // namespace detail

/// Selects which display backend to use for a window.
enum class WindowBackend : u8 {
    Auto = 0, ///< The platform's choice: on Linux from the environment, Wayland first; on Windows, Win32.
    Wayland,  ///< Force the Wayland backend (Linux).
    X11,      ///< Force the X11 backend (Linux).
    Win32,    ///< Force the Win32 backend (Windows).
    Invalid,  ///< Unrecognized name, or an empty candidate slot.
};

/// Parses `"auto"`, `"wayland"`, `"x11"` or `"win32"` into a [[os::WindowBackend]], on every platform (the plan says what the platform has).
///
/// Anything else, including an empty string, yields `WindowBackend::Invalid`, so callers can tell an
/// unrecognized name from `"auto"`.
/// - Param name: backend name, case-sensitive.
WindowBackend ParseWindowBackend(StringView name);

/// Ordered list of backends to try when opening a window.
///
/// - Thread-safety: thread-compatible. A plain value type.
struct BackendPlan {
    /// Backends in the order to try; only the first `count` entries are meaningful.
    WindowBackend candidates[2] = {WindowBackend::Invalid, WindowBackend::Invalid};
    /// Number of valid entries in `candidates`.
    usize count = 0;
    /// Why the plan is empty when a backend was asked for by name that the platform does not have (`"wayland is a Linux backend"`); null
    /// otherwise. Ownership: a static string.
    const char* refusal = nullptr;
};

/// Builds the ordered list of backends to try on Linux for the requested backend and environment.
///
/// With `Auto`, Wayland comes first when its environment is set, then X11 when its environment is set, so a
/// Wayland failure falls through to X11. A forced backend yields exactly that one candidate regardless
/// of the environment, with no fallback. `Win32` and `Invalid` yield an empty plan (`Win32` with its `refusal`).
/// - Param requested: the backend asked for.
/// - Param wayland_env_set: whether `$WAYLAND_DISPLAY` is set.
/// - Param x11_env_set: whether `$DISPLAY` is set.
/// - Returns: the plan; `count` is 0 when no backend is eligible.
BackendPlan PlanWindowBackends(WindowBackend requested, bool wayland_env_set, bool x11_env_set);

/// Builds the list of backends to try on Windows: `Auto` and `Win32` yield Win32, `Wayland` and `X11` an empty plan with its `refusal`.
///
/// - Param requested: the backend asked for.
/// - Returns: the plan; `count` is 0 when no backend is eligible.
BackendPlan PlanWin32WindowBackends(WindowBackend requested);

} // namespace engine_placeholder::os
