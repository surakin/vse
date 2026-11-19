# macOS debugging backend (Phase 21)

What Phase 21 implements to bring up the debugging facilities on macOS (Day 290: stack traces, the module list, the crash context; Day 291: the
crash handler, death tests, fault injection, the debugger check, post-mortem dumps; Day 292: dumps and scripts; every function is built); Linux is the reference
(`engine/foundation/os/linux/debug/`). Everything else is shared: the facade, the assert family, the report writer, the log ring,
the crash state, the tests. `posix_spawn`, `setrlimit`, signals and `sigaltstack` are the same calls as on Linux, so most of the
Linux files compile with small changes. See `docs/guide/porting-debugging.md` and `tests/debug/BACKEND-CONFORMANCE.md`.

The crash library (Breakpad backend) and libbacktrace list macOS as supported at the pinned versions (audit in the design spec).
**Known limits:** libbacktrace on macOS needs the dSYM next to the binary (or a debug build) for file and line; the library takes
Mach exceptions itself (not signals) on macOS, so the Linux signal recorder is **not** used there; the hardened runtime and
notarization restrict some of what a crash handler may do.

## Functions with a `Backend:` label (a test keeps this list in step with the headers)

### `InitCrashContext`
Build id: the `LC_UUID` load command of the main image (`_dyld_get_image_header(0)`), hex. OS name and version:
`sysctlbyname("kern.osproductversion")`. CPU brand: `machdep.cpu.brand_string` (Apple silicon has it too, `Apple M1` and so on: no fallback
needed, checked on Day 290's review); count: `hw.logicalcpu`; memory: `hw.memsize`. Executable: `_NSGetExecutablePath`. Groups: *crash context*.

As built (Day 290, `os/macos/debug/crash_context_macos.cpp`): as described, nothing allocated (the context may be filled on the assert path); the executable is
`_NSGetExecutablePath`'s path without `realpath`. Compared with `sysctl -n` by `tests/debug/macos/stack_trace_macos_test.cpp`.

### `EnablePostMortemDumps`
`setrlimit(RLIMIT_CORE)` as on Linux; cores are written to `/cores` (`kern.corefile`), which is normally disabled
(`sysctl kern.coredump`), so report `Limited` with that note; the system also writes crash reports to
`~/Library/Logs/DiagnosticReports` (name that in the destination). Groups: *post-mortem*.

As built (Day 291, brought forward from Day 292; `os/macos/debug/core_dump_macos.cpp`): the limit raised as on Linux; the destination names
`kern.corefile` and `~/Library/Logs/DiagnosticReports`; `Limited` with how to lift it when `kern.coredump` is 0 or the corefile directory is not
writable by the user (`/cores` is root's, 755, by default: on the development Mac the result is `Limited`).

### `FaultSupported` and `InjectFault`
As Linux (same signals). Integer divide by zero does not trap on arm64: `FaultSupported(DivideByZero)` is false there.
`IllegalInstruction`: `brk`/`udf` on arm64. Expected reasons as Linux. Groups: *fault injection*.

As built (Day 291, `os/macos/debug/fault_injection.cpp`): Linux's, except `BusError`, which writes into a page mapped `PROT_NONE` (macOS
delivers a protection fault on a mapped page as SIGBUS; it has no `memfd_create`).

### `IsDebuggerAttached`
`sysctl` with `CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()` and test `kp_proc.p_flag & P_TRACED`. Groups: *assert*.

**`CrashDumpDirectory`** (moved to the file layer: see `engine/foundation/io/BACKEND.md`).
`~/Library/Application Support/<app>/crashes` (from `NSSearchPathForDirectoriesInDomains` or `$HOME`), created 0700.
Sandboxed apps: the container directory. Groups: *dump directory*.

As built (Day 291, `os/macos/debug/debugger.cpp`): as described, cached for a second as on Linux; true under `lldb` (checked by hand) and in a
child that called `ptrace(PT_TRACE_ME)` (tested).

### `RegisterThreadForCrashHandling`
`sigaltstack` on the thread is still useful for signal-based handlers, but the library uses Mach exception ports, which do
not need it: set the name and return true. Groups: *threads*.

As built (Day 291, `os/macos/debug/crash_hook.cpp`): sets the engine's crash name and the thread's pthread name (the report reads the latter from
Breakpad's thread), returns whether the handler is installed. A stack overflow on a registered worker is reported (tested).

### `InitDebugFacilities` and `ReloadDebugModules`
The module list from `_dyld_image_count`, `_dyld_get_image_name`, `_dyld_get_image_header`, and
`_dyld_register_func_for_add_image` for changes. Groups: *stack trace*.

As built (Day 290, `os/macos/debug/module_list.cpp`; libbacktrace on Mach-O, `third_party/libbacktrace`): each image's range is its `__TEXT` segment
plus the slide, not the span of its segments (in the shared cache those overlap other images: Day 290 saw libobjc claim libsystem_pthread's frames).
The images come from `_dyld_register_func_for_add_image` and `..._remove_image` into a registry under a mutex (walking dyld's list by index races with
`dlclose`: indices shift, and a header can be unmapped mid-walk); a refresh copies the registry, and is still explicit, as on Linux
(`ReloadDebugModules` after `dlopen`). libbacktrace skips `MH_BUNDLE`
images; plug-ins are dylibs. File and line need the dSYM (`engine_dsym`).

### `DemangleSymbol`
`abi::__cxa_demangle`. Groups: *stack trace*.

As built (Day 290): the shared `stack_trace_backtrace.cpp`, unchanged.

### `CurrentThreadId`
`pthread_threadid_np(nullptr, &id)`. Groups: *crash report*.

As built (Day 290): the low 31 bits, never 0, so the crash report and the job system's logs (`jobs/macos/thread_platform.cpp`) print one number.

### `DeathTestsSupported` and `RunInChild`
`posix_spawn` shared with Linux; `PR_SET_PDEATHSIG` does not exist on macOS: the child watches a pipe from the parent and exits when it closes (a pipe
reaches EOF even if the parent is killed; an `atexit` handler would not run then). `RLIMIT_CORE` and signal decoding as Linux. Groups: *death tests*.

As built (Day 291): Linux's backend moved to `os/posix/debug/death_test_posix.cpp` with the hooks of `posix_debug.h` (the pipe, the program's path,
the environment, dying with the parent). The watch pipe's read end reaches the child as descriptor 3; a thread there kills the child at end of file.
A child whose parent is killed with SIGKILL is gone within moments (tested).

## Pieces without a neutral label

**As built (Day 291), the crash handler:** sentry's Breakpad backend with `SENTRY_TRANSPORT=none` (four Breakpad private headers re-vendored,
`third_party/sentry_native/VENDORED.md`). Breakpad takes hardware faults as Mach exceptions on its own thread and aborts through a SIGABRT
handler; sentry calls `on_crash` with the crashed thread's register context only, and `os/macos/debug/crash_hook.cpp` builds the report from it:
the abort path when the context's stack pointer lies on the current thread's stack, otherwise the crashed thread found among the task's threads
by its registers, the reason from that thread's exception state (`x86_EXCEPTION_STATE64`: the context's own exception part is zero), the stack
walked from the frame pointer through `vm_read_overwrite`. After the dump Breakpad ends the process with `_exit(<exception type>)` (exit code 1
for `EXC_BAD_ACCESS`), not a signal; without the handler a fault ends it by signal.

**A host's handler installed before the crash handler (crash handler chaining Day 3):** `detail::NoteHostSignalHandlers` reads each fatal
signal's disposition before `sentry_init`. After the report, when the host has a handler for the crash's signal (an access fault: `SIGSEGV` or
`SIGBUS`, whichever the kernel picks; `SIGILL`, `SIGFPE`, `SIGTRAP`; an abort: `SIGABRT`), `crash_hook.cpp` puts a one-shot relay on it and sets
the vendored Breakpad's local switch `breakpad_mac_continue_after_dump` (`third_party/sentry_native/VENDORED.md`): Breakpad writes the envelope and
does not `_exit`; it uninstalls itself, resumes the threads it suspended and forwards the exception to the ports from before it (itself, not
through libSystem's `exc_server`, which a shared engine library cannot serve), or answers it unhandled, so the kernel delivers the BSD signal on
the faulting thread. For an abort the hook raises a `SIGABRT` that stays blocked until Breakpad's handler returns. The relay puts the default
action back and calls the host's handler once (`SA_NODEFER`: a fault inside it reaches the default action); then the fault ends the process
with its signal. Without a host handler for the crash's signal nothing changes (`_exit`). Tested in `crash_end_to_end_macos_test.cpp` and
`crash_host.c` (shared library). ASan builds suppress ASan's write-interceptor report on
Breakpad's stack copy into the dump (`__asan_default_suppressions`).

- **Crash handler build:** `SENTRY_BACKEND=breakpad`, `SENTRY_TRANSPORT=none`. The library's `on_crash` hook receives a
  `ucontext` on macOS (unlike Linux Breakpad, where it is null); write the report from the hook and do not install the Linux
  signal recorder. Stack overflow: Mach exceptions are delivered on a separate port thread, so no alternate stack is needed.
- **`detail::InstallCrashEventHook`** has no macOS implementation; `OnCrashEvent` is the `on_crash` hook.
- **Scripts:** the Linux shell scripts mostly work (`lldb` is the system debugger, `dsymutil`/`strip` replace `objcopy`; the
  symbol store holds dSYM bundles named by UUID). `find-core.sh` and `open-core.sh` need `/cores` rules instead of
  `coredumpctl`.
  **As built (Day 292):** `store-symbols.sh`, `split-debug.sh`, `crash-report.sh`, `find-core.sh` and `open-core.sh` each have a macOS branch
  (`docs/guide/porting-debugging.md` lists the differences); the store is `<store>/<name>/<UUID>/<name>.dSYM`, one per architecture.
  `crash_extract` reads Breakpad's padded module list and, with `--lldb`, completes the contexts' register flags so `lldb` reads the dump.
  Tested by `tests/debug/macos/scripts_macos_test.cpp` (a core saved by `lldb`, since `/cores` is root's).
- **CMake:** replace the stop in `engine/foundation/CMakeLists.txt`; no `--build-id` (the linker writes `LC_UUID`); build with
  `-g` and `dsymutil` for readable traces. libbacktrace uses `macho.c`.
