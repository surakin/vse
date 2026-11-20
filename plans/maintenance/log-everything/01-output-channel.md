# Log everything / Day 1: the output channel in the log core

**Series:** [log everything](../log-everything.md)
**Depends on:** [log devices](../log-devices.md).

## Tasks

- [ ] **Tests first** (`tests/foundation/log_output_test.cpp`, tag `[log][out]`; line in `tests/phases.txt`):
  - `out::Print("%d-%s\n", 7, "x")` reaches the console sink as exactly `7-x\n` on fd 1; `PrintError` on fd 2; no prefix, no colour in the `Color` mode;
  - a control byte, a tab and an ESC in the text pass through unchanged on every console mode;
  - an added device receives it as a record of kind `Output` with the text, the stream and level `Info`; a device limited to `Warn` does not;
  - the observer is not called and the ring does not grow;
  - `PrintBytes` writes bytes with a NUL inside; `PrintText` of 100 KiB arrives whole (in chunks of at most 16 KiB);
  - `Print` of a result longer than the buffer is cut and ends with the marker; the return value is the length that was written;
  - `ConsoleLogMode::Off` silences the console and not an added device;
  - two threads printing whole lines do not split a line.
- [ ] **`LogRecord`**: `kind` (`Log`, `Output`) and `stream` (1 or 2); the three console and file devices handle `Output` as above; `StdoutLogDevice`
  writes to `record.stream`; `WriteLogConsoleBytes` takes the descriptor.
- [ ] **`log/output.h`** with the functions above (`///` comments, labels), and the registry dispatch shared with `Log()`.
- [ ] **Docs**: header comments, `docgen lint` at zero, `docs/reference/` regenerated; the reference paragraph of the guide.
- [ ] **Standing rows** (series rules).

**Done:** `out::Print*` exist, reach every device unchanged, and the tests are written; the Clang unity tree builds.
