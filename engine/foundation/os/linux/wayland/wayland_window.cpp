#include "os/linux/wayland/wayland_window.h"

#include <sys/mman.h>
#include <unistd.h>

#include "log/log.h"

namespace engine_placeholder::os::wayland {

namespace {

// Window titles/app_ids have no OS-imposed length limit the way file
// paths do (kMaxPathBytes/PATH_MAX in os.h), so unlike os::Open a
// too-long value is truncated rather than treated as a hard failure —
// a truncated title is cosmetic, not a correctness problem.
constexpr usize kMaxTitleBytes = 256;

void ToCString(StringView sv, char (&buffer)[kMaxTitleBytes]) {
    usize n = sv.Length() < kMaxTitleBytes - 1 ? sv.Length() : kMaxTitleBytes - 1;
    for (usize i = 0; i < n; ++i) {
        buffer[i] = sv[i];
    }
    buffer[n] = '\0';
}

// Default size when the compositor's initial configure doesn't suggest
// one (width/height of 0 means "you choose") — provisional until Phase
// 3/4's renderer/window-resize handling exists.
constexpr u32 kDefaultWidth = 1280;
constexpr u32 kDefaultHeight = 720;

// Placeholder fill color (XRGB8888, X ignored) so the mapped window is
// visibly something rather than undefined/black — real content comes
// from Vulkan's swapchain once Phase 3 exists.
constexpr u32 kPlaceholderFillColor = 0xFF264F78;

struct ConfigureState {
    bool surface_configured = false;
    u32 toplevel_width = 0;
    u32 toplevel_height = 0;
};

void HandleXdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);
    static_cast<ConfigureState*>(data)->surface_configured = true;
}

constexpr xdg_surface_listener kXdgSurfaceListener = {
    .configure = HandleXdgSurfaceConfigure,
};

void HandleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                              wl_array* states) {
    (void)toplevel;
    (void)states;
    auto* state = static_cast<ConfigureState*>(data);
    if (width > 0) {
        state->toplevel_width = static_cast<u32>(width);
    }
    if (height > 0) {
        state->toplevel_height = static_cast<u32>(height);
    }
}

void HandleToplevelClose(void* data, xdg_toplevel* toplevel) {
    // Day 20 (event loop) wires this to actual shutdown — main.cpp still
    // runs a fixed-duration loop until then (see its Day 14 comment).
    (void)data;
    (void)toplevel;
}

constexpr xdg_toplevel_listener kXdgToplevelListener = {
    .configure = HandleToplevelConfigure,
    .close = HandleToplevelClose,
};

// Creates an anonymous, shrinkable-to-`size` shared-memory region
// (memfd, Linux-specific — this file is already Linux/Wayland-only) and
// maps it read+write. Returns {nullptr, -1} on any failure.
struct ShmRegion {
    void* data = nullptr;
    int fd = -1;
};

ShmRegion CreateShmRegion(usize size) {
    int fd = ::memfd_create("engine_wayland_buffer", MFD_CLOEXEC);
    if (fd < 0) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: memfd_create failed");
        return {};
    }
    if (::ftruncate(fd, static_cast<long>(size)) != 0) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: ftruncate on shm memfd failed");
        ::close(fd);
        return {};
    }
    void* data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: mmap of shm memfd failed");
        ::close(fd);
        return {};
    }
    return {data, fd};
}

} // namespace

WaylandWindow WaylandWindow::Create(WaylandConnection& connection, StringView title, StringView app_id) {
    if (!connection.Valid()) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: WaylandWindow::Create called with an invalid connection");
        return WaylandWindow();
    }

    wl_surface* surface = wl_compositor_create_surface(connection.Compositor());
    if (surface == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_compositor_create_surface failed");
        return WaylandWindow();
    }

    xdg_surface* xdg_surf = xdg_wm_base_get_xdg_surface(connection.XdgWmBase(), surface);
    if (xdg_surf == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: xdg_wm_base_get_xdg_surface failed");
        wl_surface_destroy(surface);
        return WaylandWindow();
    }

    ConfigureState configure_state;
    xdg_surface_add_listener(xdg_surf, &kXdgSurfaceListener, &configure_state);

    xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surf);
    if (toplevel == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: xdg_surface_get_toplevel failed");
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(surface);
        return WaylandWindow();
    }
    xdg_toplevel_add_listener(toplevel, &kXdgToplevelListener, &configure_state);

    char title_buf[kMaxTitleBytes];
    ToCString(title, title_buf);
    xdg_toplevel_set_title(toplevel, title_buf);

    char app_id_buf[kMaxTitleBytes];
    ToCString(app_id, app_id_buf);
    xdg_toplevel_set_app_id(toplevel, app_id_buf);

    // Initial commit with no buffer triggers the first configure; block
    // until it (and the ack it triggers) round-trips.
    wl_surface_commit(surface);
    wl_display_roundtrip(connection.Display());

    if (!configure_state.surface_configured) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: never received the initial xdg_surface configure");
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(surface);
        return WaylandWindow();
    }

    u32 width = configure_state.toplevel_width != 0 ? configure_state.toplevel_width : kDefaultWidth;
    u32 height = configure_state.toplevel_height != 0 ? configure_state.toplevel_height : kDefaultHeight;
    u32 stride = width * 4; // XRGB8888: 4 bytes/pixel
    usize shm_size = static_cast<usize>(stride) * height;

    ShmRegion region = CreateShmRegion(shm_size);
    if (region.data == nullptr) {
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(surface);
        return WaylandWindow();
    }

    auto* pixels = static_cast<u32*>(region.data);
    usize pixel_count = static_cast<usize>(width) * height;
    for (usize i = 0; i < pixel_count; ++i) {
        pixels[i] = kPlaceholderFillColor;
    }

    wl_shm_pool* pool = wl_shm_create_pool(connection.Shm(), region.fd, static_cast<int32_t>(shm_size));
    // The pool creation request already sent the fd to the compositor
    // (SCM_RIGHTS, synchronously as part of the wire request) — safe to
    // close our copy now.
    ::close(region.fd);
    if (pool == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_shm_create_pool failed");
        ::munmap(region.data, shm_size);
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(surface);
        return WaylandWindow();
    }
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
                                                   static_cast<int32_t>(stride), WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (buffer == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_shm_pool_create_buffer failed");
        ::munmap(region.data, shm_size);
        xdg_toplevel_destroy(toplevel);
        xdg_surface_destroy(xdg_surf);
        wl_surface_destroy(surface);
        return WaylandWindow();
    }

    // Second commit, now with a buffer attached: this is what actually
    // maps the window per the xdg-shell spec.
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
    wl_surface_commit(surface);
    wl_display_roundtrip(connection.Display());

    WaylandWindow window;
    window.surface_ = surface;
    window.xdg_surface_ = xdg_surf;
    window.xdg_toplevel_ = toplevel;
    window.buffer_ = buffer;
    window.shm_data_ = region.data;
    window.shm_size_ = shm_size;
    window.width_ = width;
    window.height_ = height;
    return window;
}

WaylandWindow::~WaylandWindow() {
    ReleaseAll();
}

WaylandWindow::WaylandWindow(WaylandWindow&& other) noexcept
    : surface_(other.surface_),
      xdg_surface_(other.xdg_surface_),
      xdg_toplevel_(other.xdg_toplevel_),
      buffer_(other.buffer_),
      shm_data_(other.shm_data_),
      shm_size_(other.shm_size_),
      width_(other.width_),
      height_(other.height_) {
    other.surface_ = nullptr;
    other.xdg_surface_ = nullptr;
    other.xdg_toplevel_ = nullptr;
    other.buffer_ = nullptr;
    other.shm_data_ = nullptr;
    other.shm_size_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

WaylandWindow& WaylandWindow::operator=(WaylandWindow&& other) noexcept {
    if (this != &other) {
        ReleaseAll();
        surface_ = other.surface_;
        xdg_surface_ = other.xdg_surface_;
        xdg_toplevel_ = other.xdg_toplevel_;
        buffer_ = other.buffer_;
        shm_data_ = other.shm_data_;
        shm_size_ = other.shm_size_;
        width_ = other.width_;
        height_ = other.height_;
        other.surface_ = nullptr;
        other.xdg_surface_ = nullptr;
        other.xdg_toplevel_ = nullptr;
        other.buffer_ = nullptr;
        other.shm_data_ = nullptr;
        other.shm_size_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void WaylandWindow::ReleaseAll() {
    if (shm_data_ != nullptr) {
        ::munmap(shm_data_, shm_size_);
        shm_data_ = nullptr;
        shm_size_ = 0;
    }
    if (buffer_ != nullptr) {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
    if (xdg_toplevel_ != nullptr) {
        xdg_toplevel_destroy(xdg_toplevel_);
        xdg_toplevel_ = nullptr;
    }
    if (xdg_surface_ != nullptr) {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }
    if (surface_ != nullptr) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
}

} // namespace engine_placeholder::os::wayland
