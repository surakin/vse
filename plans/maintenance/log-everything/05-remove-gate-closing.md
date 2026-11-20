# Log everything / Day 5: remove `os::Print*`, the gate, the closing runs and the review

**Series:** [log everything](../log-everything.md)
**Depends on:** Days 1 to 4.

## Tasks

- [x] Remove `Print`, `PrintError`, `PrintText`, `PrintErrorText`, `PrintLine`, `PrintBytes`, `PrintErrorBytes` from `os/console.h` (the compiler is the gate:
  nothing can print around the log system); update its header comment and `docs/reference`, and drop `<cstdio>` from the allow list entry if nothing needs it.
- [x] CLAUDE.md: the rule "output goes through `out::`" in the wrappers table (replace `os::Print` in the console row) and `docs/guide/foundation-logging-and-assert.md`
  ("Common mistakes": printing with `os::Print`).
- [x] Closing runs: `scripts/test-phase.sh --affected` plain, asan and tsan (this series touches every program: also `--all` plain), `ctest -L docs`,
  `docgen samples`, Clang and GCC unity builds and Clang non-unity; quote the `ran X of Y` lines.
- [x] `/code-review high` over the series; findings recorded below. `STATUS.md`, `KNOWN-ISSUES.md` (the Windows text-mode difference: the C library no longer
  turns `\n` into `\r\n` on output; the PC runs are the user's).

**Done:** `os::Print*` is gone, everything prints through `out::`, the runs and the review are recorded.

## Notes (2026-11-20, Linux)

- `os/console.h` no longer has `Print*`; it keeps `Flush`, `FlushError`, `FlushAll`, `Abort`, `ExitNow`, `GetEnvironment`. CLAUDE.md's wrappers table and the guides name `out::Print` and `Log`. The compiler is the gate: nothing can print around the log system.

## Post-implementation code review

`/code-review high` over the series' diff (2026-11-20): 5 findings reported.

- **Fixed:** `out::PrintLine` emitted the text and its newline as two records, so two threads could interleave a line break away from its line; it now emits one record when the line fits (regression test "PrintLine reaches the console as one write").
- **Declined, recorded in `KNOWN-ISSUES.md`:** the 16 KiB stack buffer and one write per `Print` (the same syscall count as `os::Print` had; buffering would change when output appears relative to stderr); Windows newline translation of console output (not built there); only `script_demo`'s diagnostics moved to `Log`, other `PrintError` calls stay bare stderr output (no channel to give them); `ConsoleLogMode::Off` silences program output as well as log lines (documented in the guide).

Closing runs (Linux, 2026-11-20), each partial by design:

- plain: `ran 4370 of 11706 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 42 s`
- asan: `ran 3572 of 11706 tests (phases: all; flavors: asan; display: included; crash tests: excluded; soak: excluded) in 63 s`
- tsan: `ran 3222 of 11706 tests (phases: all; flavors: tsan; display: included; crash tests: excluded; soak: excluded) in 71 s`

Failures: the known ones (`engine_c_surface`, "A Job destroyed without being released", the audio snapshot cost) and the rhi/Vulkan tests, which fail on this machine because no GPU is found; one test of this series ("two threads printing whole lines") failed on a non-thread-safe test sink and was fixed (mutex), then passed.
