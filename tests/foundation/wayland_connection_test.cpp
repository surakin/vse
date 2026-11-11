
#include <catch2/catch_test_macros.hpp>
#include "os/console.h"
#include "display_session.h"

#include "log/log.h"
#include "platform/linux/wayland/wayland_connection.h"
#include "os/os.h"
#include "string/string.h"
#include "types.h"
#include "lang/utility.h"
#include "cstr/mem_ops.h"

using engine_placeholder::LogPlatform;
using engine_placeholder::LogVerbosity;
using engine_placeholder::SetWriteSinkForTesting;
using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::os::Write;
using engine_placeholder::os::wayland::WaylandConnection;

namespace
{

char g_WaylandConnectionTest_capture_buffer[2048];
usize g_WaylandConnectionTest_capture_len = 0;

long WaylandConnectionTestCapturingWriteSink(int fd, const void* buf, usize len)
{
    (void)fd;
    usize copy_len = len;
    if (g_WaylandConnectionTest_capture_len + copy_len > sizeof(g_WaylandConnectionTest_capture_buffer))
    {
        copy_len = sizeof(g_WaylandConnectionTest_capture_buffer) - g_WaylandConnectionTest_capture_len;
    }
    engine_placeholder::MemCopy(g_WaylandConnectionTest_capture_buffer + g_WaylandConnectionTest_capture_len, buf,
                                copy_len);
    g_WaylandConnectionTest_capture_len += copy_len;
    return static_cast<long>(len);
}

}  // namespace

TEST_CASE("WaylandConnection::Connect binds compositor/xdg_wm_base/seat/shm against a real compositor", "[wayland][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE(conn.Valid());
    REQUIRE(conn.Display() != nullptr);
    REQUIRE(conn.Compositor() != nullptr);
    REQUIRE(conn.XdgWmBase() != nullptr);
    REQUIRE(conn.Seat() != nullptr);
    REQUIRE(conn.Shm() != nullptr);
}

TEST_CASE("WaylandConnection::Connect fails gracefully (no crash) when no compositor is reachable", "[wayland][os]")
{
    const char* previous = engine_placeholder::os::GetEnvironment("WAYLAND_DISPLAY");
    ::setenv("WAYLAND_DISPLAY", "engine-test-nonexistent-socket", 1);

    WaylandConnection conn = WaylandConnection::Connect();
    REQUIRE_FALSE(conn.Valid());
    REQUIRE(conn.Display() == nullptr);

    if (previous != nullptr)
    {
        ::setenv("WAYLAND_DISPLAY", previous, 1);
    }
    else
    {
        ::unsetenv("WAYLAND_DISPLAY");
    }
}

TEST_CASE("WaylandConnection::Connect logs each bound global's interface name and version", "[wayland][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    g_WaylandConnectionTest_capture_len = 0;
    LogPlatform.SetVerbosity(LogVerbosity::Info);
    SetWriteSinkForTesting(&WaylandConnectionTestCapturingWriteSink);

    WaylandConnection conn = WaylandConnection::Connect();

    SetWriteSinkForTesting(&Write);
    LogPlatform.SetVerbosity(LogVerbosity::Warn);

    REQUIRE(conn.Valid());
    StringView captured(g_WaylandConnectionTest_capture_buffer, g_WaylandConnectionTest_capture_len);
    REQUIRE(captured.Contains(StringView("wl_compositor")));
    REQUIRE(captured.Contains(StringView("xdg_wm_base")));
    REQUIRE(captured.Contains(StringView("wl_seat")));
    REQUIRE(captured.Contains(StringView("wl_shm")));
}

TEST_CASE("WaylandConnection move construction/assignment transfers ownership without double-free", "[wayland][os]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    WaylandConnection a = WaylandConnection::Connect();
    REQUIRE(a.Valid());
    wl_display* display = a.Display();

    WaylandConnection b(engine_placeholder::Move(a));
    REQUIRE_FALSE(a.Valid());
    REQUIRE(b.Valid());
    REQUIRE(b.Display() == display);

    WaylandConnection c;
    c = engine_placeholder::Move(b);
    REQUIRE_FALSE(b.Valid());
    REQUIRE(c.Valid());
    REQUIRE(c.Display() == display);
}
