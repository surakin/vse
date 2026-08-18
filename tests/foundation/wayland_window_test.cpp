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
