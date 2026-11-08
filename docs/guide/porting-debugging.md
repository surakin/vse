# Porting the debugging facilities to another platform

The debugging facilities (stack traces, the assert family, the crash report, crash handling, core dumps, the scripts) are
written once and run on every platform; a port supplies a handful of thin functions. This page is for the person doing the
Windows port (Phase 13) or the macOS port (Phase 22). Linux is the reference. Mobile and consoles are designed in (an
`Unsupported` answer, skipped death tests) and have no work here.

Read, in order: this page; the checklist for your platform (`engine/foundation/os/windows/debug/BACKEND.md` or
`engine/foundation/os/macos/debug/BACKEND.md`); the conformance list (`tests/debug/BACKEND-CONFORMANCE.md`); and the audit in
`docs/superpowers/specs/2026-11-06-debugging-design.md` section 10.

## What you reuse unchanged

The facade (`engine/foundation/debug/*.cpp` outside `stack_trace_backtrace.cpp`'s platform seams), the assert family and handler
chain, the report writer, the log ring, the crash state helpers, `StartProgramDebugging`, and every test in `tests/debug` that
is not in a `*_linux_*` file. The neutral headers and the platform-free sources compile for Windows and macOS today: the ctests
`debug_cross_syntax_windows` and `debug_cross_syntax_macos` check that they parse for those targets with clang `-fsyntax-only` and
stand-in standard headers (`tests/debug/cross_syntax/shim`: a parse check, not a build; the stand-ins are looser than the real
libraries). Keep them passing as you add code.

## What you write

Every neutral function a platform implements has a `Backend:` line in its doc comment; the ctest `debug_backend_lists` fails if a
labeled function is missing from either checklist, or a checklist names a function without the label. Add the label when you
add a function a platform must implement, and an entry to both `BACKEND.md` files.

Order of work that keeps something testable at each step:

1. **Build it.** Replace the stop in `engine/foundation/CMakeLists.txt` (it names this phase and the checklist) with the real
   source list under `os/<platform>/debug/`. Configure the crash library as on Linux (`SENTRY_BACKEND=breakpad`,
   `SENTRY_TRANSPORT=none`); build libbacktrace with the object-format files for the platform instead of `elf.c`.
2. **Thin functions.** `CurrentThreadId`, `IsDebuggerAttached`, `io::CrashDumpDirectory`, the build id in `InitCrashContext`,
   `DemangleSymbol`, the module list. Run the *crash context*, *assert* and *stack trace* groups.
3. **Death tests.** `DeathTestsSupported` and `RunInChild`, then `FaultSupported` and `InjectFault`. The *death tests* and
   *fault injection* groups tell you, with the `CrashReason` table, when the decoding is right.
4. **Crash handling.** Install the library's handler and write the report from its `on_crash` hook (Windows and macOS pass the
   exception or user context; unlike Linux there is no recorder to write). Then `RegisterThreadForCrashHandling`. Run the
   *crash handler*, *threads* and *programs* groups.
5. **Post-mortem dumps and scripts.** `EnablePostMortemDumps`; the platform's equivalents of `find-core.sh`, `open-core.sh`,
   `store-symbols.sh` and `split-debug.sh`; `crash-report.sh` works wherever `lldb` and `crash_extract` do.

## Windows

```text
Build id        RSDS record in the PE debug directory (GUID + age), hex
OS version      RtlGetVersion
Debugger        IsDebuggerPresent
Dump directory  SHGetKnownFolderPath(FOLDERID_LocalAppData) / <app> / crashes, UTF-8
Death tests     CreateProcessW suspended -> job object (KILL_ON_JOB_CLOSE, DIE_ON_UNHANDLED_EXCEPTION) -> resume; only the pipe ends
                inherited; SetErrorMode(SEM_NOGPFAULTERRORBOX) and _set_abort_behavior in the child; the deadline ends the whole job
Child result    exit code is an NTSTATUS: 0xC0000005 AccessViolation, 0xC000001D IllegalInstruction, 0xC0000094 ArithmeticFault
                (not on arm64), 0xC00000FD StackOverflow; exit code 3 plus a marker written by the child's SIGABRT handler is Abort
Thread          GetCurrentThreadId; SetThreadStackGuarantee for stack overflow; SetThreadDescription for the name
Crash report    from the library's on_crash hook, on Breakpad's handler thread after the dump: EXCEPTION_POINTERS -> reason, address,
                RtlVirtualUnwind walk; the faulting thread from a first vectored handler that only observes; frames as module+offset
                (no DbgHelp while a crash is reported); a gate filter so a second crashing thread waits instead of ending the process
Post-mortem     WER LocalDumps under HKLM only (the per-user key is ignored, measured): read, never written; Limited with the values to set
Symbol store    PDB and image in the WinDbg layout (store-symbols.ps1); crash-report.ps1 and crash_extract --resolve name the frames
Symbols         DbgHelp from the PDBs (decided and built on Phase 13 Day 154; libbacktrace is not built on Windows): capture with
                RtlCaptureStackBackTrace, names and lines with SymFromAddrW / SymGetLineFromAddrW64, inlined frames with
                SymAddrIncludeInlineTrace; every DbgHelp call under one lock, never from the crash hook
Modules         EnumProcessModulesEx + GetModuleInformation + GetModuleFileNameExW, the same immutable table as Linux
```

Done on Day 154: symbols, modules, the crash context, the debugger check, thread ids and names (`os/windows/debug/`). Done on Day 155:
the crash hook and the report, the crash gate, death tests and fault injection (`BusError` has no Windows form), the no-upload check and
the allocation check. Done on Day 156: post-mortem dumps (read-only), `crash_extract` on Windows, the symbol store and the PowerShell scripts.
The Windows order of work is complete; what is left is the Linux run of the shared changes at the phase close.

The traps: an exception filter may call only a few APIs and must not allocate; fast-fail crashes (`__fastfail`, `/GS` failures)
end the process in the kernel, so no filter, handler or `on_crash` sees them (no report, no dump; exit code `0xC0000409`); Breakpad
restores the previous top-level filter while it handles a crash, so a second thread crashing meanwhile needs the gate; the report is
written on another thread, so the faulting thread's id and name must be recorded where it faulted; WER can show a dialog for a
crashed child; every path is wide characters and the engine speaks UTF-8.

## macOS

```text
Build id        LC_UUID of the main image (_dyld_get_image_header(0))
OS version      sysctlbyname("kern.osproductversion")
Debugger        sysctl KERN_PROC_PID, p_flag & P_TRACED
Dump directory  ~/Library/Application Support/<app>/crashes (the container directory when sandboxed)
Death tests     posix_spawn, shared with Linux; no PR_SET_PDEATHSIG: the child watches a pipe from the parent (EOF even if it is killed)
Child result    signals, as Linux; integer divide does not trap on arm64 (FaultSupported(DivideByZero) is false)
Thread          pthread_threadid_np; no alternate stack needed (the library uses Mach exception ports)
Post-mortem     setrlimit(RLIMIT_CORE) and /cores (normally disabled: Limited); crash reports go to ~/Library/Logs/DiagnosticReports
Symbols         DWARF in a dSYM next to the binary (libbacktrace needs it for file and line)
```

The traps: the hardened runtime and notarization restrict what a handler may do; `dsymutil` and `strip` replace `objcopy`; the
symbol store holds dSYM bundles named by UUID.

## Where a test expectation differs by platform

See the table in `tests/debug/BACKEND-CONFORMANCE.md`: the `CrashReason` for a stack overflow on macOS is `AccessViolation`
unless the handler saw it; `DivideByZero` is unsupported on arm64; a fast-fail on Windows is not reported by the library. A test
that cannot hold on a platform skips with a printed reason; it is never deleted.

## Sharing this page

Code blocks above that cannot compile on Linux are plain fences on purpose. Keep it that way, or `ctest -L docs` will try to
compile them.
