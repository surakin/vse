#pragma once

#include <wayland-client.h>

#include "os/linux/wayland/generated/xdg-decoration-client-protocol.h"
#include "os/linux/wayland/generated/xdg-shell-client-protocol.h"

// Phase 1 Day 18: connects to a Wayland compositor and binds the
// globals this engine needs (wl_compositor, xdg_wm_base, wl_seat,
// wl_shm) via the registry. Surface/window creation is Day 19; the
// event loop that dispatches these globals' events is Day 20 — this
// type only owns the connection and the bound globals themselves.
namespace engine_placeholder::os::wayland {

class WaylandConnection {
public:
    // Connects to the compositor (wl_display_connect(nullptr) — uses
    // $WAYLAND_DISPLAY, defaulting to "wayland-0") and binds
    // compositor/xdg_wm_base/seat/shm via a registry roundtrip. Returns
    // an invalid connection (Valid() == false) on any failure — no
    // compositor running, or a required global not advertised — logged
    // via LogPlatform, never crashes.
    static WaylandConnection Connect();

    WaylandConnection() = default;
    ~WaylandConnection();

    WaylandConnection(const WaylandConnection&) = delete;
    WaylandConnection& operator=(const WaylandConnection&) = delete;
    WaylandConnection(WaylandConnection&& other) noexcept;
    WaylandConnection& operator=(WaylandConnection&& other) noexcept;

    bool Valid() const { return display_ != nullptr; }

    wl_display* Display() const { return display_; }
    wl_compositor* Compositor() const { return compositor_; }
    xdg_wm_base* XdgWmBase() const { return xdg_wm_base_; }
    wl_seat* Seat() const { return seat_; }
    wl_shm* Shm() const { return shm_; }
    // Optional: null when the compositor doesn't offer
    // zxdg_decoration_manager_v1 (e.g. GNOME, which only does client-side
    // decorations). KWin/wlroots offer it and draw the titlebar only if
    // the client asks for server-side mode through it.
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
