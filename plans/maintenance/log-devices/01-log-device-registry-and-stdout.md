# Log devices / Day 1: LogDevice, the registry and the stdout device

**Series:** [log devices](../log-devices.md)
**Depends on:** nothing in the series.

Today `Log()` (`engine/foundation/log/log.cpp`) formats the line, appends it to the ring, calls the observer, then calls `g_write_sink(1, ...)`.

## Tasks

- [ ] **Tests first** (`tests/foundation/log_device_test.cpp`, tag `[log][device]`; line in `tests/phases.txt`):
  - a line reaches every registered device whose minimum level admits it, and none whose level does not;
  - the record carries channel name, level, message, the formatted line and a timestamp;
  - `RemoveLogDevice` stops delivery and returns only after in-flight writes finish (two threads: one logs in a loop, one removes);
  - the table holds 8 devices; the ninth `AddLogDevice` fails by return value (`Fails:`), a duplicate is refused;
  - with the default registration, output on fd 1 is byte-identical to today's (capture through `SetWriteSinkForTesting`);
  - the ring holds the line whether or not a device is registered; the error trace stays in the same write;
  - the assert path writes to fd 1 with the table empty.
- [ ] **`log/log_device.h`** (layer 3, no allocator, no `io`): `LogRecord`; `class LogDevice` with inline virtuals `Write(const LogRecord&)`
  (pure), `Flush()` (default empty), `~LogDevice()` (so the class has no key function, as `io::File`); `AddLogDevice(device, min_level)`,
  `RemoveLogDevice(device)`; `///` comments with the contract labels (`Fails:`, thread-safety: `Write` may run on several threads at once).
- [ ] **The registry** in `log.cpp`: a fixed table of 8 slots (`Atomic` pointer and level each), a busy counter per slot as the observer has,
  so `RemoveLogDevice` waits; no allocation, usable during static initialization; one table per process.
- [ ] **`StdoutLogDevice`**: writes the formatted line with the log core's write sink (`g_write_sink`), so `SetWriteSinkForTesting` keeps
  capturing it; a process-lifetime instance registered at the first `Log()` call, at `Debug`-and-above (channel verbosity still filters first).
- [ ] **Route `Log()`** through the table after the ring append and the observer; the observer's "handled" result still suppresses the
  console device only (`StdoutLogDevice`), not other devices (record this rule in the header).
- [ ] **Signal safety**: the assert and crash report keep their direct `g_write_sink(1, ...)` writes; `signal_safety_check` passes.
- [ ] **Docs**: `docs/reference/` regenerated; `docs/guide/foundation-logging-and-assert.md` gets a reference-style paragraph (facts only).
- [ ] **Standing rows** (series rules).

**Done:** `Log()` delivers to a table of devices; with only the stdout device the output is unchanged; the new tests and the existing log and
debug tests pass plain, asan and tsan; the review is recorded below.

## Post-implementation code review

(to be written)
