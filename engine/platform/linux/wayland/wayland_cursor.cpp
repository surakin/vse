#include "platform/linux/wayland/wayland_cursor.h"

#include <sys/mman.h>
#include <unistd.h>

#include "log/log.h"
#include "os/os.h"

namespace vse::os::wayland
{

namespace
{

wp_cursor_shape_device_v1_shape WireShape(CursorShape shape)
{
    switch (shape)
    {
    case CursorShape::IBeam: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
    case CursorShape::Hand: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    case CursorShape::ResizeHorizontal: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
    case CursorShape::ResizeVertical: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
    case CursorShape::ResizeDiagonalNWSE: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;
    case CursorShape::ResizeDiagonalNESW: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;
    case CursorShape::ResizeAll: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL;
    case CursorShape::NotAllowed: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
    default: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
}

// Builds an ARGB8888 premultiplied wl_shm buffer from RGBA bytes. Returns null on failure.
wl_buffer* MakeCursorBuffer(wl_shm* shm, const CursorImage& image)
{
    return CreateArgbShmBuffer(shm, image.rgba, image.width, image.height);
}

}  // namespace

wl_buffer* CreateArgbShmBuffer(wl_shm* shm, const byte* rgba, u32 width, u32 height)
{
    CursorImage image;
    image.rgba = rgba;
    image.width = width;
    image.height = height;
    const usize size = static_cast<usize>(image.width) * image.height * 4;
    const int fd = ::memfd_create("engine_wayland_cursor", MFD_CLOEXEC);
    if (fd < 0)
    {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: memfd_create failed for a cursor image");
        return nullptr;
    }
    if (::ftruncate(fd, static_cast<long>(size)) != 0)
    {
        ::close(fd);
        return nullptr;
    }
    void* data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        ::close(fd);
        return nullptr;
    }
    u32* out = static_cast<u32*>(data);
    const usize count = static_cast<usize>(image.width) * image.height;
    for (usize i = 0; i < count; ++i)
    {
        const byte* p = image.rgba + i * 4;
        out[i] = (static_cast<u32>(p[3]) << 24) | (static_cast<u32>(p[0]) << 16) | (static_cast<u32>(p[1]) << 8) | p[2];
    }
    ::munmap(data, size);
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<i32>(size));
    ::close(fd);
    if (pool == nullptr)
    {
        return nullptr;
    }
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<i32>(image.width),
                                                  static_cast<i32>(image.height), static_cast<i32>(image.width * 4),
                                                  WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return buffer;
}

const zwp_relative_pointer_v1_listener WaylandCursor::kRelativeListener = {
    .relative_motion = WaylandCursor::HandleRelativeMotion,
};

const zwp_locked_pointer_v1_listener WaylandCursor::kLockedListener = {
    .locked = WaylandCursor::HandleLocked,
    .unlocked = WaylandCursor::HandleUnlocked,
};

bool WaylandCursor::ListenersComplete()
{
    return kRelativeListener.relative_motion != nullptr && kLockedListener.locked != nullptr &&
           kLockedListener.unlocked != nullptr;
}

void WaylandCursor::Init(WaylandConnection* connection, InputEventQueue* queue)
{
    connection_ = connection;
    queue_ = queue;
}

void WaylandCursor::AttachPointer(wl_pointer* pointer)
{
    pointer_ = pointer;
    if (connection_ == nullptr || pointer == nullptr)
    {
        return;
    }
    if (connection_->CursorShapeManager() != nullptr && shape_device_ == nullptr)
    {
        shape_device_ = wp_cursor_shape_manager_v1_get_pointer(connection_->CursorShapeManager(), pointer);
    }
    if (connection_->RelativePointerManager() != nullptr && relative_pointer_ == nullptr)
    {
        relative_pointer_ = zwp_relative_pointer_manager_v1_get_relative_pointer(connection_->RelativePointerManager(),
                                                                                 pointer);
        if (relative_pointer_ != nullptr)
        {
            zwp_relative_pointer_v1_add_listener(relative_pointer_, &kRelativeListener, this);
        }
    }
}

void WaylandCursor::DestroyLockedPointer()
{
    if (locked_pointer_ != nullptr)
    {
        zwp_locked_pointer_v1_destroy(locked_pointer_);
        locked_pointer_ = nullptr;
    }
}

void WaylandCursor::DetachPointer()
{
    DestroyLockedPointer();
    lock_.Cleared();
    if (relative_pointer_ != nullptr)
    {
        zwp_relative_pointer_v1_destroy(relative_pointer_);
        relative_pointer_ = nullptr;
    }
    if (shape_device_ != nullptr)
    {
        wp_cursor_shape_device_v1_destroy(shape_device_);
        shape_device_ = nullptr;
    }
    pointer_ = nullptr;
}

void WaylandCursor::OnPointerEnter(u32 serial)
{
    enter_serial_ = serial;
    Reapply();
}

void WaylandCursor::Reapply()
{
    if (pointer_ == nullptr || enter_serial_ == 0 || !touched_)
    {
        return;
    }
    if (image_active_ && cursor_surface_ != nullptr)
    {
        wl_pointer_set_cursor(pointer_, enter_serial_, cursor_surface_, hotspot_x_ / static_cast<i32>(scale_),
                              hotspot_y_ / static_cast<i32>(scale_));
    }
    else if (shape_ == CursorShape::Hidden)
    {
        wl_pointer_set_cursor(pointer_, enter_serial_, nullptr, 0, 0);
    }
    else if (shape_device_ != nullptr)
    {
        wp_cursor_shape_device_v1_set_shape(shape_device_, enter_serial_, WireShape(shape_));
    }
}

bool WaylandCursor::SetShape(CursorShape shape)
{
    if (static_cast<usize>(shape) >= static_cast<usize>(CursorShape::Count))
    {
        return false;
    }
    if (shape_device_ == nullptr && !(shape == CursorShape::Hidden && pointer_ != nullptr))
    {
        return false;
    }
    shape_ = shape;
    touched_ = true;
    Reapply();
    return true;
}

bool WaylandCursor::SetImage(const CursorImage& image)
{
    if (ValidateCursorImage(image) != CursorImageError::Ok || pointer_ == nullptr || connection_ == nullptr)
    {
        return false;
    }
    const u64 hash = HashCursorImage(image);
    if (buffer_ == nullptr || hash != image_hash_)
    {
        wl_buffer* buffer = MakeCursorBuffer(connection_->Shm(), image);
        if (buffer == nullptr)
        {
            return false;
        }
        if (cursor_surface_ == nullptr)
        {
            cursor_surface_ = wl_compositor_create_surface(connection_->Compositor());
        }
        if (cursor_surface_ == nullptr)
        {
            wl_buffer_destroy(buffer);
            return false;
        }
        if (buffer_ != nullptr)
        {
            wl_buffer_destroy(buffer_);
        }
        buffer_ = buffer;
        image_hash_ = hash;
        scale_ = image.scale;
        hotspot_x_ = image.hotspot_x;
        hotspot_y_ = image.hotspot_y;
        wl_surface_set_buffer_scale(cursor_surface_, static_cast<i32>(scale_));
        wl_surface_attach(cursor_surface_, buffer_, 0, 0);
        wl_surface_damage_buffer(cursor_surface_, 0, 0, static_cast<i32>(image.width), static_cast<i32>(image.height));
        wl_surface_commit(cursor_surface_);
    }
    image_active_ = true;
    touched_ = true;
    Reapply();
    return true;
}

void WaylandCursor::ClearImage()
{
    if (!image_active_ && buffer_ == nullptr)
    {
        return;
    }
    image_active_ = false;
    if (buffer_ != nullptr)
    {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
    image_hash_ = 0;
    if (cursor_surface_ != nullptr)
    {
        // Show the shape again first, so the compositor stops using the surface before it goes.
        Reapply();
        wl_surface_destroy(cursor_surface_);
        cursor_surface_ = nullptr;
    }
}

void WaylandCursor::DoLockAction(PointerLockState::Action action)
{
    if (action == PointerLockState::Action::Acquire)
    {
        if (!SupportsLock() || surface_ == nullptr)
        {
            lock_.Failed();
            return;
        }
        locked_pointer_ = zwp_pointer_constraints_v1_lock_pointer(connection_->PointerConstraints(), surface_, pointer_,
                                                                  nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        if (locked_pointer_ == nullptr)
        {
            lock_.Failed();
            return;
        }
        zwp_locked_pointer_v1_add_listener(locked_pointer_, &kLockedListener, this);
    }
    else if (action == PointerLockState::Action::Release)
    {
        DestroyLockedPointer();
    }
}

bool WaylandCursor::SetLock(bool locked)
{
    if (!SupportsLock())
    {
        return false;
    }
    DoLockAction(lock_.SetWanted(locked));
    return true;
}

void WaylandCursor::OnFocus(bool focused)
{
    DoLockAction(lock_.SetFocused(focused));
}

void WaylandCursor::HandleLocked(void* data, zwp_locked_pointer_v1*)
{
    static_cast<WaylandCursor*>(data)->lock_.Granted();
}

void WaylandCursor::HandleUnlocked(void* data, zwp_locked_pointer_v1*)
{
    auto* self = static_cast<WaylandCursor*>(data);
    // A oneshot lock is inert once unlocked (the compositor broke it, or the surface lost focus): destroy it and wait
    // for the next focus change.
    self->DestroyLockedPointer();
    self->lock_.Lost();
}

void WaylandCursor::HandleRelativeMotion(void* data, zwp_relative_pointer_v1*, u32, u32, wl_fixed_t, wl_fixed_t,
                                         wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    auto* self = static_cast<WaylandCursor*>(data);
    if (!self->lock_.Active() || self->queue_ == nullptr)
    {
        return;
    }
    InputEvent event;
    event.type = InputEventType::RelativeMotion;
    event.x = wl_fixed_to_double(dx_unaccel);
    event.y = wl_fixed_to_double(dy_unaccel);
    event.time_ns = MonotonicTimeNanos();
    self->queue_->Push(event);
}

void WaylandCursor::Shutdown()
{
    DetachPointer();
    if (buffer_ != nullptr)
    {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
    if (cursor_surface_ != nullptr)
    {
        wl_surface_destroy(cursor_surface_);
        cursor_surface_ = nullptr;
    }
    image_active_ = false;
    image_hash_ = 0;
    surface_ = nullptr;
}

}  // namespace vse::os::wayland
