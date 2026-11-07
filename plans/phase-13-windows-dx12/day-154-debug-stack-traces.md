# Phase 13 / Day 154 — Debugging I: Stack Traces, Symbols, Crash Context

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 153; `engine/foundation/os/windows/debug/BACKEND.md`, `tests/debug/BACKEND-CONFORMANCE.md`.

The first third of the Windows debugging backend: the parts that need no crash. New directory `engine/foundation/os/windows/debug/`.

## Tasks

- [x] **Vendoring first (moved from Day 155):** the Windows private headers and sources of Breakpad that sentry-native's CMake names for Windows
      (`KNOWN-ISSUES.md`, Phase 7 Day 95), same pinned version, recorded in the vendoring note, so `foundation_debug` configures and builds on Windows.
- [x] **The resolver decision, implemented:** a DbgHelp resolver behind `ResolveStackTrace` (`os/windows/debug/stack_trace_dbghelp.cpp`): `SymInitializeW` once
      with the module list's search path, `SymFromAddrW` and `SymGetLineFromAddrW64` per frame, results written into the facade's fixed buffers; DbgHelp is not
      thread-safe, so every call is under one lock, and the resolver is **not** called from the crash hook (the report records raw addresses and module offsets;
      resolution happens after the crash or in the test parent) — record that rule in `BACKEND.md`. Capture uses `RtlCaptureStackBackTrace` (no DbgHelp).
      `libbacktrace` is not built on Windows: `third_party/libbacktrace`'s CMake branch says so; the facade's two-include gate is updated for the DbgHelp file.
- [x] **`InitDebugFacilities`/`ReloadDebugModules`:** module list from `EnumProcessModulesEx` + `GetModuleInformation` + `GetModuleFileNameExW`, published
      through `module_list.h`'s immutable table; a module loaded later (a `LoadLibrary` in a test) appears after `ReloadDebugModules`.
- [x] **`DemangleSymbol`:** `UnDecorateSymbolName` (MSVC and `clang-cl` both produce MSVC-mangled names; Itanium names do not occur on this target — assert it in
      a test with a `clang-cl`-built function).
- [x] **`InitCrashContext`:** build id from the PE debug directory's CodeView RSDS record (GUID + age, lowercase hex, the same string the symbol store uses on
      Day 156), OS from `RtlGetVersion`, CPU brand and count (`__cpuid`, `GetActiveProcessorCount`), memory (`GlobalMemoryStatusEx`), executable path (UTF-8).
- [x] **`IsDebuggerAttached`** (`IsDebuggerPresent`), **`CurrentThreadId`** (`GetCurrentThreadId`), **`RegisterThreadForCrashHandling`**
      (`SetThreadStackGuarantee` with the size the report needs, measured; `SetThreadDescription` with the crash thread name).
- [x] **Conformance groups** that need no crash: *stack trace*, *crash context*, *assert* (non-fatal parts), *threads* — run them; every case passes or skips with
      a reason written in the test; the PDB-dependent line numbers are checked against a function whose line is known.
- [x] **CMake:** the stop in `engine/foundation/CMakeLists.txt` for Windows is replaced by the real Windows debug source list (today's files only; Day 155 adds the
      crash files); `dbghelp.lib`, `psapi.lib` linked privately.
- [x] **Documentation:** `BACKEND.md` sections marked implemented with what differs; `docs/guide/porting-debugging.md` and `foundation-debugging.md` get the
      Windows resolver and the "no resolution inside the crash" rule; `debug_backend_lists` passes; reference regenerated; `docgen lint` zero.
- [x] **Runs:** Windows `--affected` plain and asan; Linux `--affected` (the facade's gate and CMake changed); quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the DbgHelp lock (a stack trace requested from two threads at once, and from inside the log
      while the lock is held), buffer bounds when a symbol name is longer than the facade's buffer, and modules unloading while a trace is resolved.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** stack traces on Windows resolve function names and lines from PDBs, the crash context carries the PE build id, and the non-crashing conformance groups
pass or skip with a reason.

## Result (2026-11-07, Windows PC)

- **Vendoring:** `sentry-native.zip` 0.17.6 downloaded with the developer's OK (9.1 MB, SHA-256 matches `VENDORED.md`); building the `sentry` target
  until no include was missing needed two Breakpad headers, copied unmodified (`client/windows/common/ipc_protocol.h`,
  `common/windows/string_utils-inl.h`; `VENDORED.md` updated). sentry-native is added to the Windows build before the engine's options; under
  clang-cl its own `-Werror -Wall` (`-Weverything` there) stops on warnings newer than the release, so the target gets `-Wno-error -w`; it needs
  `synchronization.lib` (`WaitOnAddress`) on Windows, linked by `foundation_debug`. On Windows `sentry.h` includes `<windows.h>` itself, so
  `crash_handler_sentry.cpp` includes it inside the enable/disable pair (CLAUDE.md, unity builds step 1).
- **Resolver decided and built:** DbgHelp for both compilers (`os/windows/debug/stack_trace_dbghelp.cpp`): `RtlCaptureStackBackTrace` captures;
  `SymFromAddrW`, `SymGetLineFromAddrW64` and the inline-frame calls (`SymAddrIncludeInlineTrace`, `SymFromInlineContextW`) resolve, under one lock,
  at `address - 1`; the search path is the executable's directory only. libbacktrace is not built on Windows; `FormatStackTrace` moved to the
  neutral `debug/stack_trace_format.cpp` (built on `ResolveStackTrace`, so Linux formats the same way it did). Rule recorded in `BACKEND.md`:
  DbgHelp is never called while a crash is reported. The PDB stores names readable, so `demangle` changes nothing on Windows.
- **Module list** (`EnumProcessModulesEx`, `GetModuleInformation`, `GetModuleFileNameExW`), **crash context** (CodeView RSDS build id, `RtlGetVersion`,
  CPUID brand, `GetActiveProcessorCount`, `GlobalMemoryStatusEx`), **`IsDebuggerAttached`**, **`CurrentThreadId`**, **`RegisterThreadForCrashHandling`**
  (name, `SetThreadDescription`, a 64 KiB stack guarantee: the measured size is Day 155's, which writes the report it is for).
- **Placeholders** for Days 155-156 in `os/windows/debug/crash_pending.cpp` (crash event hook, death tests, fault injection, post-mortem): they report
  themselves unsupported, so every death-test case skips with "this platform cannot start child processes".
- **Tests:** `tests/debug` on Windows (plain and ASan) with today's groups: `stack_trace_test`, `assert_family_test`, `death_test_*` (skipping),
  `crash_state_test`, `failure_hooks_test`, the new `stack_trace_windows_test.cpp` (decorated names, no Itanium names under clang-cl, the PDB line,
  the module table, the build id, thread ids, the search path), `debug_checks_off_tests`, and the backend-list gates. Windows test pieces:
  `resource_guard_windows.cpp`, the plugin exports its function and links `/INCREMENTAL:NO` (otherwise `GetProcAddress` returns a jump thunk),
  `tests/windows/support/symbol_search_path.h`. `platform_neutral_check` rule 1 now exempts `*_windows*` and `*_macos*` files as it did `*_linux*`
  (fixture added). Under MSVC Debug the inlined-frame case skips (MSVC inlines nothing at `/Od`); under clang-cl it runs and passes.
  `crash_handler_test`, `crash_report_test` (its Linux-only build-id case is in a neutral file), `saved_dumps_test`, `core_dump_test` and
  `crash_extract` join with their days.
- **Runs** (after the review fixes): `scripts/test-phase.sh --affected --crashes` printed
  `ran 472 of 727 tests (phases: all; flavors: plain; display: included; crash tests: included) in 24 s`, with `--flavor asan`
  `ran 255 of 727 tests (phases: all; flavors: asan; display: included; crash tests: included) in 18 s`; `ctest --preset windows-msvc` 727 of 727,
  `windows-clang-cl` 503 of 503, non-unity 727 of 727.
- **For the Linux run at the phase close:** shared files changed today: `debug/stack_trace_backtrace.cpp` (formatting moved out),
  `debug/stack_trace_format.cpp` (new), `debug/crash_handler_sentry.cpp` (the include pair), `engine/foundation/CMakeLists.txt`, the root
  `CMakeLists.txt` (Windows block only), `tests/debug/CMakeLists.txt`, `stack_trace_test.cpp`, `plugin.cpp`, `tools/platform_neutral_check/check.cmake`,
  `third_party/sentry_native/` (two headers), the guides. On Linux: `scripts/test-phase.sh --affected --crashes` with asan and tsan (the debug
  suite, including the signal-handler allocation test that goes through the moved formatter), `ctest -L docs`.

## Post-implementation code review

`/code-review high` over the day's diff: **6 findings, 2 fixed, 4 declined.**

Fixed: (1) `SymInitializeW` with a null search path honoured `_NT_SYMBOL_PATH`, which can name a symbol server: a network path in the debugging
facilities. The search path is now the executable's directory, set at start and on every reload; a test sets a `srv*...https://` value and checks
it is not used (it failed before the fix). (2) Frames were looked up at the return address itself; after a call to a `[[noreturn]]` function that
can name the next function. Lookups use `address - 1`, as libbacktrace does (no deterministic regression test: it depends on code layout; the
line checks still pass).

Declined: (3) an assertion's trace on Windows is resolved through DbgHelp, which allocates from the process heap (Linux promises no `malloc`
after `InitDebugFacilities`); an assert inside a CRT allocation could deadlock while printing; filed. (4) `FillPlatformCrashContext` uses the
loader (`GetProcAddress` for `RtlGetVersion`); programs fill the context at startup. (5) `FrameInfo::function`'s header comment says "mangled",
which is not true on Windows (the guide and `BACKEND.md` say so); the reference regenerates from Day 157. (6) The Linux side of the shared changes
runs at the phase close.
