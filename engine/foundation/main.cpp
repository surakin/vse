// Real entry point for the `engine_main`/`engine_main_asan` executables.
// Foundation-layer verification lives in /tests/foundation (Catch2,
// discovered via `ctest`) — this file has no test logic of its own.
#include <poll.h>

#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/linux/wayland/wayland_connection.h"
#include "os/linux/wayland/wayland_window.h"
#include "os/os.h"
#include "string/string.h"

using namespace engine_placeholder;

namespace {

void WriteLine(StringView label, StringView value) {
    ArenaAllocator scratch(256);
    StringBuilder<ArenaAllocator> line(scratch);
    line.Append(label).Append(value).Append('\n');
    os::Write(1, line.CStr(), line.View().Length());
}

// Non-blocking pump of one Wayland display's events: reads whatever's
// already available on the socket (never blocks, per the poll(..., 0)
// timeout) and dispatches it. Uses the prepare_read/read_events pair
// rather than wl_display_dispatch_pending() alone, since dispatch_pending
// only drains what a *previous* read already buffered — without this,
// events the compositor sends after the initial connect/window-mapping
// round-trips (resize/close among them) would never actually be read off
// the socket, silently starving Day 20's event handling. Blocking
// wl_display_dispatch() isn't used instead because this loop must keep
// meeting its frame budget (and, for engine_main run with no human at
// the compositor, terminate on its own) even when the compositor sends
// nothing.
void PumpWaylandEvents(wl_display* display) {
    while (wl_display_prepare_read(display) != 0) {
        wl_display_dispatch_pending(display);
    }
    wl_display_flush(display);

    pollfd pfd{};
    pfd.fd = wl_display_get_fd(display);
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0) {
        wl_display_read_events(display);
    } else {
        wl_display_cancel_read(display);
    }
    wl_display_dispatch_pending(display);
}

} // namespace

int main(int argc, char** argv) {
    os::StashNativeCommandLine(argc, argv);

    ArenaAllocator cmd_line_arena(4096);
    os::CommandLine<ArenaAllocator> cmd_line = os::CommandLine<ArenaAllocator>::FromNative(cmd_line_arena);

    WriteLine("program: ", cmd_line.ProgramName());
    for (usize i = 0; i < cmd_line.ArgCount(); ++i) {
        WriteLine("arg: ", cmd_line.Arg(i));
    }
    for (usize i = 0; i < cmd_line.FlagCount(); ++i) {
        ArenaAllocator scratch(256);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("flag: --").Append(cmd_line.FlagName(i));
        if (cmd_line.FlagHasValue(i)) {
            line.Append('=').Append(cmd_line.FlagValue(i));
        }
        line.Append('\n');
        os::Write(1, line.CStr(), line.View().Length());
    }

    // Day 18-19: connect to the compositor and map a window. Neither
    // failing is fatal here (logged via LogPlatform, not crashed) — the
    // frame-timed loop below still runs either way, matching this file's
    // existing "fail clearly, don't crash" pattern (Day 17).
    os::wayland::WaylandConnection wayland_connection = os::wayland::WaylandConnection::Connect();
    os::wayland::WaylandWindow window;
    if (wayland_connection.Valid()) {
        window = os::wayland::WaylandWindow::Create(wayland_connection, "re-engine", "re_engine");
    }

    // Day 14: frame-timed main loop, target ~60Hz. Still wall-clock
    // bounded (so `engine_main` terminates on its own with no compositor
    // interaction, e.g. in automated/headless runs) but Day 20 adds a
    // second, earlier exit: the window's xdg_toplevel close event, via
    // WaylandWindow::ShouldClose().
    constexpr u64 kTargetFrameNanos = 16'666'667;
    constexpr u64 kRunDurationNanos = 6'000'000'000;

    u64 run_start = os::MonotonicTimeNanos();
    u64 last = run_start;
    while (os::MonotonicTimeNanos() - run_start < kRunDurationNanos) {
        u64 now = os::MonotonicTimeNanos();
        u64 delta_nanos = now - last;
        last = now;

        ArenaAllocator scratch(256);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("delta_ms: ").AppendFloat(static_cast<f64>(delta_nanos) / 1'000'000.0, 3).Append('\n');
        os::Write(1, line.CStr(), line.View().Length());

        if (wayland_connection.Valid()) {
            PumpWaylandEvents(wayland_connection.Display());
            if (window.ShouldClose()) {
                break;
            }
        }

        u64 elapsed = os::MonotonicTimeNanos() - now;
        if (elapsed < kTargetFrameNanos) {
            os::SleepNanos(kTargetFrameNanos - elapsed);
        }
    }

    return 0;
}
