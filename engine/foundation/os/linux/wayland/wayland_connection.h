#pragma once

#include <wayland-client.h>

#include "os/linux/wayland/generated/xdg-decoration-client-protocol.h"
#include "os/linux/wayland/generated/xdg-shell-client-protocol.h"

//! Wayland connection: the compositor connection and the globals the engine binds.

namespace engine_placeholder::os::wayland {

/// Connection to a Wayland compositor plus the bound `wl_compositor`, `xdg_wm_base`, `wl_seat` and `wl_shm` globals.
///
/// Only owns the connection and the globals; surfaces, input and the event
/// loop live in other types. Movable, not copyable.
///
/// Thread-safety: not-thread-safe. libwayland objects here are used from one thread.
/// Ownership: owns the `wl_display` and every bound global and releases them on
///   destruction. The accessors return borrowed pointers valid until then.
class WaylandConnection {
public:
    /// Connects to the compositor named by `$WAYLAND_DISPLAY` (default `wayland-0`) and binds the required globals.
    ///
    /// Binds `wl_compositor`, `xdg_wm_base`, `wl_seat` and `wl_shm` with a registry
    /// roundtrip, plus `zxdg_decoration_manager_v1` when offered.
    /// Returns: the connection; check [[os::wayland::WaylandConnection::Valid]].
    /// Fails: returns an invalid connection (logged via `LogPlatform`) if no compositor is running or a required global is missing.
    static WaylandConnection Connect();

    /// Constructs an invalid connection that owns nothing.
    WaylandConnection() = default;

    /// Releases the bound globals and disconnects from the compositor.
    ~WaylandConnection();

    WaylandConnection(const WaylandConnection&) = delete;
    WaylandConnection& operator=(const WaylandConnection&) = delete;
    /// Takes over the connection and globals of `other`, leaving it invalid.
    WaylandConnection(WaylandConnection&& other) noexcept;

    /// Releases this connection, then takes over that of `other`, leaving it invalid.
    WaylandConnection& operator=(WaylandConnection&& other) noexcept;

    /// Returns whether the connection to the compositor is open.
    bool Valid() const { return display_ != nullptr; }

    /// Returns the `wl_display`, or null if invalid.
    wl_display* Display() const { return display_; }
    /// Returns the bound `wl_compositor`, or null if invalid.
    wl_compositor* Compositor() const { return compositor_; }
    /// Returns the bound `xdg_wm_base`, or null if invalid.
    xdg_wm_base* XdgWmBase() const { return xdg_wm_base_; }
    /// Returns the bound `wl_seat`, or null if invalid.
    wl_seat* Seat() const { return seat_; }
    /// Returns the bound `wl_shm`, or null if invalid.
    wl_shm* Shm() const { return shm_; }
    /// Returns the bound `zxdg_decoration_manager_v1`, or null when the compositor does not offer it.
    ///
    /// GNOME only does client-side decorations and does not offer it. KWin and
    /// wlroots offer it and draw the titlebar only if the client asks for server-side mode.
    zxdg_decoration_manager_v1* DecorationManager() const { return decoration_manager_; }

private:
    void ReleaseAll();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* xdg_wm_base_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_shm* shm_ = nullptr;
    zxdg_decoration_manager_v1* decoration_manager_ = nullptr;
};

} // namespace engine_placeholder::os::wayland
