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

    // True once the compositor has sent this window's xdg_toplevel a
    // `close` event (e.g. the user hit the titlebar's close button) —
    // the engine's main loop checks this each iteration and exits when
    // it flips, rather than reacting inside the listener callback
    // itself. Requires the connection's event loop (wl_display_dispatch
    // et al) to actually run for the underlying event to be observed.
    bool ShouldClose() const { return should_close_; }

    wl_surface* Surface() const { return surface_; }
    xdg_surface* XdgSurface() const { return xdg_surface_; }
    xdg_toplevel* XdgToplevel() const { return xdg_toplevel_; }
    // True if the compositor agreed to draw the window's decorations
    // (titlebar, close button, resize edges). False means undecorated
    // (compositor lacks xdg-decoration, or chose client-side).
    bool ServerSideDecorated() const { return server_side_decorated_; }
    u32 Width() const { return width_; }
    u32 Height() const { return height_; }

private:
    // xdg_surface/xdg_toplevel listener trampolines. Private statics
    // (not free functions in an anonymous namespace) so they can reach
    // instance state directly via the `data` pointer without a friend
    // declaration.
    static void HandleXdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial);
    static void HandleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                                         wl_array* states);
    static void HandleToplevelClose(void* data, xdg_toplevel* toplevel);
    static void HandleDecorationConfigure(void* data, zxdg_toplevel_decoration_v1* decoration, uint32_t mode);

    // Listener vtables. Static members (rather than anonymous-namespace
    // globals in the .cpp) so their initializers, which reference the
    // private handlers above, are in class-member context.
    static const xdg_surface_listener kXdgSurfaceListener;
    static const xdg_toplevel_listener kXdgToplevelListener;
    static const zxdg_toplevel_decoration_v1_listener kDecorationListener;

    // Re-points the xdg_surface/xdg_toplevel proxies' stored listener
    // user-data at `this`. Wayland listener registration captures a
    // fixed `data` pointer at add_listener() time, but this type is
    // movable (its address can change), so every move ctor/assignment
    // calls this to keep the compositor's callbacks landing on the
    // object's current address instead of a stale/moved-from one.
    void RebindEventListenerUserData();

    bool ReplaceBuffer();
    void ReleaseAll();

    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* xdg_toplevel_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    bool server_side_decorated_ = false;
    wl_buffer* buffer_ = nullptr;
    wl_shm* shm_ = nullptr; // not owned; the connection's
    void* shm_data_ = nullptr;
    usize shm_size_ = 0;
    u32 width_ = 0;
    u32 height_ = 0;
    // Buffered xdg_toplevel configure state, applied atomically (and
    // logged, if changed) when the matching xdg_surface configure
    // arrives — per the xdg-shell spec, toplevel configure supplies
    // pending state and the surface configure is what's actually acked.
    u32 pending_width_ = 0;
    u32 pending_height_ = 0;
    bool should_close_ = false;
};

} // namespace engine_placeholder::os::wayland
