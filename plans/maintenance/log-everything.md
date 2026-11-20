# Maintenance: everything goes through the log system

This is not a numbered phase: it is a maintenance series of five days, ordered by the user on 2026-11-20 (follows [log devices](log-devices.md); no phase
or day number changes).

**Origin:** after the log devices were built, the user tried `script_demo` and saw no colours: the demos, tools and tests print with `os::Print`,
`os::PrintError` and friends (about 690 calls: `game/` 196, `tools/` 266, `tests/` 113, the docs' samples 134, one in `engine/audio`), straight to the C
streams, past every log device. The user's decision: **everything goes through the log system** (scope: all of it, tests and docs included), and
**program output goes to the devices without a `[Channel] LEVEL:` prefix** (the "output channel"), so output stays byte-identical and the goldens, the
parsed outputs and the help texts do not change.

**Depends on:** [log devices](log-devices.md).

**Design:**
- `log/output.h`, namespace `vse::out`: `Print`, `PrintError`, `PrintText`, `PrintErrorText`, `PrintLine`, `PrintBytes`, `PrintErrorBytes`, `Flush`,
  `FlushError`: the same shapes as the `os::` functions they replace, so the migration is a rename. An output record is a `LogRecord` of kind `Output`
  (no channel, no prefix, no ring entry, no observer), on stream 1 (standard output) or 2 (standard error).
- Every device sees it: the console devices write the bytes unchanged to their stream (the coloured one adds no colour and escapes nothing, because
  tools print tabs, tables and binary), the file device appends them, an added device gets them at level `Info`.
- Output is not buffered by the C library any more (one write per call), so it interleaves correctly with log lines; a formatted call is formatted into
  a 16 KiB stack buffer (longer text is cut, with a marker) and `PrintText`/`PrintBytes` chunk without a limit.
- `os::Print*` are removed from `os/console.h` (the compiler is the gate); `Flush`, `FlushError`, `FlushAll` stay for C streams written by libraries.

## Days

1. [The output channel in the log core](log-everything/01-output-channel.md)
2. [Demos and benches (`game/`)](log-everything/02-game.md)
3. [Tools (`tools/`) and their golden and CLI tests](log-everything/03-tools.md)
4. [Tests, `engine/audio` and the documentation samples](log-everything/04-tests-engine-docs.md)
5. [Remove `os::Print*`, the gate, the closing runs and the review](log-everything/05-remove-gate-closing.md)

## Rules for every day

As [log devices](log-devices.md): `docgen lint` at zero for public headers, `STATUS.md` and `KNOWN-ISSUES.md` in the same commit, the wrappers only (no
`std`). **Tests run once, at the end (Day 5)** (the user, 2026-11-20, "skip the tests until the end"); each day builds clean in the Clang unity tree.
End-of-series `/code-review high`.
