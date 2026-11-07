#pragma once

#include <wayland-client.h>

#include "os/linux/wayland/generated/xdg-shell-client-protocol.h"
#include "os/linux/wayland/wayland_connection.h"
#include "string/string.h"
#include "types.h"

//! Wayland window: a `wl_surface` wrapped as a mapped xdg-shell toplevel.

namespace engine_placeholder::os::wayland {

/// A `wl_surface` wrapped as an xdg-shell toplevel window with a solid-fill `wl_shm` buffer so it maps.
///
/// Per xdg-shell a surface is not mapped until a buffer is committed after the first
/// configure and ack, so the window creates a minimal solid-fill buffer to complete that
/// sequence; real pixel content is expected to come from a swapchain. It owns the
/// surface, xdg surface, toplevel, optional decoration object and buffer, but not the event loop.
/// Movable, not copyable.
///
/// Thread-safety: owner-thread-only. Listener callbacks run on the thread that dispatches the connection's events, which must also call its methods.
/// Ownership: owns the surface, xdg objects and buffer; the connection is borrowed and must outlive the window.
class WaylandWindow {
public:
    /// Creates a toplevel on `connection`, blocks until the compositor's initial configure is acknowledged, then maps it.
    ///
    /// Sets `title` and `app_id`, waits with `wl_display_roundtrip`, then attaches a solid-fill
    /// buffer sized to the compositor's suggested size (or the hint, or a default) and commits.
    /// `width_hint` and `height_hint` give the size used when the compositor suggests none,
    /// and the size restored when it sends a 0x0 configure (for example on leaving maximized)
    /// with nothing better known.
    /// Param connection: valid connection; must outlive the returned window.
    /// Param title: window title.
    /// Param app_id: application id the compositor uses to group and identify the window.
    /// Param width_hint: fallback width in pixels; 0 means the built-in 1280x720 default.
    /// Param height_hint: fallback height in pixels; 0 means the built-in 1280x720 default.
    /// Returns: the window; check [[os::wayland::WaylandWindow::Valid]].
    /// Fails: returns an invalid window (logged via `LogPlatform`) on any failure.
    static WaylandWindow Create(WaylandConnection& connection, StringView title, StringView app_id,
                                u32 width_hint = 0, u32 height_hint = 0);

    /// Constructs an invalid window that owns nothing.
    WaylandWindow() = default;

    /// Destroys the buffer and the xdg and surface objects.
    ~WaylandWindow();

    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;
    /// Takes over the objects of `other`, re-pointing the listeners at this object and leaving `other` invalid.
    WaylandWindow(WaylandWindow&& other) noexcept;

    /// Releases this window, then takes over the objects of `other`, leaving it invalid.
    WaylandWindow& operator=(WaylandWindow&& other) noexcept;

    /// Returns whether the window has a surface.
    bool Valid() const { return surface_ != nullptr; }

    /// Returns whether the compositor has sent a `close` event to the toplevel, such as from the titlebar close button.
    ///
    /// The main loop polls this instead of reacting inside the listener callback. The
    /// connection's event loop (`wl_display_dispatch` or similar) must run for the event to be seen.
    bool ShouldClose() const { return should_close_; }

    /// Allocates and commits a buffer of the new size if a configure changed it since the last call.
    ///
    /// The window does not actually resize until this happens. A burst of configures,
    /// such as an interactive drag, is coalesced into one buffer. The event pump calls
    /// this after dispatching.
    /// Returns: `true` if a new buffer was committed.
    bool CommitPendingResize();

    /// Records that the surface was given to a graphics layer (a Vulkan swapchain), which then owns the surface's buffers.
    ///
    /// After this, a resize only updates the size and the resize serial; attaching this window's own shared-memory buffer
    /// would race the driver's buffers (with explicit sync the compositor kills the connection).
    void MarkSurfaceExported() const { surface_exported_ = true; }

    /// Returns the `wl_surface`, or null if invalid.
    ///
    /// Ownership: borrowed; owned by this window.
    wl_surface* Surface() const { return surface_; }
    /// Returns the `xdg_surface`, or null if invalid.
    ///
    /// Ownership: borrowed; owned by this window.
    xdg_surface* XdgSurface() const { return xdg_surface_; }
    /// Returns the `xdg_toplevel`, or null if invalid.
    ///
    /// Ownership: borrowed; owned by this window.
    xdg_toplevel* XdgToplevel() const { return xdg_toplevel_; }
    /// Returns whether the compositor agreed to draw the window decorations (titlebar, close button, resize edges).
    ///
    /// False means undecorated: the compositor lacks xdg-decoration or chose client-side mode.
    bool ServerSideDecorated() const { return server_side_decorated_; }
    /// Returns the current window width in pixels.
    u32 Width() const { return width_; }
    /// Returns the current window height in pixels.
    u32 Height() const { return height_; }
    /// Returns a counter that changes each time a configure changes the window size.
    u64 ResizeSerial() const { return resize_serial_; }
    /// Asks the compositor to make the toplevel fullscreen (`xdg_toplevel_set_fullscreen`) or to leave fullscreen.
    ///
    /// Param fullscreen: the state wanted.
    /// Returns: true if the request was sent.
    /// Fails: returns false on an invalid window.
    bool SetFullscreen(bool fullscreen);
    /// Returns whether the latest toplevel configure listed the fullscreen state.
    bool IsFullscreen() const { return fullscreen_; }

private:
    // xdg_surface/xdg_toplevel listener trampolines. Private statics
    // (not free functions in an anonymous namespace) so they can reach
    // instance state directly via the `data` pointer without a friend
    // declaration.
    static void HandleXdgSurfaceConfigure(void* data, xdg_surface* surface, u32 serial);
    static void HandleToplevelConfigure(void* data, xdg_toplevel* toplevel, i32 width, i32 height,
                                         wl_array* states);
    static void HandleToplevelClose(void* data, xdg_toplevel* toplevel);
    static void HandleDecorationConfigure(void* data, zxdg_toplevel_decoration_v1* decoration, u32 mode);

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
    u64 resize_serial_ = 0;
    // Buffered xdg_toplevel configure state, applied atomically (and
    // logged, if changed) when the matching xdg_surface configure
    // arrives — per the xdg-shell spec, toplevel configure supplies
    // pending state and the surface configure is what's actually acked.
    u32 pending_width_ = 0;
    u32 pending_height_ = 0;
    // True if the latest toplevel configure said maximized/fullscreen/
    // tiled: its size must not become the size we restore to later.
    bool pending_constrained_ = false;
    // True if the latest toplevel configure listed the fullscreen state.
    bool fullscreen_ = false;
    u32 default_width_ = 0;
    u32 default_height_ = 0;
    // Last size the window had while unconstrained — what a 0x0
    // ("client chooses") configure restores. 0 = not yet known.
    u32 floating_width_ = 0;
    u32 floating_height_ = 0;
    bool buffer_stale_ = false;
    // Set once the surface was handed to a graphics layer: from then on that layer attaches the buffers, and this window must not.
    mutable bool surface_exported_ = false;
    bool should_close_ = false;
};

} // namespace engine_placeholder::os::wayland
