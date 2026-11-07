# Phase 12 / Day 147 — Debugging I: Stack Traces, Symbols, Crash Context

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 146; `engine/foundation/os/windows/debug/BACKEND.md`, `tests/debug/BACKEND-CONFORMANCE.md`.

The first third of the Windows debugging backend: the parts that need no crash. New directory `engine/foundation/os/windows/debug/`.

## Tasks

- [ ] **The resolver decision, implemented:** a DbgHelp resolver behind `ResolveStackTrace` (`os/windows/debug/stack_trace_dbghelp.cpp`): `SymInitializeW` once
      with the module list's search path, `SymFromAddrW` and `SymGetLineFromAddrW64` per frame, results written into the facade's fixed buffers; DbgHelp is not
      thread-safe, so every call is under one lock, and the resolver is **not** called from the crash hook (the report records raw addresses and module offsets;
      resolution happens after the crash or in the test parent) — record that rule in `BACKEND.md`. Capture uses `RtlCaptureStackBackTrace` (no DbgHelp).
      `libbacktrace` is not built on Windows: `third_party/libbacktrace`'s CMake branch says so; the facade's two-include gate is updated for the DbgHelp file.
- [ ] **`InitDebugFacilities`/`ReloadDebugModules`:** module list from `EnumProcessModulesEx` + `GetModuleInformation` + `GetModuleFileNameExW`, published
      through `module_list.h`'s immutable table; a module loaded later (a `LoadLibrary` in a test) appears after `ReloadDebugModules`.
- [ ] **`DemangleSymbol`:** `UnDecorateSymbolName` (MSVC and `clang-cl` both produce MSVC-mangled names; Itanium names do not occur on this target — assert it in
      a test with a `clang-cl`-built function).
- [ ] **`InitCrashContext`:** build id from the PE debug directory's CodeView RSDS record (GUID + age, lowercase hex, the same string the symbol store uses on
      Day 149), OS from `RtlGetVersion`, CPU brand and count (`__cpuid`, `GetActiveProcessorCount`), memory (`GlobalMemoryStatusEx`), executable path (UTF-8).
- [ ] **`IsDebuggerAttached`** (`IsDebuggerPresent`), **`CurrentThreadId`** (`GetCurrentThreadId`), **`RegisterThreadForCrashHandling`**
      (`SetThreadStackGuarantee` with the size the report needs, measured; `SetThreadDescription` with the crash thread name).
- [ ] **Conformance groups** that need no crash: *stack trace*, *crash context*, *assert* (non-fatal parts), *threads* — run them; every case passes or skips with
      a reason written in the test; the PDB-dependent line numbers are checked against a function whose line is known.
- [ ] **CMake:** the stop in `engine/foundation/CMakeLists.txt` for Windows is replaced by the real Windows debug source list (today's files only; Day 148 adds the
      crash files); `dbghelp.lib`, `psapi.lib` linked privately.
- [ ] **Documentation:** `BACKEND.md` sections marked implemented with what differs; `docs/guide/porting-debugging.md` and `foundation-debugging.md` get the
      Windows resolver and the "no resolution inside the crash" rule; `debug_backend_lists` passes; reference regenerated; `docgen lint` zero.
- [ ] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (the facade's gate and CMake changed); quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the DbgHelp lock (a stack trace requested from two threads at once, and from inside the log
      while the lock is held), buffer bounds when a symbol name is longer than the facade's buffer, and modules unloading while a trace is resolved.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** stack traces on Windows resolve function names and lines from PDBs, the crash context carries the PE build id, and the non-crashing conformance groups
pass or skip with a reason.
