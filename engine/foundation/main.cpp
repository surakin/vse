// Real entry point for the `engine_main`/`engine_main_asan` executables.
// Foundation-layer verification lives in /tests/foundation (Catch2,
// discovered via `ctest`) — this file has no test logic of its own.
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

    // Day 14: frame-timed main loop. Target ~60Hz; bounded to a few
    // seconds so `engine_main` still exits on its own — Day 20 replaces
    // this bound with "exit when the window's close event fires" once
    // there's a window to close. Extended from Day 14's original 3s to
    // 6s so the Day 19 window is on screen long enough to visually
    // confirm.
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

        // Minimal, non-blocking pump so the connection stays responsive
        // (e.g. xdg_wm_base pings) across the loop's lifetime — Day 20
        // replaces this with the real dispatch-driven event loop.
        if (wayland_connection.Valid()) {
            wl_display_dispatch_pending(wayland_connection.Display());
            wl_display_flush(wayland_connection.Display());
        }

        u64 elapsed = os::MonotonicTimeNanos() - now;
        if (elapsed < kTargetFrameNanos) {
            os::SleepNanos(kTargetFrameNanos - elapsed);
        }
    }

    return 0;
}
