# Phase 26 / Day 292 — Debugging III: Dumps, Symbol Store and Scripts

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 291.

Minidumps, core files, the dSYM symbol store and the shell scripts on macOS: the last third of the debugging backend.

## Tasks

- [ ] **Minidumps:** Breakpad writes a minidump on macOS into `CrashDumpDirectory()`; `crash_extract` reads it (the module list carries the `LC_UUID`s) and
      resolves frames through the store; a test crashes a child and checks the dump's modules and the resolved top frame.
- [ ] **`EnablePostMortemDumps`:** `setrlimit(RLIMIT_CORE)` as Linux; report `Limited` naming `/cores` and `kern.coredump` (normally off) and the
      DiagnosticReports directory; the *post-mortem* group checks the reported destination, never that a core appeared (it is the system's setting).
- [ ] **Symbol store:** `scripts/store-symbols.sh` on macOS stores dSYM bundles under `<store>/<name>/<UUID>/<name>.dSYM` (the UUID from
      `dwarfdump --uuid`), so `lldb`'s `target symbols add` and `crash_extract` find them; `split-debug.sh` uses `dsymutil` and `strip -S` instead of
      `objcopy`; both keep working on Linux (shared scripts: file the Linux run).
- [ ] **`crash-report.sh`, `find-core.sh`, `open-core.sh`:** BSD userland (`stat -f`, no `readlink -f`, `date` without `%N`), `/cores` rules instead of
      `coredumpctl`, `lldb` as the debugger; the script tests run on macOS and skip with a reason where a tool is missing.
- [ ] **Conformance groups** *dump directory* and *post-mortem* pass or skip with a reason; the whole `tests/debug` suite passes with `--crashes`.
- [ ] **Documentation:** `foundation-debugging.md` and `porting-debugging.md` macOS sections completed (store layout, the scripts, `lldb` commands);
      `os/macos/debug/BACKEND.md` fully marked as built; `debug_backend_lists` passes; `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --crashes --flavor all` (quote); Linux run of the shared scripts filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the scripts on paths with spaces (`~/Library/Application Support`), quoting, the
      store's handling of a dSYM with several architectures.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function of `os/macos/debug/BACKEND.md` is built, the whole `tests/debug` suite passes or skips with a reason on macOS in three flavors
with crash tests included, a minidump from a crashed child is resolved through the dSYM store, and the scripts work on both macOS and Linux.
