#pragma once

//! A window the host program owns: the engine renders into it and never creates, resizes or destroys
//! it. The host gives its native handles (an X11 `Display*` and `Window`, a Wayland `wl_display*` and `wl_surface*`)
//! and tells the engine the size and scale whenever they change; the RHI makes its presentation surface from the
//! handles, on the host's own connection (the engine opens none of its own).
#include "containers/vector.h"
#include "platform/clipboard.h"
#include "platform/cursor.h"
#include "platform/input_event.h"
#include "platform/window.h"
#include "types.h"

namespace vse::os
{

/// What the host does for the engine's requests on a window the host owns: the cursor, the pointer lock, the clipboard,
/// the text-input rectangle and the focus. Every method has a refusing default, so a host implements what it can.
///
/// - Thread-safety: thread-compatible; called on the thread that drives the window (the host's frame thread).
/// - Ownership: owned by the caller of `ForeignWindow::SetHooks`, which keeps it alive until it sets another or null.
class ForeignWindowHooks
{
public:
    virtual ~ForeignWindowHooks() = default;
    /// Whether `SetCursor` does anything.
    virtual bool SupportsCursor() const { return false; }
    /// Sets the cursor shape on the host's window (`Hidden` hides it).
    ///
    /// - Returns: false when the host refuses.
    virtual bool SetCursor(CursorShape shape)
    {
        (void)shape;
        return false;
    }
    /// Whether `SetPointerLock` does anything.
    virtual bool SupportsPointerLock() const { return false; }
    /// Locks or unlocks the pointer on the host's window.
    virtual bool SetPointerLock(bool locked)
    {
        (void)locked;
        return false;
    }
    /// Whether the host has a text clipboard.
    virtual bool SupportsClipboard() const { return false; }
    /// Puts UTF-8 text on the host's clipboard.
    virtual bool SetClipboardText(StringView text)
    {
        (void)text;
        return false;
    }
    /// Reads the host's clipboard text into `out` (UTF-8; replaced).
    ///
    /// - Returns: false when the clipboard has no text.
    virtual bool GetClipboardText(Vector<byte>* out)
    {
        (void)out;
        return false;
    }
    /// Tells the host where the text caret is, in window pixels, so it can place its input method's popup.
    virtual void SetTextInputRect(i32 x, i32 y, i32 width, i32 height)
    {
        (void)x;
        (void)y;
        (void)width;
        (void)height;
    }
    /// Asks the host to give (true) or take (false) keyboard focus for the engine's view.
    virtual void RequestFocus(bool focused) { (void)focused; }
};

/// A [[os::Window]] over native handles the host owns.
///
/// It has no events, no cursor and no clipboard (the host owns input and forwards it). `Valid` is true until the
/// host calls `Close`.
///
/// - Thread-safety: thread-compatible, like every window: the host's frame thread calls `SetSize` and the engine's
///   frame runs on the same thread.
/// - Ownership: borrows the handles in `info`; the host keeps them valid until the engine's surface is destroyed.
class ForeignWindow final : public Window
{
public:
    /// Describes the host's window.
    ///
    /// - Param info: the native handles (`kind` Wayland or Xlib).
    /// - Param width: the drawable width in pixels.
    /// - Param height: the drawable height in pixels.
    /// - Param scale: the content scale (1.0 on a standard display).
    ForeignWindow(const NativeWindowInfo& info, u32 width, u32 height, f32 scale);

    /// Tells the window its new size; a change bumps `ResizeSerial`.
    ///
    /// - Param width: the drawable width in pixels; 0 reads as minimized.
    /// - Param height: the drawable height in pixels.
    /// - Param scale: the content scale.
    /// - Fails: never.
    void SetSize(u32 width, u32 height, f32 scale);

    /// Points the window at new native handles (the host re-created its window) and sets its size; `Valid` becomes
    /// true again and `ResizeSerial` changes.
    ///
    /// - Param info: the native handles.
    /// - Fails: never.
    void Rebind(const NativeWindowInfo& info, u32 width, u32 height, f32 scale);

    /// Marks the window gone (the host is closing it): `Valid` becomes false.
    ///
    /// - Fails: never.
    void Close() { valid_ = false; }

    /// Sets who answers the engine's cursor, pointer lock, clipboard, text-input and focus requests (null: none).
    ///
    /// - Param hooks: borrowed.
    void SetHooks(ForeignWindowHooks* hooks) { hooks_ = hooks; }

    /// Queues an input event the host reported, stamped by the caller; a full queue drops it and counts it.
    ///
    /// - Returns: false when the event was dropped.
    bool PushEvent(const InputEvent& event) { return events_.Push(event); }

    /// Tells the host where the text caret is (forwards to the hooks).
    void SetTextInputRect(i32 x, i32 y, i32 width, i32 height);

    /// Asks the host to give or take keyboard focus (forwards to the hooks).
    void RequestFocus(bool focused);

    bool PollEvent(InputEvent* out) override { return events_.Pop(out); }
    u64 DroppedEvents() const override { return events_.Dropped(); }
    bool SupportsCursorShape() const override { return hooks_ != nullptr && hooks_->SupportsCursor(); }
    bool SetCursor(CursorShape shape) override { return hooks_ != nullptr && hooks_->SetCursor(shape); }
    bool SupportsPointerLock() const override { return hooks_ != nullptr && hooks_->SupportsPointerLock(); }
    bool SetPointerLock(bool locked) override;
    bool PointerLocked() const override { return locked_; }
    bool SupportsClipboard(ClipboardKind kind) const override;
    bool SetClipboard(ClipboardKind kind, const ClipboardOffer& offer) override;
    u32 RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name) override;
    bool TakeClipboardResult(u32 request_id, ClipboardResult* out) override { return requests_.Take(request_id, out); }
    bool ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const override;

    bool Valid() const override { return valid_; }
    void PumpEvents() override {}
    bool ShouldClose() const override { return !valid_; }
    u32 Width() const override { return width_; }
    u32 Height() const override { return height_; }
    f32 ContentScale() const override { return scale_; }
    const char* BackendName() const override { return "foreign"; }
    NativeWindowInfo GetNativeInfo() const override { return valid_ ? info_ : NativeWindowInfo(); }
    bool IsMinimized() const override { return !valid_ || width_ == 0 || height_ == 0; }
    u64 ResizeSerial() const override { return valid_ ? serial_ : 0; }

private:
    NativeWindowInfo info_;
    u32 width_;
    u32 height_;
    f32 scale_;
    u64 serial_;
    bool valid_ = true;
    bool locked_ = false;
    ForeignWindowHooks* hooks_ = nullptr;
    InputEventQueue events_;
    ClipboardRequests requests_;
};

}  // namespace vse::os
