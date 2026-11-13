# Phase 21 / Day 292 — Debugging III: Dumps, Symbol Store and Scripts

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 291.

Minidumps, core files, the dSYM symbol store and the shell scripts on macOS: the last third of the debugging backend.

## Tasks

- [x] **`crash_extract`'s macOS resolver** (moved from Day 290, which had no macOS crash report to resolve yet): `tools/crash_extract/resolve.cpp`'s
      platform branch resolves a macOS report's frames through the dSYM, with `atos` as a cross-check in the test when present.
      **Amended (Day 292):** the resolver is `lldb`, run by `scripts/crash-report.sh`, as on Linux (where `--resolve` is not built either);
      `crash_extract` gets what `lldb` needs instead. An in-process resolver like Windows' would have needed a second libbacktrace build (its
      Mach-O reader only reads images loaded in the running process) and a new exception to the library-boundary gate, while `lldb` already
      reads the dump with the store's dSYMs once two things are fixed: Breakpad's macOS writer pads the module and thread lists after their
      count (`crash_extract` read every module 4 bytes early: base `0x06a3d000…`, size 1), and it flags each context as holding no register
      (so `lldb` put every thread at pc 0). `ReadDumpModules` now reads the padded list, and `crash_extract --lldb <dump-or-envelope> <out>`
      writes a copy with the flags completed (`CompleteMacContextFlags`). The test names the faulting frame with `lldb` through the dSYM and
      cross-checks that address with `atos`.
- [x] **Minidumps:** Breakpad writes a minidump on macOS into `CrashDumpDirectory()`; `crash_extract` reads it (the module list carries the `LC_UUID`s) and
      resolves frames through the store; a test crashes a child and checks the dump's modules and the resolved top frame.
      (`tests/debug/macos/scripts_macos_test.cpp`: the program is the first module, its id the report's `build id:` and age 0, the exception
      `EXC_BAD_ACCESS`, three contexts completed; the top frame resolved through the store by `crash-report.sh`. Synthesized dumps in
      `minidump_test.cpp` cover the padding and the flags on every system, cut and damaged copies included. System libraries have a zero
      id: they live in the dyld shared cache, with no file for Breakpad to read.)
- [x] **`EnablePostMortemDumps`:** (done on Day 291, which needed it for the post-mortem death tests that began to run there; `os/macos/debug/core_dump_macos.cpp`) `setrlimit(RLIMIT_CORE)` as Linux; report `Limited` naming `/cores` and `kern.coredump` (normally off) and the
      DiagnosticReports directory; the *post-mortem* group checks the reported destination, never that a core appeared (it is the system's setting).
- [x] **Symbol store:** `scripts/store-symbols.sh` on macOS stores dSYM bundles under `<store>/<name>/<UUID>/<name>.dSYM` (the UUID from
      `dwarfdump --uuid`), so `lldb`'s `target symbols add` and `crash_extract` find them; `split-debug.sh` uses `dsymutil` and `strip -S` instead of
      `objcopy`; both keep working on Linux (shared scripts: file the Linux run). (A universal dSYM is filed once per architecture with
      `ditto --arch`, tested on `debug_universal_program`, built for x86-64 and arm64; `crash-report.sh` finds a bundle by UUID alone, so a renamed
      copy of a program finds its own. The Linux branches are unchanged but for `readlink -f` in `find-core.sh`, now `realpath`; the Linux run is
      filed in `KNOWN-ISSUES.md`.)
- [x] **`crash-report.sh`, `find-core.sh`, `open-core.sh`:** BSD userland (`stat -f`, no `readlink -f`, `date` without `%N`), `/cores` rules instead of
      `coredumpctl`, `lldb` as the debugger; the script tests run on macOS and skip with a reason where a tool is missing. (The scripts run under
      `/bin/bash` 3.2, in a directory whose name has a space. `/cores` belongs to root on this Mac, so the core test uses a core `lldb` saves with
      `process save-core` and `ENGINE_CORE_PATTERN`, a new override of the system's pattern; a core written by the kernel was not tried, filed in
      `KNOWN-ISSUES.md`. `lldb` is asked which program a core belongs to on the core alone: given a program too, it uses that file whatever the
      core records.)
- [x] **Conformance groups** *dump directory* and *post-mortem* pass or skip with a reason; the whole `tests/debug` suite passes with `--crashes`.
- [x] **Documentation:** `foundation-debugging.md` and `porting-debugging.md` macOS sections completed (store layout, the scripts, `lldb` commands);
      `os/macos/debug/BACKEND.md` fully marked as built; `debug_backend_lists` passes; `docgen lint` zero. (`docgen lint` and `ctest -L docs` do not
      run on the Mac before Day 293; no public header changed, and the guides' check is filed for the Linux machine. `BUILD.md`: the formatter
      and the scripts' tools.)
- [x] **Runs:** `scripts/test-phase.sh --affected --crashes --flavor all` (quote); Linux run of the shared scripts filed.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the scripts on paths with spaces (`~/Library/Application Support`), quoting, the
      store's handling of a dSYM with several architectures.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of `os/macos/debug/BACKEND.md` is built, the whole `tests/debug` suite passes or skips with a reason on macOS in three flavors
with crash tests included, a minidump from a crashed child is resolved through the dSYM store, and the scripts work on both macOS and Linux.

## Result

Done on 2026-11-12 on the Mac. Every function of `os/macos/debug/BACKEND.md` is built. A minidump from a crashed child is read by
`crash_extract` (the program first, with its `LC_UUID`), turned into a trace by `crash-report.sh` and `lldb`, and a stripped copy of the program
gets its files and lines back from the dSYM store and not without it (the faulting address cross-checked with `atos`). The scripts run on macOS's
bash 3.2 in directories whose names have spaces; their Linux branches wait for the Linux machine (`KNOWN-ISSUES.md`).

- **Found on the way:** Breakpad's macOS dumps pad the module and thread lists (`crash_extract --modules` printed every base 4 bytes off) and flag
  every context as holding no register (`lldb` showed each thread at pc 0). Fixed in `crash_extract` (`ReadDumpModules`, the new `--lldb` mode).
  Given a program beside a core, `lldb` uses that file whatever the core records, so `find-core.sh` asks about the core alone.
- **Found in the pulled main line:** its clang-tidy pass wrote `(a <=> b) < nullptr` in `tests/types/int128_test.cpp`, which libc++ rejects (the
  Mac's build stopped there). Fixed here with the literal `0` and a `NOLINT` reason; on the rebase the main line's own fix of the same day (the
  named orderings, `Ordering::less`) was kept instead. Phase 20's `net_access_check` tests run on the Mac and needed a
  macOS flavor line in `tests/phases.txt`. The Command Line Tools' Apple `clang-format` 17 is not the main line's formatter (it differs from 23.1.4 and
  ignores `.clang-format-ignore`); the day's files were formatted with it one by one, each diff checked, and 23.1.4 is not installed here
  (`BUILD.md`, `KNOWN-ISSUES.md`).
- **Tests added:** `tests/debug/macos/scripts_macos_test.cpp` (11 cases, three flavors; the core cases plain only), `debug_universal_program` (x86-64
  and arm64), five synthesized-dump cases in `minidump_test.cpp` (every system).
- **Runs (macOS, the script, crash tests included):** before the review `ran 1354 of 1354 tests (phases: all; flavors: all; display: included; crash
  tests: included) in 333 s`, the one failure `A child that writes without pause is still killed at the timeout [tsan]` (flaky under load, Day 291);
  after it `ran 1363 of 1363 tests (phases: all; flavors: all; display: included; crash tests: included) in 242 s`, three failures, all timing tests
  recorded as flaky under load (that one, `Throughput scales with participant count`, and `Benchmark: the wrappers cost what the standard types
  cost [tsan]`, new in that flavor), each passing when run again alone (`KNOWN-ISSUES.md`). Non-unity build clean.

## Post-implementation code review

`/code-review high` on the day's uncommitted changes: 9 findings; 7 fixed, 1 fixed in part, 1 in part declined. Each fixed behavior has a test
shown to fail before the fix, except where noted.

1. **Fixed: `crash-report.sh`'s new `real_dir` resolved links in the directory only**, not a program that is itself a link, which on Linux changed
   the `readlink -f` behavior (a symlinked program would give `lldb` the wrong search path). Now `realpath` first, as `find-core.sh` does (finding 9,
   the duplicate helpers, is the same fix). Checked by hand with a linked program; Linux's run is filed.
2. **Fixed in part: `find-core.sh`'s fallback**, when no image path ends with the program's name, could take dyld or a system library and report a
   false "different build". It now takes the first image outside `/usr/lib` and `/System`. The scenario was not reproduced as such (with the
   program missing, `lldb` lists it first anyway); the new test covers a rename after the crash.
3. **Fixed: `CompleteMacContextFlags` failed the whole dump when one context did not fit**, so a dump cut short gave no trace at all. Contexts and
   list entries that do not fit are skipped; `Malformed` is kept for a damaged header or directory. Test: a thread context past the end.
4. **Fixed: on macOS a missing `crash_extract` stopped `crash-report.sh`** (exit 2) even for a plain dump. It warns and reads the dump as it is (and
   so does a dump the tool refuses). Test: `ENGINE_CRASH_EXTRACT=/nonexistent` on a `.dmp`.
5. **Fixed: `%N` in `kern.corefile` was matched with the full program name**; the kernel's is cut to 32 bytes (xnu's `p_name`, as the review says;
   not seen in a kernel core here). Test: a 38-character program name.
6. **Fixed: the store lookup forked per module and ran even without `lldb`**: one `awk` pass, only when `lldb`, a store and the tool are there.
7. **Fixed in part, rest declined: `--lldb` copied the whole dump** into a second buffer: it patches its own read buffer now. The check that the output
   is not the input stays, as in `Extract` (an envelope named twice would be replaced by the bare dump); it compares strings only, as `Extract` does.
8. **Fixed: `store-symbols.sh` deleted the stored bundle before copying the new one**: it copies beside it, swaps it in when complete, and exits 2
   with the old bundle kept if the copy fails. Test: an unreadable `Info.plist` in the source. That the test fails on the old code was not shown (the
   change that would put the old order back was refused by a safety check on `rm -rf` of a variable); the old script plainly removed first.
9. **Fixed (with 1): two path-resolving helpers.**
