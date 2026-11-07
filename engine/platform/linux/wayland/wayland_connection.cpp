#include "platform/linux/wayland/wayland_connection.h"

#include "log/log.h"
#include "mem/arena_allocator.h"
#include "string/string.h"

namespace engine_placeholder::os::wayland {

namespace {

void HandlePing(void* data, xdg_wm_base* shell, u32 serial) {
    (void)data;
    // Mandatory per the xdg-shell spec: a client that doesn't respond
    // risks the compositor considering it unresponsive/dead.
    xdg_wm_base_pong(shell, serial);
}

constexpr xdg_wm_base_listener kXdgWmBaseListener = {
    .ping = HandlePing,
};

void LogBoundGlobal(const char* interface_name, u32 version) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("Wayland: bound ").Append(interface_name).Append(" v").AppendUInt(version);
    Log(LogPlatform, LogVerbosity::Info, line.View());
}

// Plain aggregate used only as the registry listener's `data` pointer
// during Connect()'s roundtrip — WaylandConnection's fields are
// populated from this afterward (Connect() is a static member, so it
// has private access; these free functions never touch WaylandConnection
// itself).
struct RegistryState {
    wl_compositor* compositor = nullptr;
    xdg_wm_base* shell = nullptr;
    wl_seat* seat = nullptr;
    wl_shm* shm = nullptr;
    zxdg_decoration_manager_v1* decoration_manager = nullptr; // optional
};

void HandleGlobal(void* data, wl_registry* registry, u32 name, const char* interface, u32 version) {
    auto* state = static_cast<RegistryState*>(data);
    StringView iface(interface);

    if (iface == StringView(wl_compositor_interface.name)) {
        state->compositor =
            static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
        LogBoundGlobal(interface, version);
    } else if (iface == StringView(xdg_wm_base_interface.name)) {
        state->shell = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(state->shell, &kXdgWmBaseListener, nullptr);
        LogBoundGlobal(interface, version);
    } else if (iface == StringView(wl_seat_interface.name)) {
        state->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        LogBoundGlobal(interface, version);
    } else if (iface == StringView(wl_shm_interface.name)) {
        state->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        LogBoundGlobal(interface, version);
    } else if (iface == StringView(zxdg_decoration_manager_v1_interface.name)) {
        state->decoration_manager = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
        LogBoundGlobal(interface, version);
    }
    // Anything else advertised by the compositor isn't a global this
    // engine needs yet — silently ignored, not an error.
}

void HandleGlobalRemove(void* data, wl_registry* registry, u32 name) {
    // A compositor global disappearing mid-session (e.g. a seat being
    // unplugged) is Day 20's event-loop concern, not Day 18's connect
    // +bind step.
    (void)data;
    (void)registry;
    (void)name;
}

constexpr wl_registry_listener kRegistryListener = {
    .global = HandleGlobal,
    .global_remove = HandleGlobalRemove,
};

} // namespace

WaylandConnection WaylandConnection::Connect() {
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_display_connect failed (no compositor running?)");
        return WaylandConnection();
    }

    wl_registry* registry = wl_display_get_registry(display);
    if (registry == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_display_get_registry failed");
        wl_display_disconnect(display);
        return WaylandConnection();
    }

    RegistryState state;
    wl_registry_add_listener(registry, &kRegistryListener, &state);
    wl_display_roundtrip(display);

    if (state.compositor == nullptr || state.shell == nullptr || state.seat == nullptr || state.shm == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: compositor did not advertise all required globals");
        if (state.compositor != nullptr) {
            wl_compositor_destroy(state.compositor);
        }
        if (state.shell != nullptr) {
            xdg_wm_base_destroy(state.shell);
        }
        if (state.seat != nullptr) {
            wl_seat_destroy(state.seat);
        }
        if (state.shm != nullptr) {
            wl_shm_destroy(state.shm);
        }
        if (state.decoration_manager != nullptr) {
            zxdg_decoration_manager_v1_destroy(state.decoration_manager);
        }
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return WaylandConnection();
    }

    WaylandConnection conn;
    conn.display_ = display;
    conn.registry_ = registry;
    conn.compositor_ = state.compositor;
    conn.xdg_wm_base_ = state.shell;
    conn.seat_ = state.seat;
    conn.shm_ = state.shm;
    conn.decoration_manager_ = state.decoration_manager;
    return conn;
}

WaylandConnection::~WaylandConnection() {
    ReleaseAll();
}

WaylandConnection::WaylandConnection(WaylandConnection&& other) noexcept
    : display_(other.display_),
      registry_(other.registry_),
      compositor_(other.compositor_),
      xdg_wm_base_(other.xdg_wm_base_),
      seat_(other.seat_),
      shm_(other.shm_),
      decoration_manager_(other.decoration_manager_) {
    other.display_ = nullptr;
    other.registry_ = nullptr;
    other.compositor_ = nullptr;
    other.xdg_wm_base_ = nullptr;
    other.seat_ = nullptr;
    other.shm_ = nullptr;
    other.decoration_manager_ = nullptr;
}

WaylandConnection& WaylandConnection::operator=(WaylandConnection&& other) noexcept {
    if (this != &other) {
        ReleaseAll();
        display_ = other.display_;
        registry_ = other.registry_;
        compositor_ = other.compositor_;
        xdg_wm_base_ = other.xdg_wm_base_;
        seat_ = other.seat_;
        shm_ = other.shm_;
        decoration_manager_ = other.decoration_manager_;
        other.display_ = nullptr;
        other.registry_ = nullptr;
        other.compositor_ = nullptr;
        other.xdg_wm_base_ = nullptr;
        other.seat_ = nullptr;
        other.shm_ = nullptr;
        other.decoration_manager_ = nullptr;
    }
    return *this;
}

void WaylandConnection::ReleaseAll() {
    if (compositor_ != nullptr) {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }
    if (xdg_wm_base_ != nullptr) {
        xdg_wm_base_destroy(xdg_wm_base_);
        xdg_wm_base_ = nullptr;
    }
    if (seat_ != nullptr) {
        wl_seat_destroy(seat_);
        seat_ = nullptr;
    }
    if (shm_ != nullptr) {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }
    if (decoration_manager_ != nullptr) {
        zxdg_decoration_manager_v1_destroy(decoration_manager_);
        decoration_manager_ = nullptr;
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
}

} // namespace engine_placeholder::os::wayland
