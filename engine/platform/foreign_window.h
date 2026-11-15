#pragma once

//! A window the host program owns (Phase 23 Day 352): the engine renders into it and never creates, resizes or destroys
//! it. The host gives its native handles (an X11 `Display*` and `Window`, a Wayland `wl_display*` and `wl_surface*`)
//! and tells the engine the size and scale whenever they change; the RHI makes its presentation surface from the
//! handles, on the host's own connection (the engine opens none of its own).
#include "platform/window.h"
#include "types.h"

namespace engine_placeholder::os
{

/// A [[os::Window]] over native handles the host owns.
///
/// It has no events, no cursor and no clipboard (the host owns input; Day 353 forwards it). `Valid` is true until the
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
};

}  // namespace engine_placeholder::os
