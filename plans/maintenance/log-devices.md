# Maintenance: log devices

This is not a numbered phase: it is a maintenance series of four days, chosen by the user on 2026-11-20 over inserting a phase. It does not change
`CURRENT_PHASE` or any phase or day number.

**Origin:** the log core (`engine/foundation/log/log.{h,cpp}`) sends every line through one global function pointer (`g_write_sink`, default
`os::Write` to fd 1), plus one observer slot (`SetLogObserver`, used by `engine_c`). A program cannot send logs to a file, a window or the network.
The user wants a **log device interface**, stdout by default, any other output pluggable.

**Depends on:** Phase 7 (the ring of recent lines, error traces, the crash path), Phase 9 (`io::File`), Phase 11 (settings), Phase 12 (layering:
`log` is below `io`), Phase 23 (one registry per process, shutdown removal).

**Decisions (the user, 2026-11-20):** several devices active at once; a maintenance series; the first series builds the interface, the stdout
device and a **file** device. Memory (capture), network (`engine/net`) and GUI (`engine/ui_window`) devices come later, each in its own layer,
through the same `AddLogDevice`.

**Goal:** `LogDevice` (an abstract class), a fixed registry of non-owning devices with a minimum level each, `StdoutLogDevice` as the default,
`FileLogDevice` with rotation, and settings to choose them.

**Definition of done:**
- Every day's boxes are checked.
- The line format, the 256-line crash ring, the error traces, `SetLogObserver` and `SetWriteSinkForTesting` behave as before; the crash and
  assert path still writes straight to fd 1 and stays signal-safe.
- The log core still includes nothing above layer 3 (`module_deps_check`); the file device is in the io layer.
- New tests pass plain, asan and tsan; `docgen lint` is at zero; `ctest -L docs` passes.

## Days

1. [LogDevice, the registry and the stdout device](log-devices/01-log-device-registry-and-stdout.md)
2. [The file device with rotation](log-devices/02-file-log-device.md)
3. [Settings, the C API observer as a device, shutdown](log-devices/03-settings-observer-and-shutdown.md)
4. [A capture device for tests, documentation and the closing runs](log-devices/04-capture-device-docs-and-closing.md)

## Rules for every day

- Start with `scripts/test-phase.sh --start-day`; run `--affected` and, because the series touches concurrency, `--affected --flavor asan` and
  `--flavor tsan`; quote the script's `ran X of Y` line in the day file and in `STATUS.md`.
- `docgen lint` at zero and `docs/reference/` regenerated for any public header; Day 4 writes the guide text.
- End-of-day `/code-review high`, recorded under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit.
- The wrapped types only (`Atomic`, `Mutex`, `StringView`, `FormatTo`, `UniquePtr`); no `std`.
