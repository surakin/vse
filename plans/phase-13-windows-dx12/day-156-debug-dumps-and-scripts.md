# Phase 13 / Day 156 — Debugging III: Dumps, Symbol Store and Scripts

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 155.

Make a Windows crash readable after the fact: the dump directory, post-mortem dumps, a symbol store and the PowerShell versions of the crash scripts.

## Tasks

- [x] **`CrashDumpDirectory`** in use by the crash handler (Day 152's private directory); saved-dump pruning (`saved_dumps.cpp`) works on Windows paths and the
      test that checks it runs.
- [x] **`EnablePostMortemDumps`:** Windows Error Reporting `LocalDumps` for the program's own key (`DumpFolder`, `DumpType`, `DumpCount`); per-user
      (`HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\<exe>`) if WER honors it on this Windows build — test it — otherwise `Limited` with the
      note naming the `HKLM` values to set by hand; never writes outside the program's own key; the test removes what it wrote.
      *(Measured: WER ignores the per-user key on this build, so the function only reads the machine's key and writes nothing; see the notes.)*
- [x] **Symbol store:** `scripts/store-symbols.ps1` copies each PDB (and the executable, for minidump loading) into `<store>/<name>.pdb/<GUID+age>/<name>.pdb`
      (and `<name>.exe/<timestamp+size>/<name>.exe`), the layout WinDbg and Visual Studio read; the GUID+age is the build id from Day 154. Test: store the demo's
      symbols, read the id back from the store path.
- [x] **Scripts:** `scripts/crash-report.ps1` (the logic of `crash-report.sh`: find the newest dump and report, resolve frames against a symbol store using
      `crash_extract` and DbgHelp), `scripts/find-dump.ps1` (the WER and engine dump locations), `scripts/open-dump.ps1` (open in `cdb` or WinDbg with the store
      on the symbol path). The shell scripts stay for Linux; `crash_report_script_*` tests get Windows twins.
- [x] **`tools/crash_extract` on Windows:** reads a Windows minidump's module list and the engine report; a ctest crashes the demo (`--crash` test hook, or a
      death-test child) and checks the extracted report names the faulting function after resolution against the store.
- [x] **Human check (record in this file, or file in `KNOWN-ISSUES.md` if not done):** open one minidump in WinDbg/`cdb` with the stored symbols and confirm
      the call stack is readable. *(Filed: no debugger is installed on the PC.)*
- [x] **Remaining conformance groups:** *dump directory*, *post-mortem*, *symbols* — pass or skip with a reason; `debug_backend_lists` and
      `debug_cross_syntax_windows` pass. `BACKEND.md`'s every section is now marked implemented or explicitly not applicable.
- [x] **Documentation:** `docs/guide/porting-debugging.md` Windows order of work complete; `foundation-debugging.md` "Reading a crash on Windows" (where dumps
      are, the symbol store, the scripts); reference regenerated; `docgen lint` zero. *(The guides are updated; the reference for `post_mortem.h` and
      `docgen lint` wait for docgen on Windows, Day 157: filed.)*
- [x] **Runs:** Windows `scripts/test-phase.sh --affected --crashes` plain and asan; Linux `--affected` only if shared debug code changed (record); quote.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: a script that deletes or overwrites dumps it did not create, registry writes outside the
      program's key, and symbol-store paths built from untrusted dump contents.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function in the Windows debugging checklist is implemented or marked not applicable, a crash on Windows can be read back with symbols from the
store through the scripts, and every debugging conformance group passes or skips with a reason.

## Notes (2026-11-07, Windows PC)

- **`CrashDumpDirectory`** was already the crash handler's default (Day 152's private directory; the facade parses it in the native style since
  Day 155). New Windows twins in `crash_end_to_end_windows_test.cpp`: the default directory (`%LOCALAPPDATA%\<ENGINE_APP_NAME>\crashes`, an
  envelope there after a crash), a directory that cannot be created (`DirectoryFailed`, the program runs on), and a dump kept across two
  restarts (moved to `saved\`). `saved_dumps_test.cpp` (pruning) and `core_dump_test.cpp` (the neutral post-mortem cases) now build on Windows.
- **`EnablePostMortemDumps`** (`os/windows/debug/post_mortem.cpp`, replacing `crash_pending.cpp`). **Measured: WER does not read LocalDumps from
  HKCU on this build.** A child that cleared its error mode, asked WER for no window and a local-only report (`WerSetFlags(WER_FAULT_REPORTING_NO_UI
  | WER_FAULT_REPORTING_FLAG_QUEUE)`) and crashed without the engine handler left no dump in the folder its per-user key named, inside or outside
  the death-test job, from Git Bash and from PowerShell; a minimal probe program (no engine, no Catch2) did the same. WER did run each time and
  queued a report under `ProgramData\Microsoft\Windows\WER\ReportQueue` (nothing was sent); the three queued reports, the test keys and folders
  were removed. So the function writes nothing (HKLM needs an administrator and changes the machine for every user): it reads the program's
  LocalDumps key, or the machine-wide one, and reports `Enabled` with the folder and dump type, or `Limited` with the key and values to create, or
  `Limited` when the process's error mode (`SEM_NOGPFAULTERRORBOX`: every death-test child) or the machine (`Disabled`) keeps WER away. The
  task's "per-user if WER honors it" is thus the plan's own fallback. `post_mortem_windows_test.cpp` checks both machine states (this PC has no
  HKLM key) and that no key appears.
- **Symbol store:** `scripts/store-symbols.ps1` files `<pdb>\<GUID+age>\<pdb>` and `<exe>\<timestamp><size>\<exe>` (the layout WinDbg reads with
  `srv*<store>`), from `crash_extract --image`; the test reads the stored path back and finds the build id the dump records.
- **Scripts:** `crash-report.ps1` (the exception and modules, the report with its frames named from the store, exit 3 for a different build of the
  program, 64 for bad usage), `find-dump.ps1` (the engine's envelopes and WER's dumps, newest first; a name with a path in it is refused),
  `open-dump.ps1` (WinDbg, or cdb with `-Batch`; `srv*<store>` as the only symbol path and `_NT_SYMBOL_PATH` cleared; exit 66 without a
  debugger). Windows twins of the script tests: `crash_report_script_windows_test.cpp` (the mismatch case copies the other flavor of the test
  program under this one's name). `split-debug.sh` has no twin: a PDB is already separate from its image.
- **`crash_extract` on Windows:** built in the Windows tree (`foundation_io` and `dbghelp`); new modes `--modules`, `--report`, `--image`,
  `--resolve` (`tools/crash_extract/minidump.cpp`: the module list, the exception, a PE's CodeView record, all bounds-checked over a buffer;
  `resolve.cpp`: report lines and store paths, where a name from a dump must be one plain file name and the build id hex; `windows/resolve.cpp`,
  the tool's one per-platform directory (registered with `module_deps_check` and `platform_neutral_check`, `ARCHITECTURE.md` section 4):
  DbgHelp with the store as its only search path). `envelope.cpp` gained `FindAttachment` (the report the handler attaches, by file name).
  `minidump_test.cpp` builds a minidump byte by byte, cuts and damages it at every byte, and reads the test program's own image (its build id
  equals the crash context's). The ctest of the plan ("crashes the demo and checks the extracted report names the faulting function after
  resolution against the store") is `crash_report_script_windows_test.cpp`'s first case, with a death-test child in place of the demo (the
  programs are not on Windows yet).
- **Human check:** not done: neither WinDbg nor cdb is installed on the PC (filed in `KNOWN-ISSUES.md`). What was checked instead: the dump's
  frames resolve through DbgHelp from the stored PDB to `InjectFault` at `fault_injection.cpp:50`, the test function and the death-test runner.
- **Conformance groups:** *dump directory* (above), *post-mortem* (`core_dump_test.cpp`'s two neutral cases pass as `Limited` in a death-test
  child; the Windows cases above), *symbols* (`stack_trace` group from Day 154, plus the store). `debug_backend_lists` passes; the
  `debug_cross_syntax_windows` gate is a Linux ctest (a clang syntax check with the Windows triple) and runs at the phase close. Every
  `BACKEND.md` section is now **Built** or not applicable (libbacktrace).
- **Gates:** the tool's DbgHelp file sits in `tools/crash_extract/windows/`, a new `platform-dir` in `tools/module_deps_check/modules.txt` (and an
  owner of `os/windows/` for `win32.h`) and a Windows directory for `platform_neutral_check` rule 4; `ARCHITECTURE.md` section 4 names it. It checks
  the PDB's presence through the file layer (`io::Stat`), not `GetFileAttributesW`.
- **A death-test child hung at exit under ASan** (found by the day's full runs: the restart case timed out 2 times in about 75 ASan runs). A test-only
  watchdog (`tests/debug/hang_watchdog_windows.cpp`: after 20 s it suspends the other threads, prints their raw frames, and the parent names them)
  showed the main thread inside `ExitProcess` (`RtlExitUserProcess`, waiting) from `ExitChildProcess` -> `std::_Exit`. `ExitChildProcess` now ends
  the child with `TerminateProcess` on itself after flushing the C streams: no DLL detach and no atexit, which is what `_exit` does on Linux. Before:
  hung about 1 in 30 when 12 run at once; after: 0 of 240 and 0 of 120 (plain and ASan mixed). The watchdog stays in the two crash-handler children
  that showed hangs, so a future hang prints where it is.
- **A test collision, not a product bug:** the default-directory case used one application name for every process, so `ctest -j8`'s plain and ASan
  copies removed each other's dump (169 of 240 parallel runs failed); the name now carries the process id.
- **Runs** (after the review fixes): `scripts/test-phase.sh --affected --crashes` printed
  `ran 543 of 862 tests (phases: all; flavors: plain; display: included; crash tests: included) in 36 s`, with `--flavor asan`
  `ran 319 of 862 tests (phases: all; flavors: asan; display: included; crash tests: included) in 21 s` (one failure: `FramePool does not commit
  slab memory until blocks are used [asan]`, phase 2, the flake filed on Day 155; it passed in the full run that followed); `ctest --preset
  windows-msvc` 862 of 862, `windows-clang-cl` 573 of 573, non-unity 862 of 862. ASan-specific: the allocation check skips; every crash and
  script case passes in both flavors. No test folder or registry key was left behind.
- **For the Linux run at the phase close:** shared files changed today: `tools/crash_extract/` (`main.cpp`, `envelope.cpp` and `.h`, the new
  `minidump.*` and `resolve.*`; its `CMakeLists.txt`), `tests/debug/CMakeLists.txt` (the Linux test list gains `minidump_test.cpp` and the two
  parser sources), `tests/debug/minidump_test.cpp`, `debug/post_mortem.h` (a comment), `tools/module_deps_check/modules.txt`,
  `tools/platform_neutral_check/check.cmake`, `ARCHITECTURE.md`, the guides. On Linux: `scripts/test-phase.sh --affected --crashes` with asan and
  tsan, the `crash_extract_tool` ctest (the tool's argument handling changed), `ctest -L docs` after regenerating `post_mortem.h`'s reference.

## Post-implementation code review

`/code-review high` over the day's diff: **7 findings, 4 fixed, 1 not a defect, 2 declined**; the day's full runs found two more problems, both
fixed (above: the exit hang, the shared folder name).

Fixed: (1) the DbgHelp resolver broke three gates (an SDK header and a per-platform include in a neutral directory, a direct file call); moved
into `tools/crash_extract/windows/`, registered, and the file check goes through `io::Stat` (the gates are the regression test). (2)
`crash-report.ps1` failed (exit 2) for a dump without a report when a store was given; now a warning. (3) `open-dump.ps1` passed an empty `-y` to
cdb without a store. (4) The program's LocalDumps key name was built without checking for truncation; a name that does not fit now has no key
of its own. No regression tests for (2)-(4): no debugger is installed for (3), and (2) and (4) need inputs the script and function cannot be
given here without new test hooks.

Not a defect: (5) a suspected refusal of `RegGetValueW` to read a `REG_EXPAND_SZ` value: a new test against a throw-away HKCU key
(`DescribeLocalDumps`, now an internal function taking the registry root) passes with and without `RRF_NOEXPAND`; the explicit expansion stays
and the test stays as coverage.

Declined: (6) the restart case's rare hang, at first only recorded; it was then found and fixed (see the notes). (7) `crash_extract --resolve`
matches a report frame to the first dump module with the same file name, so two DLLs of one name in different folders resolve against the
first's PDB; rare, and the report keeps the full path for a reader.
