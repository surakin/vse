# Log devices / Day 2: terminal detection and the TTY device

**Series:** [log devices](../log-devices.md)
**Depends on:** Day 1.

Added 2026-11-20 at the user's request: on a colour-capable terminal the console device is a **TTY device** that colours the lines, instead of the
plain stdout device. There is no terminal detection in the engine today (no `isatty`, no `NO_COLOR`), so the `os` module gains it first.

## Tasks

- [ ] **Tests first** (`tests/foundation/terminal_test.cpp` and `tests/foundation/tty_log_device_test.cpp`, tags `[os][terminal]`, `[log][tty]`;
  lines in `tests/phases.txt`):
  - `os::DetectTerminal(fd, env)` is a pure function of a descriptor-kind and an environment view, so it is tested without a terminal: not a
    terminal -> `None`; `NO_COLOR` set and non-empty -> `None`; `TERM=dumb` -> `None`; `COLORTERM=truecolor` -> `TrueColor`; `TERM` with
    `256color` -> `Color256`; other `TERM` -> `Color16`; `CLICOLOR_FORCE` set (and not `NO_COLOR`) -> colour even when not a terminal;
  - a real pipe and a regular file give `None`; a pty (the test opens one on Linux and macOS) gives colour with `TERM=xterm-256color`;
  - the TTY device writes `ESC[..m` colour per level (Error red, Warn yellow, Info default, Debug and Trace dim), resets at the end of every
    line, and a message containing an ESC byte or control characters is written with them escaped (no injection of terminal sequences);
  - the TTY device's output with the escapes stripped equals the stdout device's line, byte for byte;
  - the console choice: `ConsoleLogDevice` selection picks the TTY device on a colour terminal and the stdout device otherwise (decided from the
    detection result handed in, not the process's real fd).
- [ ] **`os/terminal.h`** (`engine/foundation/os/`, a wrapper per `std_usage_check`): `os::IsTerminal(fd)` (POSIX `isatty`; Windows
  `GetConsoleMode`, in `os/windows/`; macOS shares the POSIX file), `os::DetectTerminal(...)` (the pure rule above) and, on Windows, enabling
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING` for the output handle (detection says `None` if it cannot be enabled). `Backend:` labels and a section in
  each platform's `BACKEND.md` where the debugging-style rule asks for it. Environment is read with `os::GetEnvironment`; `NO_COLOR`, `CLICOLOR_FORCE`,
  `TERM` and `COLORTERM` are the conventional terminal variables, read before any `Settings` exists, so they get lines in
  `tools/config_access_check/allowlist.txt` (a reason each); `log.color` (Day 4) is the setting that overrides them.
- [ ] **`log/tty_log_device.{h,cpp}`** (layer 3, no `io`): `TtyLogDevice`, same `LogDevice` contract as the stdout device (writes through the log
  core's write sink so `SetWriteSinkForTesting` captures it); a small palette per level, optional dim channel name; the whole coloured line is built
  in the 512-byte stack buffer and written in one call so lines from threads do not interleave; a line that does not fit is truncated the way the
  plain line is.
- [ ] **Default console device**: at the first `Log()`, the registry installs the TTY device if `DetectTerminal(1, environment)` says colour, else the
  stdout device (never both). Crash and assert output stay plain: they write straight to fd 1 without colour (signal-safe, no environment read).
- [ ] **Docs**: header `///` comments, `docgen lint` at zero, `docs/reference/` regenerated, `docs/guide/platform-os.md` reference paragraph for the
  terminal functions.
- [ ] **Standing rows** (series rules).
- [ ] **Windows and macOS**: the Windows PC (VT enabling, `GetConsoleMode`) and the Mac builds and runs are the user's machines: list them in
  `KNOWN-ISSUES.md` if not done by the series' close.

**Done:** on a colour terminal the console device is coloured, anywhere else (pipe, file, `NO_COLOR`, `TERM=dumb`) it is the unchanged plain
line; tests pass plain, asan and tsan; the review is recorded below.

## Post-implementation code review

(to be written)
