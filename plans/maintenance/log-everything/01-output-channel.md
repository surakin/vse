# Log everything / Day 1: the output channel in the log core

**Series:** [log everything](../log-everything.md)
**Depends on:** [log devices](../log-devices.md).

## Tasks

- [x] **Tests first** (`tests/foundation/log_output_test.cpp`, tag `[log][out]`; line in `tests/phases.txt`):
  - `out::Print("%d-%s\n", 7, "x")` reaches the console sink as exactly `7-x\n` on fd 1; `PrintError` on fd 2; no prefix, no colour in the `Color` mode;
  - a control byte, a tab and an ESC in the text pass through unchanged on every console mode;
  - an added device receives it as a record of kind `Output` with the text, the stream and level `Info`; a device limited to `Warn` does not;
  - the observer is not called and the ring does not grow;
  - `PrintBytes` writes bytes with a NUL inside; `PrintText` of 100 KiB arrives whole (in chunks of at most 16 KiB);
  - `Print` of a result longer than the buffer is cut and ends with the marker; the return value is the length that was written;
  - `ConsoleLogMode::Off` silences the console and not an added device;
  - two threads printing whole lines do not split a line.
- [x] **`LogRecord`**: `kind` (`Log`, `Output`) and `stream` (1 or 2); the three console and file devices handle `Output` as above; `StdoutLogDevice`
  writes to `record.stream`; `WriteLogConsoleBytes` takes the descriptor.
- [x] **`log/output.h`** with the functions above (`///` comments, labels), and the registry dispatch shared with `Log()`.
- [x] **Docs**: header comments, `docgen lint` at zero, `docs/reference/` regenerated; the reference paragraph of the guide.
- [x] **Standing rows** (series rules).

**Done:** `out::Print*` exist, reach every device unchanged, and the tests are written; the Clang unity tree builds.

## Notes (2026-11-20, Linux)

- `log/output.{h,cpp}`: `out::Print`, `PrintError`, `PrintText`, `PrintErrorText`, `PrintLine`, `PrintBytes`, `PrintErrorBytes`, `Flush`, `FlushError`. Output is a `LogRecord` of kind `Output` with a `stream` (1 or 2); the console devices write it unchanged (the coloured one too), the file device appends it, an added device gets it at `Info`. Not in the ring, not seen by the observer. `detail::EmitOutput` is the one entry point.
- Also added `LogF(channel, level, format, ...)` (a formatted `Log`), used to turn the runtime diagnostics of `script_demo` into log lines.
- `ConsoleLogMode::Off` silences output on the console (the guide's sample showed it: it now mutes the console, then restores it before printing).
