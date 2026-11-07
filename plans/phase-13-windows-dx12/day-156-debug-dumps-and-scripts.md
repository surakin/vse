# Phase 13 / Day 156 — Debugging III: Dumps, Symbol Store and Scripts

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 155.

Make a Windows crash readable after the fact: the dump directory, post-mortem dumps, a symbol store and the PowerShell versions of the crash scripts.

## Tasks

- [ ] **`CrashDumpDirectory`** in use by the crash handler (Day 152's private directory); saved-dump pruning (`saved_dumps.cpp`) works on Windows paths and the
      test that checks it runs.
- [ ] **`EnablePostMortemDumps`:** Windows Error Reporting `LocalDumps` for the program's own key (`DumpFolder`, `DumpType`, `DumpCount`); per-user
      (`HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\<exe>`) if WER honors it on this Windows build — test it — otherwise `Limited` with the
      note naming the `HKLM` values to set by hand; never writes outside the program's own key; the test removes what it wrote.
- [ ] **Symbol store:** `scripts/store-symbols.ps1` copies each PDB (and the executable, for minidump loading) into `<store>/<name>.pdb/<GUID+age>/<name>.pdb`
      (and `<name>.exe/<timestamp+size>/<name>.exe`), the layout WinDbg and Visual Studio read; the GUID+age is the build id from Day 154. Test: store the demo's
      symbols, read the id back from the store path.
- [ ] **Scripts:** `scripts/crash-report.ps1` (the logic of `crash-report.sh`: find the newest dump and report, resolve frames against a symbol store using
      `crash_extract` and DbgHelp), `scripts/find-dump.ps1` (the WER and engine dump locations), `scripts/open-dump.ps1` (open in `cdb` or WinDbg with the store
      on the symbol path). The shell scripts stay for Linux; `crash_report_script_*` tests get Windows twins.
- [ ] **`tools/crash_extract` on Windows:** reads a Windows minidump's module list and the engine report; a ctest crashes the demo (`--crash` test hook, or a
      death-test child) and checks the extracted report names the faulting function after resolution against the store.
- [ ] **Human check (record in this file, or file in `KNOWN-ISSUES.md` if not done):** open one minidump in WinDbg/`cdb` with the stored symbols and confirm
      the call stack is readable.
- [ ] **Remaining conformance groups:** *dump directory*, *post-mortem*, *symbols* — pass or skip with a reason; `debug_backend_lists` and
      `debug_cross_syntax_windows` pass. `BACKEND.md`'s every section is now marked implemented or explicitly not applicable.
- [ ] **Documentation:** `docs/guide/porting-debugging.md` Windows order of work complete; `foundation-debugging.md` "Reading a crash on Windows" (where dumps
      are, the symbol store, the scripts); reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `scripts/test-phase.sh --affected --crashes` plain and asan; Linux `--affected` only if shared debug code changed (record); quote.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: a script that deletes or overwrites dumps it did not create, registry writes outside the
      program's key, and symbol-store paths built from untrusted dump contents.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every function in the Windows debugging checklist is implemented or marked not applicable, a crash on Windows can be read back with symbols from the
store through the scripts, and every debugging conformance group passes or skips with a reason.
