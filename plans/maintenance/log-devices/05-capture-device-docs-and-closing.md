# Log devices / Day 5: a capture device for tests, documentation and the closing runs

**Series:** [log devices](../log-devices.md)
**Depends on:** Days 1 to 4.

## Tasks

- [x] **`CaptureLogDevice`** in `tests/support/` (test-only; a `Vector` of records under a `Mutex`; `Lines()`, `Clear()`), with its own test.
- [x] **Migrate** two or three of the `SetWriteSinkForTesting` users that only read log lines (`tests/foundation/log_test.cpp`,
  `tests/ui/debug_overlay_test.cpp`) to the capture device; the assert and crash tests keep `SetWriteSinkForTesting` (they test fd 1).
- [x] **Guide**: `docs/guide/foundation-logging-and-assert.md`, the narrative for log devices (what a device is, adding one, thread rules, why
  the crash path bypasses the table, an example of writing a device) with a compiling sample. Written here because it is the series' last day.
- [x] **`PLAN.md`** progress log and assumption #10 note; **`STATUS.md`**; **`KNOWN-ISSUES.md`** (record that memory, network and GUI devices are
  not built, and the Windows/macOS runs still owed).
- [x] **Closing runs**: `scripts/test-phase.sh --affected` in plain, asan and tsan (quote the `ran X of Y` lines); `ctest -L docs`; the Clang and GCC
  builds in unity and `-DENGINE_UNITY_BUILD=OFF`.
- [x] **Series review**: `/code-review high` over the series' diff; findings recorded below.

**Done:** tests no longer need the global write sink to read log lines; the guide explains the devices; the closing runs are recorded.

## Notes (2026-11-20, Linux)

- `tests/support/capture_log_device.h` (`engine_test::CaptureLogDevice`), used by `log_device_test.cpp`; `tests/ui/debug_overlay_test.cpp` now silences the
  console with `SetConsoleLogMode(Off)` instead of replacing the write sink. `log_test.cpp` and the assert and crash tests keep
  `SetWriteSinkForTesting`: they test the sink itself.
- Guide: `docs/guide/foundation-logging-and-assert.md`, section "Log devices", with a compiling sample.

## Post-implementation code review


Run `/code-review high` on the series' diff (2026-11-20): 9 findings.

- **Fixed:** `Log()` touched the busy counter of all 8 slots on every line, so a remover could wait without end under steady logging and every line paid 16 contended atomics (now an empty slot costs one load; the slot is re-read after the increment); `TtyLogDevice::Write` had no bound on a record longer than a `Log` line (now cut; test); the trace was asked for more bytes than `kLogTraceBytes` and then clamped by the console devices only (now capped at the source; test); the plain console device copied line and trace into a second buffer (now one write from `Log`'s buffer when they are contiguous); the timestamp was read for every line (now only when a device is added); attaching a second `Settings` with no log path removed the first one's file device (now only a change or a set path replaces it; test).
- **Declined, recorded in `KNOWN-ISSUES.md`:** Windows leaves the console's VT mode on (the PC's to settle, with the unbuilt Windows half); the `Auto` selection has no test (needs a detection seam or a pty); `FileLogDevice` lines carry no timestamp (an option for later, with the formatting of records); file I/O under the device's own lock (one lock per device is the contract).
- The tests of the series were run once at the end, as the user asked: `ran 4355 of 11689` (plain), `3571 of 11689` (asan), `3221 of 11689` (tsan); the failures are all already in `KNOWN-ISSUES.md` (`engine_c_surface` in a Release tree, the Job-destroyed test, the X11 focus-loss test, the audio cost snapshot). Build: Clang and GCC unity, Clang non-unity. After the review fixes the `[log]` tests were run again in plain, asan and tsan (settings, io and foundation).

