#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/wayland/wayland_window.h"

using engine_placeholder::os::wayland::WaylandConnection;
using engine_placeholder::os::wayland::WaylandWindow;

TEST_CASE("WaylandWindow::Create maps a real, visible toplevel window against a real compositor",
          "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    WaylandWindow window = WaylandWindow::Create(conn, "engine_placeholder test window", "engine_placeholder");
    REQUIRE(window.Valid());
    REQUIRE(window.Surface() != nullptr);
    REQUIRE(window.XdgSurface() != nullptr);
    REQUIRE(window.XdgToplevel() != nullptr);
    REQUIRE(window.Width() > 0);
    REQUIRE(window.Height() > 0);
}

TEST_CASE("WaylandWindow::Create's window does not request close before any close event arrives",
          "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    WaylandWindow window = WaylandWindow::Create(conn, "engine_placeholder test window", "engine_placeholder");
    REQUIRE(window.Valid());
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("A default-constructed WaylandWindow reports ShouldClose() false", "[wayland][os]") {
    WaylandWindow window;
    REQUIRE_FALSE(window.ShouldClose());
}

TEST_CASE("WaylandWindow::Create fails gracefully on an invalid connection", "[wayland][os]") {
    WaylandConnection invalid_conn;
    REQUIRE_FALSE(invalid_conn.Valid());

    WaylandWindow window = WaylandWindow::Create(invalid_conn, "title", "app_id");
    REQUIRE_FALSE(window.Valid());
    REQUIRE(window.Surface() == nullptr);
}

TEST_CASE("WaylandWindow::Create truncates an overlong title instead of failing", "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    char long_title[600];
    for (auto& c : long_title) {
        c = 'x';
    }
    long_title[sizeof(long_title) - 1] = '\0';

    WaylandWindow window = WaylandWindow::Create(
        conn, engine_placeholder::StringView(long_title, sizeof(long_title) - 1), "engine_placeholder");
    REQUIRE(window.Valid());
}

TEST_CASE("WaylandWindow move construction/assignment transfers ownership without double-free", "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());

    WaylandWindow a = WaylandWindow::Create(conn, "move test", "engine_placeholder");
    REQUIRE(a.Valid());
    wl_surface* surface = a.Surface();

    WaylandWindow b(std::move(a));
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(b.Surface() == surface);

    WaylandWindow c;
    c = std::move(b);
    REQUIRE_FALSE(b.Valid());
    REQUIRE(c.Valid());
    REQUIRE(c.Surface() == surface);
}

TEST_CASE("WaylandWindow gets server-side decorations when the compositor offers xdg-decoration",
          "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());
    if (conn.DecorationManager() == nullptr) {
        SKIP("compositor does not offer zxdg_decoration_manager_v1");
    }
    WaylandWindow window = WaylandWindow::Create(conn, "decoration test", "engine_placeholder");
    REQUIRE(window.Valid());
    REQUIRE(window.ServerSideDecorated());

    // Survives a move (listener user data re-pointed).
    WaylandWindow moved(std::move(window));
    REQUIRE(moved.ServerSideDecorated());
}

TEST_CASE("WaylandWindow re-allocates its buffer and takes the new size when the compositor resizes it",
          "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());
    WaylandWindow window = WaylandWindow::Create(conn, "resize test", "engine_placeholder");
    REQUIRE(window.Valid());
    const engine_placeholder::u32 original_width = window.Width();

    // Ask the compositor to maximize: it answers with a configure at
    // the maximized size, which must become the window's size (and the
    // buffer must be re-allocated to match, or the compositor ignores
    // the new size).
    xdg_toplevel_set_maximized(window.XdgToplevel());
    wl_surface_commit(window.Surface());
    for (int i = 0; i < 10 && window.Width() == original_width; ++i) {
        wl_display_roundtrip(conn.Display());
    }
    REQUIRE(window.CommitPendingResize()); // size changed -> new buffer
    if (window.Width() == original_width) {
        SKIP("compositor did not resize the window on set_maximized");
    }
    xdg_toplevel_unset_maximized(window.XdgToplevel());
    for (int i = 0; i < 5; ++i) {
        wl_display_roundtrip(conn.Display());
    }
    window.CommitPendingResize();
    wl_display_roundtrip(conn.Display());
    REQUIRE(window.Valid());
    // The new buffer was attached and the compositor kept the connection.
    REQUIRE(wl_display_get_error(conn.Display()) == 0);
}
