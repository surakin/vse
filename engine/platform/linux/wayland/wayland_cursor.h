#pragma once

#include <wayland-client.h>

#include "platform/cursor.h"
#include "platform/input_event.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "types.h"

//! Wayland cursor shapes, custom cursor images and the pointer lock for [[os::wayland::WaylandInput]].

namespace engine_placeholder::os::wayland {

/// Everything about the mouse cursor on one Wayland seat: the standard shapes (`wp_cursor_shape_v1`), a custom image (a `wl_shm` buffer on a cursor surface), and the
/// pointer lock with relative motion (`zwp_pointer_constraints_v1`, `zwp_relative_pointer_v1`).
///
/// The cursor is set with the serial of the last `wl_pointer.enter`, and the compositor forgets it when the pointer leaves, so the current cursor is applied again on
/// every enter. A compositor that lacks a protocol makes the matching operation report false; nothing else changes.
///
/// Thread-safety: not-thread-safe; owner-thread only, on the thread that dispatches the connection.
/// Ownership: owns the shape device, the relative pointer, the locked pointer, the cursor surface and its buffer; borrows the connection and the queue, which must outlive it.
class WaylandCursor {
public:
    /// Attaches the connection (for the globals, the compositor and `wl_shm`) and the queue that receives `RelativeMotion`.
    void Init(WaylandConnection* connection, InputEventQueue* queue);

    /// Sets the window's surface, which the pointer lock is requested on.
    void SetSurface(wl_surface* surface) { surface_ = surface; }

    /// Creates the per-pointer objects (shape device, relative pointer) for a new `wl_pointer`.
    void AttachPointer(wl_pointer* pointer);

    /// Destroys the per-pointer objects and releases the lock; the cursor choices are kept for the next pointer.
    void DetachPointer();

    /// Records the serial of a `wl_pointer.enter` and applies the current cursor.
    void OnPointerEnter(u32 serial);

    /// Tells the lock the window gained or lost keyboard focus.
    void OnFocus(bool focused);

    /// Returns whether the compositor offers standard cursor shapes and there is a pointer.
    bool SupportsShape() const { return shape_device_ != nullptr; }
    /// Shows a standard shape; [[os::CursorShape::Hidden]] works whenever there is a pointer. Returns false if unsupported or out of range.
    bool SetShape(CursorShape shape);

    /// Returns whether there is a pointer to set an image on.
    bool SupportsImage() const { return pointer_ != nullptr; }
    /// Shows a custom image; returns false for an invalid image or when there is no pointer.
    bool SetImage(const CursorImage& image);
    /// Removes the custom image and frees its buffer and surface.
    void ClearImage();

    /// Returns whether the compositor offers the lock protocols and there is a pointer.
    bool SupportsLock() const { return pointer_ != nullptr && connection_ != nullptr && connection_->PointerConstraints() != nullptr && relative_pointer_ != nullptr; }
    /// Asks for the lock on or off (see [[os::Window::SetPointerLock]]). Returns false if unsupported.
    bool SetLock(bool locked);
    /// Returns whether the compositor has granted the lock.
    bool Locked() const { return lock_.Active(); }

    /// Returns how many shm buffers the cursor owns (0 or 1), so a test can check that images do not leak.
    u32 BufferCount() const { return buffer_ != nullptr ? 1u : 0u; }

    /// Releases every object. Idempotent; must run while the display is connected.
    void Shutdown();

    /// Returns whether every event of the relative pointer and locked pointer has a listener function (libwayland aborts on a null slot).
    static bool ListenersComplete();

private:
    static void HandleRelativeMotion(void* data, zwp_relative_pointer_v1* pointer, u32 utime_hi, u32 utime_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel,
                                     wl_fixed_t dy_unaccel);
    static void HandleLocked(void* data, zwp_locked_pointer_v1* locked);
    static void HandleUnlocked(void* data, zwp_locked_pointer_v1* locked);
    static const zwp_relative_pointer_v1_listener kRelativeListener;
    static const zwp_locked_pointer_v1_listener kLockedListener;

    void Reapply();
    void DoLockAction(PointerLockState::Action action);
    void DestroyLockedPointer();

    WaylandConnection* connection_ = nullptr;
    InputEventQueue* queue_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wp_cursor_shape_device_v1* shape_device_ = nullptr;
    zwp_relative_pointer_v1* relative_pointer_ = nullptr;
    zwp_locked_pointer_v1* locked_pointer_ = nullptr;
    wl_surface* cursor_surface_ = nullptr;
    wl_buffer* buffer_ = nullptr;
    u64 image_hash_ = 0;
    i32 hotspot_x_ = 0;
    i32 hotspot_y_ = 0;
    u32 scale_ = 1;
    u32 enter_serial_ = 0;
    bool touched_ = false; // the program chose a cursor: until then the compositor's default stays
    bool image_active_ = false;
    CursorShape shape_ = CursorShape::Arrow;
    PointerLockState lock_;
};

} // namespace engine_placeholder::os::wayland
