# Debugging backend conformance

What a platform backend (`engine/foundation/os/<platform>/debug/`) must satisfy. The tests in `tests/debug` use only the neutral
API; Linux-only details live in `*_linux_*` files. A backend is done when every group below passes or skips with the reason
shown. The platform checklists are `os/windows/debug/BACKEND.md` and `os/macos/debug/BACKEND.md`.

| Group (test tags) | Neutral functions | Must hold | May skip |
|---|---|---|---|
| *death tests* `[death]` | `DeathTestsSupported`, `RunInChild` | a child runs a registered function, its output and exit are captured, a timeout kills it and its descendants | all death-test-based groups, if `DeathTestsSupported()` is false (iOS, consoles) |
| *fault injection* `[fault]` | `FaultSupported`, `InjectFault` | each supported kind ends the child with the `CrashReason` below | a kind for which `FaultSupported` is false |
| *assert* `[assert]` | `IsDebuggerAttached`, the assert family, `SetAssertHandler` | message, trace and context are written once; ensure continues; the handler chain works | none |
| *stack trace* `[stacktrace]` | `InitDebugFacilities`, `ReloadDebugModules`, `CaptureStackTrace`, `ResolveStackTrace`, `DemangleSymbol` | the faulting function is in the trace with file and line when debug information exists; a stripped binary gives module and offset | file and line where no debug information is available |
| *crash context* `[crashreport]` | `InitCrashContext`, `GetCrashContext` | build id equals the binary's, OS, CPU, memory, executable are non-empty | device model (empty on desktop) |
| *crash report* `[crashreport]` | `WriteCrashReport`, `WriteCrashContext`, providers, the log ring | sections in order; no allocation once initialized; a faulting provider loses only the rest | none |
| *threads* `[crash]` | `RegisterThreadForCrashHandling`, `CurrentThreadId` | a fault on another thread names that thread; two threads at once give one report | the stack-overflow case, if the platform cannot handle it (say why) |
| *crash handler* `[crash]` | `InstallCrashHandler`, `UninstallCrashHandler`, `GetCrashDumpDirectory` | a fault leaves one envelope with a minidump of every thread, a report file, and the original signal or exception code | none |
| *dump directory* `[crash]` | `GetCrashDumpDirectory`, `EnsurePrivateDirectory` | private to the user, named after the application | none |
| *post-mortem* `[postmortem]` | `EnablePostMortemDumps` | `Enabled` or `Limited` with a note, never silent | `Unsupported` (iOS, consoles) |
| *programs* `[programs]` | `StartProgramDebugging`, `RunCrashTest` | `--crash-test` kinds give the reports above | none |

## Required `CrashReason` per `FaultKind`

| FaultKind | Linux | Windows | macOS |
|---|---|---|---|
| `NullWrite` | `AccessViolation` (SIGSEGV) | `AccessViolation` (0xC0000005) | `AccessViolation` (SIGSEGV or SIGBUS: report `AccessViolation`) |
| `IllegalInstruction` | `IllegalInstruction` (SIGILL) | `IllegalInstruction` (0xC000001D) | `IllegalInstruction` (SIGILL) |
| `DivideByZero` | `ArithmeticFault` (SIGFPE) | `ArithmeticFault` (0xC0000094) on x86-64; unsupported on arm64 | `ArithmeticFault` on x86-64; unsupported on arm64 |
| `StackOverflow` | `StackOverflow` when seen in the process, else `AccessViolation` | `StackOverflow` (0xC00000FD) | `AccessViolation` unless the handler saw it |
| `Abort` | `Abort` (SIGABRT) | `Abort` (exit code 3 plus the marker the child writes from its SIGABRT handler) | `Abort` (SIGABRT) |
