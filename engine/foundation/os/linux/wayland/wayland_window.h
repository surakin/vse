#pragma once

#include <wayland-client.h>

#include "os/linux/wayland/generated/xdg-shell-client-protocol.h"
#include "os/linux/wayland/wayland_connection.h"
#include "string/string.h"
#include "types.h"

// Phase 1 Day 19: wraps a wl_surface as an xdg_shell toplevel window and
// gets it actually mapped/visible. Per the xdg-shell spec, a surface
// isn't mapped until a buffer is attached and committed *after* the
// first configure/ack_configure round-trip — so this also creates a
// minimal solid-fill wl_shm buffer to complete that sequence. Real pixel
// content comes from Vulkan's swapchain once Phase 3 exists; this fill
// exists only to satisfy "a window appears on screen" for this day's
// milestone. Input (Days 21-22) and the event loop (Day 20) are separate
// — this type only owns the surface/xdg_surface/xdg_toplevel/buffer.
namespace engine_placeholder::os::wayland {

class WaylandWindow {
public:
    // Creates a surface on `connection`, wraps it via xdg_wm_base into
    // an xdg_toplevel, sets `title`/`app_id`, blocks (via
    // wl_display_roundtrip) until the compositor's initial configure is
    // received and acknowledged, then attaches a solid-fill buffer sized
    // to the compositor's suggested size (or a default if none was
    // suggested) and commits to map the window. Returns an invalid
    // window (Valid() == false) on any failure — logged via
    // LogPlatform, never crashes. `connection` must outlive the
    // returned window.
    static WaylandWindow Create(WaylandConnection& connection, StringView title, StringView app_id);

    WaylandWindow() = default;
    ~WaylandWindow();

    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;
    WaylandWindow(WaylandWindow&& other) noexcept;
    WaylandWindow& operator=(WaylandWindow&& other) noexcept;

    bool Valid() const { return surface_ != nullptr; }

    wl_surface* Surface() const { return surface_; }
    xdg_surface* XdgSurface() const { return xdg_surface_; }
    xdg_toplevel* XdgToplevel() const { return xdg_toplevel_; }
    u32 Width() const { return width_; }
    u32 Height() const { return height_; }

private:
    void ReleaseAll();

    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;
    wl_buffer* buffer_ = nullptr;
    void* shm_data_ = nullptr;
    usize shm_size_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
};

} // namespace engine_placeholder::os::wayland
