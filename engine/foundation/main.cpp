// Real entry point for the `engine_main`/`engine_main_asan` executables.
// Foundation-layer verification lives in /tests/foundation (Catch2,
// discovered via `ctest`) — this file has no test logic of its own.
#include "log/log.h"
#include "mem/arena_allocator.h"
#include "os/command_line.h"
#include "os/input.h"
#include "os/linux/platform_window_host.h"
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

// Parses a non-negative decimal integer; false on empty/non-digit/overflow-ish input.
bool ParseSeconds(StringView text, u64& out) {
    if (text.Length() == 0 || text.Length() > 6) {
        return false;
    }
    u64 value = 0;
    for (usize i = 0; i < text.Length(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + static_cast<u64>(text[i] - '0');
    }
    out = value;
    return true;
}

// Day 22/23 debug readout: logs InputState whenever something in it
// changed since the last frame (so a human at the keyboard/mouse can
// confirm each interaction lands, without a per-frame log flood).
struct InputSnapshot {
    f64 mouse_x = 0.0;
    f64 mouse_y = 0.0;
    bool inside = false;
    u32 buttons = 0;
    u32 modifiers = 0;
    f64 scroll_dx = 0.0;
    f64 scroll_dy = 0.0;
    u32 keys_down = 0;
};

u32 CountKeysDown(const os::KeyboardState& kb) {
    u32 n = 0;
    for (usize i = 1; i < os::KeyboardState::kKeyCount; ++i) {
        if (kb.IsDown(static_cast<os::Key>(i))) {
            ++n;
        }
    }
    return n;
}

void LogInputIfChanged(const os::InputState& in, InputSnapshot& prev) {
    InputSnapshot cur;
    cur.mouse_x = in.mouse_x;
    cur.mouse_y = in.mouse_y;
    cur.inside = in.pointer_inside;
    cur.buttons = in.mouse_buttons;
    cur.modifiers = in.keyboard.modifiers;
    cur.scroll_dx = in.scroll_dx;
    cur.scroll_dy = in.scroll_dy;
    cur.keys_down = CountKeysDown(in.keyboard);

    bool changed = cur.mouse_x != prev.mouse_x || cur.mouse_y != prev.mouse_y || cur.inside != prev.inside ||
                   cur.buttons != prev.buttons || cur.modifiers != prev.modifiers ||
                   cur.scroll_dx != prev.scroll_dx || cur.scroll_dy != prev.scroll_dy ||
                   cur.keys_down != prev.keys_down;
    if (changed) {
        ArenaAllocator scratch(512);
        StringBuilder<ArenaAllocator> line(scratch);
        line.Append("input: mouse=(").AppendFloat(cur.mouse_x, 1).Append(',').AppendFloat(cur.mouse_y, 1);
        line.Append(") inside=").AppendUInt(cur.inside ? 1 : 0);
        line.Append(" buttons=0x").AppendHex(cur.buttons);
        line.Append(" scroll=(").AppendFloat(cur.scroll_dx, 1).Append(',').AppendFloat(cur.scroll_dy, 1);
        line.Append(") mods=0x").AppendHex(cur.modifiers);
        line.Append(" keys_down=").AppendUInt(cur.keys_down);
        line.Append(" W=").AppendUInt(in.keyboard.IsDown(os::Key::W) ? 1 : 0);
        line.Append('\n');
        os::Write(1, line.CStr(), line.View().Length());
    }
    prev = cur;
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

    // `--log-level=error|warn|info|debug|trace` sets every channel's
    // verbosity (default Warn hides e.g. resize and decoration lines).
    for (usize i = 0; i < cmd_line.FlagCount(); ++i) {
        if (cmd_line.FlagName(i) == StringView("log-level") && cmd_line.FlagHasValue(i)) {
            StringView v = cmd_line.FlagValue(i);
            LogVerbosity level = LogVerbosity::Warn;
            bool known = true;
            if (v == StringView("error")) level = LogVerbosity::Error;
            else if (v == StringView("warn")) level = LogVerbosity::Warn;
            else if (v == StringView("info")) level = LogVerbosity::Info;
            else if (v == StringView("debug")) level = LogVerbosity::Debug;
            else if (v == StringView("trace")) level = LogVerbosity::Trace;
            else known = false;
            if (known) {
                LogCore.SetVerbosity(level);
                LogRender.SetVerbosity(level);
                LogPlatform.SetVerbosity(level);
            }
        }
    }

    // Day 26: backend selection. Precedence: `--backend=` flag, then the
    // ENGINE_WINDOW_BACKEND env var, then auto-detection ($WAYLAND_DISPLAY
    // -> Wayland, falling back to $DISPLAY -> X11). Failing to open a
    // window is non-fatal (logged via LogPlatform, the frame loop still
    // runs) — same "fail clearly, don't crash" pattern as Day 17.
    os::InputState input_state;
    os::PlatformWindowHost window_host;
    {
        os::WindowBackend requested = os::WindowBackend::Invalid;
        bool have_flag = false;
        for (usize i = 0; i < cmd_line.FlagCount(); ++i) {
            if (cmd_line.FlagName(i) == StringView("backend") && cmd_line.FlagHasValue(i)) {
                requested = os::ParseWindowBackend(cmd_line.FlagValue(i));
                have_flag = true;
            }
        }
        if (!have_flag) {
            window_host.OpenFromEnvironment("re-engine", 1280, 720, &input_state);
        } else if (requested == os::WindowBackend::Invalid) {
            Log(LogPlatform, LogVerbosity::Error, "Window: unrecognized --backend (want auto|wayland|x11)");
        } else {
            window_host.Open(requested, "re-engine", 1280, 720, &input_state);
        }
    }
    InputSnapshot prev_input;

    // Day 14: frame-timed main loop, target ~60Hz. Still wall-clock
    // bounded (so `engine_main` terminates on its own with no compositor
    // interaction, e.g. in automated/headless runs) but Day 20 adds a
    // second, earlier exit: the window's xdg_toplevel close event, via
    // WaylandWindow::ShouldClose().
    constexpr u64 kTargetFrameNanos = 16'666'667;
    // Day 23: `--run-seconds=N` extends (or shortens) the wall-clock
    // bound so the window can be exercised for as long as a manual
    // check needs; default stays 6s so automated runs still terminate.
    // Per-frame delta-time lines are noise by default; `--frame-times`
    // turns them back on (Day 14's original readout).
    bool print_frame_times = cmd_line.HasFlag("frame-times");
    u64 run_seconds = 6;
    for (usize i = 0; i < cmd_line.FlagCount(); ++i) {
        if (cmd_line.FlagName(i) == StringView("run-seconds") && cmd_line.FlagHasValue(i)) {
            u64 parsed = 0;
            if (ParseSeconds(cmd_line.FlagValue(i), parsed)) {
                run_seconds = parsed;
            }
        }
    }
    const u64 kRunDurationNanos = run_seconds * 1'000'000'000ull;

    u64 run_start = os::MonotonicTimeNanos();
    u64 last = run_start;
    while (os::MonotonicTimeNanos() - run_start < kRunDurationNanos) {
        u64 now = os::MonotonicTimeNanos();
        u64 delta_nanos = now - last;
        last = now;

        if (print_frame_times) {
            ArenaAllocator scratch(256);
            StringBuilder<ArenaAllocator> line(scratch);
            line.Append("delta_ms: ").AppendFloat(static_cast<f64>(delta_nanos) / 1'000'000.0, 3).Append('\n');
            os::Write(1, line.CStr(), line.View().Length());
        }

        if (os::Window* active = window_host.Get()) {
            input_state.BeginFrame();
            active->PumpEvents();
            LogInputIfChanged(input_state, prev_input);
            if (active->ShouldClose()) {
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
