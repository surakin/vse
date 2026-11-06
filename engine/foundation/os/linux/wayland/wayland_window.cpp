#include "os/linux/wayland/wayland_window.h"

#include <sys/mman.h>
#include <cstring>
#include <unistd.h>

#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/window.h"

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
// from Vulkan's swapchain once Phase 4 exists.
constexpr u32 kPlaceholderFillColor = 0xFF264F78;

void LogResize(u32 width, u32 height) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append("Wayland: window resized to ").AppendUInt(width).Append('x').AppendUInt(height);
    Log(LogPlatform, LogVerbosity::Info, line.View());
}

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

// Builds a width x height XRGB8888 wl_shm buffer filled with the
// placeholder color. On success returns true and hands back the buffer,
// its mapping and mapping size.
bool MakeFilledBuffer(wl_shm* shm, u32 width, u32 height, wl_buffer*& out_buffer, void*& out_data,
                      usize& out_size) {
    u32 stride = width * 4; // XRGB8888: 4 bytes/pixel
    usize shm_size = static_cast<usize>(stride) * height;

    ShmRegion region = CreateShmRegion(shm_size);
    if (region.data == nullptr) {
        return false;
    }
    // Fill by doubling: write one pixel, then copy the filled prefix over
    // the rest in growing chunks (a few big memcpys instead of a per-
    // pixel loop, which is painfully slow in unoptimized builds and ran
    // on every resize configure).
    auto* pixels = static_cast<u32*>(region.data);
    usize pixel_count = static_cast<usize>(width) * height;
    if (pixel_count > 0) {
        pixels[0] = kPlaceholderFillColor;
        usize filled = 1;
        while (filled < pixel_count) {
            usize chunk = filled < pixel_count - filled ? filled : pixel_count - filled;
            std::memcpy(pixels + filled, pixels, chunk * sizeof(u32));
            filled += chunk;
        }
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, region.fd, static_cast<int32_t>(shm_size));
    // The pool creation request already sent the fd to the compositor
    // (SCM_RIGHTS, synchronously as part of the wire request) — safe to
    // close our copy now.
    ::close(region.fd);
    if (pool == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_shm_create_pool failed");
        ::munmap(region.data, shm_size);
        return false;
    }
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
                                                   static_cast<int32_t>(stride), WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (buffer == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: wl_shm_pool_create_buffer failed");
        ::munmap(region.data, shm_size);
        return false;
    }
    out_buffer = buffer;
    out_data = region.data;
    out_size = shm_size;
    return true;
}

} // namespace

WaylandWindow WaylandWindow::Create(WaylandConnection& connection, StringView title, StringView app_id,
                                    u32 width_hint, u32 height_hint) {
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

    // `window` is populated field-by-field from here on (rather than
    // only at the very end) specifically so its address can be handed
    // to the listeners now: events (including the initial configure
    // this function blocks on below) must land on the real object, not
    // a temporary that's discarded afterward. Every early-return below
    // still cleans up correctly since `window`'s destructor runs
    // regardless of what's returned.
    WaylandWindow window;
    window.default_width_ = width_hint != 0 ? width_hint : kDefaultWidth;
    window.default_height_ = height_hint != 0 ? height_hint : kDefaultHeight;
    window.surface_ = surface;
    window.xdg_surface_ = xdg_surf;
    xdg_surface_add_listener(xdg_surf, &kXdgSurfaceListener, &window);

    xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdg_surf);
    if (toplevel == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: xdg_surface_get_toplevel failed");
        return WaylandWindow();
    }
    window.xdg_toplevel_ = toplevel;
    xdg_toplevel_add_listener(toplevel, &kXdgToplevelListener, &window);

    // Ask for server-side decorations. Without this KWin draws no
    // titlebar/close button/resize edges at all (the window is a bare
    // surface). Optional: compositors without the protocol leave the
    // window undecorated, and the mode actually granted is reported via
    // the decoration's configure event (handled during the roundtrip
    // below).
    if (connection.DecorationManager() != nullptr) {
        window.decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(connection.DecorationManager(), toplevel);
        if (window.decoration_ != nullptr) {
            zxdg_toplevel_decoration_v1_add_listener(window.decoration_, &kDecorationListener, &window);
            zxdg_toplevel_decoration_v1_set_mode(window.decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
    }

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

    if (window.width_ == 0 || window.height_ == 0) {
        Log(LogPlatform, LogVerbosity::Error, "Wayland: never received the initial xdg_surface configure");
        return WaylandWindow();
    }

    window.shm_ = connection.Shm();
    if (!window.ReplaceBuffer()) {
        return WaylandWindow();
    }
    wl_display_roundtrip(connection.Display());
    return window;
}

// (Re)allocates the window's buffer at the current width_/height_ and
// commits it. Per xdg-shell the compositor's configure size is only a
// suggestion: the window doesn't actually change size until the client
// attaches a buffer of the new size — which is why resizing did nothing
// while this only logged the new dimensions.
bool WaylandWindow::ReplaceBuffer() {
    wl_buffer* buffer = nullptr;
    void* data = nullptr;
    usize size = 0;
    if (!MakeFilledBuffer(shm_, width_, height_, buffer, data, size)) {
        return false;
    }
    wl_surface_attach(surface_, buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_));
    wl_surface_commit(surface_);

    // The old buffer can go as soon as the new one is attached; the
    // compositor keeps its own reference to the old pool until done.
    if (buffer_ != nullptr) {
        wl_buffer_destroy(buffer_);
    }
    if (shm_data_ != nullptr) {
        ::munmap(shm_data_, shm_size_);
    }
    buffer_ = buffer;
    shm_data_ = data;
    shm_size_ = size;
    return true;
}

bool WaylandWindow::CommitPendingResize() {
    if (!buffer_stale_) {
        return false;
    }
    if (surface_exported_) {
        // A graphics layer presents to this surface: its swapchain is recreated on the resize serial and supplies the buffers.
        buffer_stale_ = false;
        return false;
    }
    // Only clear the flag on success, so a failed allocation is retried
    // on the next pump instead of leaving the window stuck at the old
    // size forever.
    if (!ReplaceBuffer()) {
        return false;
    }
    buffer_stale_ = false;
    return true;
}

WaylandWindow::~WaylandWindow() {
    ReleaseAll();
}

WaylandWindow::WaylandWindow(WaylandWindow&& other) noexcept
    : surface_(other.surface_),
      xdg_surface_(other.xdg_surface_),
      xdg_toplevel_(other.xdg_toplevel_),
      decoration_(other.decoration_),
      server_side_decorated_(other.server_side_decorated_),
      buffer_(other.buffer_),
      shm_(other.shm_),
      buffer_stale_(other.buffer_stale_),
      surface_exported_(other.surface_exported_),
      shm_data_(other.shm_data_),
      shm_size_(other.shm_size_),
      width_(other.width_),
      height_(other.height_),
      resize_serial_(other.resize_serial_),
      pending_width_(other.pending_width_),
      pending_height_(other.pending_height_),
      pending_constrained_(other.pending_constrained_),
      fullscreen_(other.fullscreen_),
      default_width_(other.default_width_),
      default_height_(other.default_height_),
      floating_width_(other.floating_width_),
      floating_height_(other.floating_height_),
      should_close_(other.should_close_) {
    other.surface_ = nullptr;
    other.xdg_surface_ = nullptr;
    other.xdg_toplevel_ = nullptr;
    other.decoration_ = nullptr;
    other.server_side_decorated_ = false;
    other.buffer_ = nullptr;
    other.buffer_stale_ = false;
    other.shm_data_ = nullptr;
    other.shm_size_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.resize_serial_ = 0;
    other.pending_width_ = 0;
    other.pending_height_ = 0;
    other.pending_constrained_ = false;
    other.fullscreen_ = false;
    other.default_width_ = 0;
    other.default_height_ = 0;
    other.floating_width_ = 0;
    other.floating_height_ = 0;
    other.should_close_ = false;
    RebindEventListenerUserData();
}

WaylandWindow& WaylandWindow::operator=(WaylandWindow&& other) noexcept {
    if (this != &other) {
        ReleaseAll();
        surface_ = other.surface_;
        xdg_surface_ = other.xdg_surface_;
        xdg_toplevel_ = other.xdg_toplevel_;
        decoration_ = other.decoration_;
        server_side_decorated_ = other.server_side_decorated_;
        buffer_ = other.buffer_;
        shm_ = other.shm_;
        buffer_stale_ = other.buffer_stale_;
        surface_exported_ = other.surface_exported_;
        shm_data_ = other.shm_data_;
        shm_size_ = other.shm_size_;
        width_ = other.width_;
        height_ = other.height_;
        resize_serial_ = other.resize_serial_;
        pending_width_ = other.pending_width_;
        pending_height_ = other.pending_height_;
        pending_constrained_ = other.pending_constrained_;
        fullscreen_ = other.fullscreen_;
        default_width_ = other.default_width_;
        default_height_ = other.default_height_;
        floating_width_ = other.floating_width_;
        floating_height_ = other.floating_height_;
        should_close_ = other.should_close_;
        other.surface_ = nullptr;
        other.xdg_surface_ = nullptr;
        other.xdg_toplevel_ = nullptr;
        other.decoration_ = nullptr;
        other.server_side_decorated_ = false;
        other.buffer_ = nullptr;
        other.buffer_stale_ = false;
        other.shm_data_ = nullptr;
        other.shm_size_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.resize_serial_ = 0;
        other.pending_width_ = 0;
        other.pending_height_ = 0;
        other.pending_constrained_ = false;
        other.fullscreen_ = false;
        other.default_width_ = 0;
        other.default_height_ = 0;
        other.floating_width_ = 0;
        other.floating_height_ = 0;
        other.should_close_ = false;
        RebindEventListenerUserData();
    }
    return *this;
}

void WaylandWindow::RebindEventListenerUserData() {
    if (xdg_surface_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(xdg_surface_), this);
    }
    if (xdg_toplevel_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(xdg_toplevel_), this);
    }
    if (decoration_ != nullptr) {
        wl_proxy_set_user_data(reinterpret_cast<wl_proxy*>(decoration_), this);
    }
}

const xdg_surface_listener WaylandWindow::kXdgSurfaceListener = {
    .configure = WaylandWindow::HandleXdgSurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::kXdgToplevelListener = {
    .configure = WaylandWindow::HandleToplevelConfigure,
    .close = WaylandWindow::HandleToplevelClose,
};

const zxdg_toplevel_decoration_v1_listener WaylandWindow::kDecorationListener = {
    .configure = WaylandWindow::HandleDecorationConfigure,
};

void WaylandWindow::HandleDecorationConfigure(void* data, zxdg_toplevel_decoration_v1* decoration, uint32_t mode) {
    (void)decoration;
    auto* window = static_cast<WaylandWindow*>(data);
    window->server_side_decorated_ = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    Log(LogPlatform, LogVerbosity::Info,
        window->server_side_decorated_ ? "Wayland: server-side decorations granted"
                                       : "Wayland: client-side decorations (window is undecorated)");
}

void WaylandWindow::HandleXdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial) {
    auto* window = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(surface, serial);

    // The latest toplevel configure's size, where 0 means "unspecified —
    // you choose". "You choose" restores the last unconstrained size, not
    // the current one: after leaving maximized the current size is the
    // screen size, which is exactly what must not stick.
    u32 fallback_width = window->floating_width_ != 0 ? window->floating_width_ : window->default_width_;
    u32 fallback_height = window->floating_height_ != 0 ? window->floating_height_ : window->default_height_;
    u32 new_width = window->pending_width_ != 0 ? window->pending_width_ : fallback_width;
    u32 new_height = window->pending_height_ != 0 ? window->pending_height_ : fallback_height;
    if (!window->pending_constrained_) {
        window->floating_width_ = new_width;
        window->floating_height_ = new_height;
    }
    if (new_width != window->width_ || new_height != window->height_) {
        window->width_ = new_width;
        window->height_ = new_height;
        window->resize_serial_ = detail::NextResizeSerial();
        LogResize(new_width, new_height);
        // Once mapped, a size change needs a matching buffer (the initial
        // configure inside Create() has no buffer yet; Create() makes it).
        // Deferred (CommitPendingResize) rather than done here: an
        // interactive drag delivers a configure per pointer motion, and
        // only the latest size is worth a new buffer.
        if (window->buffer_ != nullptr) {
            window->buffer_stale_ = true;
        }
    }
}

void WaylandWindow::HandleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                                             wl_array* states) {
    (void)toplevel;
    auto* window = static_cast<WaylandWindow*>(data);
    // Always overwrite, including with 0: a 0 dimension means "client
    // chooses", and must not leave an earlier (e.g. maximized) size
    // pending.
    window->pending_width_ = width > 0 ? static_cast<u32>(width) : 0;
    window->pending_height_ = height > 0 ? static_cast<u32>(height) : 0;

    bool constrained = false;
    bool fullscreen = false;
    auto* begin = static_cast<uint32_t*>(states->data);
    auto* end = begin + states->size / sizeof(uint32_t);
    for (uint32_t* it = begin; it < end; ++it) {
        if (*it == XDG_TOPLEVEL_STATE_MAXIMIZED || *it == XDG_TOPLEVEL_STATE_FULLSCREEN ||
            *it == XDG_TOPLEVEL_STATE_TILED_LEFT || *it == XDG_TOPLEVEL_STATE_TILED_RIGHT ||
            *it == XDG_TOPLEVEL_STATE_TILED_TOP || *it == XDG_TOPLEVEL_STATE_TILED_BOTTOM) {
            constrained = true;
        }
        if (*it == XDG_TOPLEVEL_STATE_FULLSCREEN) {
            fullscreen = true;
        }
    }
    window->pending_constrained_ = constrained;
    window->fullscreen_ = fullscreen;
}

bool WaylandWindow::SetFullscreen(bool fullscreen) {
    if (xdg_toplevel_ == nullptr) {
        return false;
    }
    if (fullscreen) {
        xdg_toplevel_set_fullscreen(xdg_toplevel_, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(xdg_toplevel_);
    }
    return true;
}

void WaylandWindow::HandleToplevelClose(void* data, xdg_toplevel* toplevel) {
    (void)toplevel;
    static_cast<WaylandWindow*>(data)->should_close_ = true;
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
    if (decoration_ != nullptr) {
        zxdg_toplevel_decoration_v1_destroy(decoration_);
        decoration_ = nullptr;
        server_side_decorated_ = false;
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
    pending_width_ = 0;
    pending_height_ = 0;
    pending_constrained_ = false;
    fullscreen_ = false;
    floating_width_ = 0;
    floating_height_ = 0;
    buffer_stale_ = false;
    should_close_ = false;
}

} // namespace engine_placeholder::os::wayland
