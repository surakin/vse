#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

#include "log/log.h"
#include "os/linux/wayland/wayland_connection.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"

using engine_placeholder::Log;
using engine_placeholder::LogPlatform;
using engine_placeholder::LogVerbosity;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::os::Write;
using engine_placeholder::os::wayland::WaylandConnection;

namespace {

char g_capture_buffer[2048];
usize g_capture_len = 0;

long CapturingWriteSink(int fd, const void* buf, usize len) {
    (void)fd;
    usize copy_len = len;
    if (g_capture_len + copy_len > sizeof(g_capture_buffer)) {
        copy_len = sizeof(g_capture_buffer) - g_capture_len;
    }
    memcpy(g_capture_buffer + g_capture_len, buf, copy_len);
    g_capture_len += copy_len;
    return static_cast<long>(len);
}

} // namespace

TEST_CASE("WaylandConnection::Connect binds compositor/xdg_wm_base/seat/shm against a real compositor",
          "[wayland][os]") {
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());
    REQUIRE(conn.Display() != nullptr);
    REQUIRE(conn.Compositor() != nullptr);
    REQUIRE(conn.XdgWmBase() != nullptr);
    REQUIRE(conn.Seat() != nullptr);
    REQUIRE(conn.Shm() != nullptr);
}

TEST_CASE("WaylandConnection::Connect fails gracefully (no crash) when no compositor is reachable",
          "[wayland][os]") {
    const char* previous = ::getenv("WAYLAND_DISPLAY");
    ::setenv("WAYLAND_DISPLAY", "engine-test-nonexistent-socket", 1);

    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE_FALSE(conn.Valid());
    REQUIRE(conn.Display() == nullptr);

    if (previous != nullptr) {
        ::setenv("WAYLAND_DISPLAY", previous, 1);
    } else {
        ::unsetenv("WAYLAND_DISPLAY");
    }
}

TEST_CASE("WaylandConnection::Connect logs each bound global's interface name and version", "[wayland][os]") {
    g_capture_len = 0;
    LogPlatform.SetVerbosity(LogVerbosity::Info);
    SetWriteSinkForTesting(&CapturingWriteSink);

    WaylandConnection conn = WaylandConnection::Connect();

    SetWriteSinkForTesting(&Write);
    LogPlatform.SetVerbosity(LogVerbosity::Warn);

    REQUIRE(conn.Valid());
    StringView captured(g_capture_buffer, g_capture_len);
    REQUIRE(captured.Contains(StringView("wl_compositor")));
    REQUIRE(captured.Contains(StringView("xdg_wm_base")));
    REQUIRE(captured.Contains(StringView("wl_seat")));
    REQUIRE(captured.Contains(StringView("wl_shm")));
}

TEST_CASE("WaylandConnection move construction/assignment transfers ownership without double-free",
          "[wayland][os]") {
    WaylandConnection a = WaylandConnection::Connect();
    REQUIRE(a.Valid());
    wl_display* display = a.Display();

    WaylandConnection b(std::move(a));
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(b.Display() == display);

    WaylandConnection c;
    c = std::move(b);
    REQUIRE_FALSE(b.Valid());
    REQUIRE(c.Valid());
    REQUIRE(c.Display() == display);
}
